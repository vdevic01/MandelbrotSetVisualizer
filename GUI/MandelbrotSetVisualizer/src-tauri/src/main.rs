// Prevents additional console window on Windows in release, DO NOT REMOVE!!
#![cfg_attr(not(debug_assertions), windows_subsystem = "windows")]

use tokio::process::Command;

#[tauri::command]
async fn generate_mandelbrot(re_start: f64, re_end: f64, im_start: f64, im_end: f64, max_iter: i32, palette_length: i32, palette_id: i32) -> String{
    let output = Command::new("./MandelbrotSetParallelOpenCL.exe")
        .arg("0")
        .arg(re_start.to_string()).arg(re_end.to_string()).arg(im_start.to_string()).arg(im_end.to_string())
        .arg("./../generated-files/mandelbrot_set.png")
        .arg(max_iter.to_string()).arg(palette_length.to_string())
        .arg(palette_id.to_string())
        .output()
        .await
        .expect("Failed to execute process");

    output.status.to_string()
}


#[tauri::command]
async fn generate_mandelbrot_hp(re_start: String, re_end: String, im_start: String, im_end: String, max_iter: i32, palette_length: i32, palette_id: i32) -> String{
    let output = Command::new("./MandelbrotSetParallelOpenCL.exe")
        .arg("1")
        .arg(&re_start).arg(&re_end).arg(&im_start).arg(&im_end)
        .arg("./../generated-files/mandelbrot_set.png")
        .arg(max_iter.to_string()).arg(palette_length.to_string())
        .arg(palette_id.to_string())
        .output()
        .await
        .expect("Failed to execute process");

    output.status.to_string()
}

fn main() {
    tauri::Builder::default()
        .invoke_handler(tauri::generate_handler![generate_mandelbrot, generate_mandelbrot_hp])
        .run(tauri::generate_context!())
        .expect("error while running tauri application");
}
