// Prevents additional console window on Windows in release, DO NOT REMOVE!!
#![cfg_attr(not(debug_assertions), windows_subsystem = "windows")]

use std::collections::HashMap;
use std::fs;
use std::path::{Path, PathBuf};
use serde::{Deserialize, Serialize};
use tauri::api::process::{Command, CommandEvent};

mod cli_args;
use cli_args::{Colour, Render, View};

fn project_dir() -> PathBuf {
    let mut path = std::env::current_exe().unwrap();
    for _ in 0..4 {
        path = path.parent().unwrap().to_path_buf();
    }
    path
}

fn settings_path() -> PathBuf {
    project_dir().join("remote-settings.json")
}

#[derive(Serialize, Deserialize, Default, Clone)]
struct RemoteSettings {
    endpoint: String,
    api_key:  String,
}

#[tauri::command]
fn get_remote_settings() -> RemoteSettings {
    let path = settings_path();
    fs::read_to_string(&path)
        .ok()
        .and_then(|s| serde_json::from_str(&s).ok())
        .unwrap_or_default()
}

#[tauri::command]
fn save_remote_settings(endpoint: String, api_key: String) -> Result<(), String> {
    let settings = RemoteSettings { endpoint, api_key };
    let json = serde_json::to_string(&settings).map_err(|e| e.to_string())?;
    fs::write(settings_path(), json).map_err(|e| e.to_string())
}

#[tauri::command]
fn get_project_dir() -> String {
    project_dir().display().to_string()
}

#[tauri::command]
async fn get_available_modes() -> Result<Vec<String>, String> {
    let (mut rx, _child) = Command::new_sidecar("mandelbrot_visualizer")
        .map_err(|e| e.to_string())?
        .args(["--list-modes"])
        .spawn()
        .map_err(|e| e.to_string())?;

    let mut modes = Vec::new();
    while let Some(event) = rx.recv().await {
        match event {
            CommandEvent::Stdout(line) => {
                let mode = line.trim().to_string();
                if !mode.is_empty() { modes.push(mode); }
            }
            CommandEvent::Terminated(_) => break,
            _ => {}
        }
    }
    Ok(modes)
}

async fn run_sidecar(mode: String, args: Vec<String>, env: HashMap<String, String>) -> Result<String, String> {
    eprintln!("[renderer] starting, mode {}", mode);

    let (mut rx, _child) = Command::new_sidecar("mandelbrot_visualizer")
        .map_err(|e| {
            eprintln!("[renderer] cannot find the mandelbrot_visualizer binary: {}", e);
            format!("Cannot find the renderer binary (mandelbrot_visualizer): {}", e)
        })?
        .args(args)
        .envs(env)
        .spawn()
        .map_err(|e| {
            eprintln!("[renderer] failed to start: {}", e);
            format!("Failed to start the renderer: {}", e)
        })?;

    // Keep what the renderer wrote: stderr explains a failure, and stdout (stage timings, the remote worker's
    // answer) is handed to the UI on success.
    let mut stderr_lines: Vec<String> = Vec::new();
    let mut stdout_text = String::new();

    while let Some(event) = rx.recv().await {
        match event {
            CommandEvent::Stdout(line) => {
                println!("{}", line.trim_end());
                stdout_text.push_str(line.trim_end());
                stdout_text.push('\n');
            }
            CommandEvent::Stderr(line) => {
                eprint!("{}", line);
                stderr_lines.push(line.trim_end().to_string());
            }
            CommandEvent::Error(message) => {
                eprintln!("[renderer] error: {}", message);
                stderr_lines.push(message);
            }
            CommandEvent::Terminated(p) => {
                eprintln!("[renderer] finished with exit code {:?}", p.code);
                return match p.code {
                    Some(0) => Ok(stdout_text),
                    code => {
                        let details = stderr_lines.join("\n");
                        let code_text = code.map(|c| c.to_string()).unwrap_or_else(|| "unknown".to_string());
                        Err(if details.is_empty() {
                            format!("The renderer failed (exit code {}) without an error message.", code_text)
                        } else {
                            details
                        })
                    }
                };
            }
            _ => {}
        }
    }
    Err("The renderer stopped without reporting a result.".to_string())
}

fn remote_env(mode: &str) -> HashMap<String, String> {
    if mode != "CUDA_REMOTE" {
        return HashMap::new();
    }
    let settings = get_remote_settings();
    let mut env = HashMap::new();
    env.insert("REMOTE_ENDPOINT".to_string(), settings.endpoint);
    env.insert("REMOTE_API_KEY".to_string(),  settings.api_key);
    env
}

