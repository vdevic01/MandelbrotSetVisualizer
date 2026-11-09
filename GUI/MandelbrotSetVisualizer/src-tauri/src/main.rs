// Prevents additional console window on Windows in release, DO NOT REMOVE!!
#![cfg_attr(not(debug_assertions), windows_subsystem = "windows")]

use std::collections::HashMap;
use std::fs;
use std::path::PathBuf;
use serde::{Deserialize, Serialize};
use tauri::api::process::{Command, CommandEvent};

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

async fn run_sidecar(args: Vec<String>, env: HashMap<String, String>) -> Result<String, String> {
    let mode = args.first().cloned().unwrap_or_default();
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

#[tauri::command]
async fn generate_mandelbrot(
    mode: String,
    re_start: f64, re_end: f64,
    im_start: f64, im_end: f64,
    max_iter: i32, palette_length: i32,
    palette_id: i32, samples: i32,
    escape_radius: f64,
) -> Result<String, String> {
    let env = remote_env(&mode);
    let mut args = vec![
        mode,
        project_dir().join("generated-files").join("mandelbrot_set.png").to_string_lossy().into_owned(),
        max_iter.to_string(), palette_length.to_string(),
        palette_id.to_string(), samples.to_string(),
        "0".to_string(),
        re_start.to_string(), re_end.to_string(),
        im_start.to_string(), im_end.to_string(),
    ];
    args.push("--escape-radius".to_string());
    args.push(escape_radius.to_string());
    run_sidecar(args, env).await
}

#[tauri::command]
async fn generate_mandelbrot_hp(
    mode: String,
    re_start: Vec<u32>, re_end: Vec<u32>,
    im_start: Vec<u32>, im_end: Vec<u32>,
    fraction_words: u32,
    max_iter: i32, palette_length: i32,
    palette_id: i32, samples: i32,
    escape_radius: f64,
) -> Result<String, String> {
    // Every coordinate has one whole word plus `fraction_words` fraction words.
    let words = fraction_words as usize + 1;
    for (name, coordinate) in [("re_start", &re_start), ("re_end", &re_end), ("im_start", &im_start), ("im_end", &im_end)] {
        if coordinate.len() != words {
            return Err(format!(
                "{} has {} words but {} fraction words need {}",
                name, coordinate.len(), fraction_words, words
            ));
        }
    }

    let env = remote_env(&mode);
    let mut args = vec![
        mode,
        project_dir().join("generated-files").join("mandelbrot_set.png").to_string_lossy().into_owned(),
        max_iter.to_string(), palette_length.to_string(),
        palette_id.to_string(), samples.to_string(),
        "1".to_string(),
    ];
    for coordinate in [&re_start, &re_end, &im_start, &im_end] {
        args.extend(coordinate.iter().map(|x| x.to_string()));
    }
    args.push("--fraction-words".to_string());
    args.push(fraction_words.to_string());
    args.push("--escape-radius".to_string());
    args.push(escape_radius.to_string());
    run_sidecar(args, env).await
}

fn main() {
    tauri::Builder::default()
        .invoke_handler(tauri::generate_handler![
            generate_mandelbrot,
            generate_mandelbrot_hp,
            get_project_dir,
            get_available_modes,
            get_remote_settings,
            save_remote_settings,
        ])
        .run(tauri::generate_context!())
        .expect("error while running tauri application");
}
