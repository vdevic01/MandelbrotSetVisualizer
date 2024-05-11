// Prevents additional console window on Windows in release, DO NOT REMOVE!!
#![cfg_attr(not(debug_assertions), windows_subsystem = "windows")]

use std::process::Command;

#[tauri::command]
fn generate_mandelbrot(re_start: f64, re_end: f64, im_start: f64, im_end:f64) -> String{
    let output = Command::new("./MandelbrotSetParallelOpenCL.exe")
        .arg(re_start.to_string()).arg(re_end.to_string()).arg(im_start.to_string()).arg(im_end.to_string())
        .arg("./../generated-files/mandelbrot_set.png")
        .output()
        .expect("Failed to execute process");

    output.status.to_string()
}

fn main() {
    tauri::Builder::default()
        .invoke_handler(tauri::generate_handler![generate_mandelbrot])
        .run(tauri::generate_context!())
        .expect("error while running tauri application");
}
