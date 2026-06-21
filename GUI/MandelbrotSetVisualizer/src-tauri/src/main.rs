// Prevents additional console window on Windows in release, DO NOT REMOVE!!
#![cfg_attr(not(debug_assertions), windows_subsystem = "windows")]

use tauri::api::process::{Command, CommandEvent};

#[tauri::command]
fn get_project_dir() -> String {
    let mut path = std::env::current_exe().unwrap();

    for _ in 0..4 {
        path = path.parent().unwrap().to_path_buf();
    }

    return path.display().to_string();
}

#[tauri::command]
async fn get_available_modes() -> Vec<String> {
    let (mut rx, _child) = Command::new_sidecar("mandelbrot_visualizer")
        .expect("failed to find mandelbrot_visualizer sidecar")
        .args(["--list-modes"])
        .spawn()
        .expect("failed to spawn mandelbrot_visualizer");

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
    modes
}

async fn run_sidecar(args: Vec<String>) -> String {
    let (mut rx, _child) = Command::new_sidecar("mandelbrot_visualizer")
        .expect("failed to find mandelbrot_visualizer sidecar")
        .args(args)
        .spawn()
        .expect("failed to spawn mandelbrot_visualizer");

    while let Some(event) = rx.recv().await {
        match event {
            CommandEvent::Stdout(line)  => print!("{}", line),
            CommandEvent::Stderr(line)  => eprint!("{}", line),
            CommandEvent::Terminated(p) => {
                return p.code.map(|c| c.to_string()).unwrap_or_default();
            }
            _ => {}
        }
    }
    String::new()
}

#[tauri::command]
async fn generate_mandelbrot(mode: String, re_start: f64, re_end: f64, im_start: f64, im_end: f64, max_iter: i32, palette_length: i32, palette_id: i32, samples: i32) -> String {
    let args = vec![
        mode,
        "./../generated-files/mandelbrot_set.png".to_string(),
        max_iter.to_string(), palette_length.to_string(),
        palette_id.to_string(), samples.to_string(),
        "0".to_string(),
        re_start.to_string(), re_end.to_string(),
        im_start.to_string(), im_end.to_string(),
    ];
    run_sidecar(args).await
}

#[tauri::command]
async fn generate_mandelbrot_hp(
    mode: String,
    re_start: [u32; 4], re_end: [u32; 4],
    im_start: [u32; 4], im_end: [u32; 4],
    max_iter: i32, palette_length: i32,
    palette_id: i32, samples: i32) -> String {
    let mut args = vec![
        mode,
        "./../generated-files/mandelbrot_set.png".to_string(),
        max_iter.to_string(), palette_length.to_string(),
        palette_id.to_string(), samples.to_string(),
        "1".to_string(),
    ];
    args.extend(re_start.iter().map(|x| x.to_string()));
    args.extend(re_end.iter().map(|x| x.to_string()));
    args.extend(im_start.iter().map(|x| x.to_string()));
    args.extend(im_end.iter().map(|x| x.to_string()));
    run_sidecar(args).await
}

fn main() {
    tauri::Builder::default()
        .invoke_handler(tauri::generate_handler![generate_mandelbrot, generate_mandelbrot_hp, get_project_dir, get_available_modes])
        .run(tauri::generate_context!())
        .expect("error while running tauri application");
}