// Where a render is written: the file shown in the app by default, or `output_filename` (a plain file name, no
// folders) inside generated-files/exports for an export.
fn output_path(output_filename: Option<String>) -> Result<String, String> {
    let mut folder = project_dir().join("generated-files");
    if output_filename.is_some() {
        folder = folder.join("exports");
    }
    let name = output_filename.unwrap_or_else(|| "mandelbrot_set.png".to_string());
    if Path::new(&name).file_name().map(|n| n.to_string_lossy() != name).unwrap_or(true) {
        return Err(format!("Invalid output file name: {}", name));
    }
    fs::create_dir_all(&folder).map_err(|e| e.to_string())?;
    Ok(folder.join(name).to_string_lossy().into_owned())
}

// Where an on-screen render keeps its escape results, so the colours can be changed without rendering again.
fn escape_cache_path() -> String {
    project_dir().join("generated-files").join("escape_cache.bin").to_string_lossy().into_owned()
}

// The renderer's arguments are built in cli_args.rs (and tested against the real renderer in contract-tests/).
#[tauri::command]
async fn generate_mandelbrot(
    mode: String,
    re_start: f64, re_end: f64,
    im_start: f64, im_end: f64,
    max_iter: i32, palette_length: i32,
    palette_id: i32, samples: i32,
    escape_radius: f64,
    color_spread: f64,
    histogram: bool,
    color_phase: f64,
    histogram_cycles: f64,
    output_filename: Option<String>,
    width: Option<i32>, height: Option<i32>,
) -> Result<String, String> {
    let env = remote_env(&mode);
    let on_screen = output_filename.is_none();
    let render = Render {
        mode: &mode,
        view: View::Double { re_start, re_end, im_start, im_end },
        max_iter, samples, escape_radius,
        colour: Colour { palette_id, palette_length, color_spread, histogram, color_phase, histogram_cycles },
        width, height,
    };
    let output = output_path(output_filename)?;
    let cache = escape_cache_path();
    let args = cli_args::render_args(&render, &output, if on_screen { Some(cache.as_str()) } else { None })?;
    run_sidecar(mode, args, env).await
}

#[tauri::command]
async fn generate_mandelbrot_hp(
    mode: String,
    re_start: String, re_end: String,
    im_start: String, im_end: String,
    fraction_words: u32,
    max_iter: i32, palette_length: i32,
    palette_id: i32, samples: i32,
    escape_radius: f64,
    color_spread: f64,
    histogram: bool,
    color_phase: f64,
    histogram_cycles: f64,
    output_filename: Option<String>,
    width: Option<i32>, height: Option<i32>,
) -> Result<String, String> {
    let env = remote_env(&mode);
    let on_screen = output_filename.is_none();
    let render = Render {
        mode: &mode,
        view: View::Fixed {
            re_start: &re_start, re_end: &re_end, im_start: &im_start, im_end: &im_end, fraction_words,
        },
        max_iter, samples, escape_radius,
        colour: Colour { palette_id, palette_length, color_spread, histogram, color_phase, histogram_cycles },
        width, height,
    };
    let output = output_path(output_filename)?;
    let cache = escape_cache_path();
    let args = cli_args::render_args(&render, &output, if on_screen { Some(cache.as_str()) } else { None })?;
    run_sidecar(mode, args, env).await
}

// Paints the image shown in the app again with other colour settings, from the escape results saved by the last
// on-screen render. Nothing is iterated, so this is much faster than a render.
#[tauri::command]
async fn recolour_mandelbrot(
    palette_id: i32, palette_length: i32,
    color_spread: f64,
    histogram: bool,
    color_phase: f64,
    histogram_cycles: f64,
) -> Result<String, String> {
    let colour = Colour { palette_id, palette_length, color_spread, histogram, color_phase, histogram_cycles };
    let args = cli_args::recolour_args(&escape_cache_path(), &output_path(None)?, &colour);
    run_sidecar("recolour".to_string(), args, HashMap::new()).await
}

fn main() {
    tauri::Builder::default()
        .invoke_handler(tauri::generate_handler![
            generate_mandelbrot,
            generate_mandelbrot_hp,
            recolour_mandelbrot,
            get_project_dir,
            get_available_modes,
            get_remote_settings,
            save_remote_settings,
        ])
        .run(tauri::generate_context!())
        .expect("error while running tauri application");
}
