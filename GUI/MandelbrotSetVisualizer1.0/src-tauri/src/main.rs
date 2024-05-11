// Prevents additional console window on Windows in release, DO NOT REMOVE!!
#![cfg_attr(not(debug_assertions), windows_subsystem = "windows")]

use std::process::Command;
use std::env;
use std::io::Read;
use std::fs::File;

#[tauri::command]
fn generate_mandelbrot(re_start: f64, re_end: f64, im_start: f64, im_end:f64) -> String{
    let output = Command::new("./MandelbrotSetParallelOpenCL.exe")
        .arg(re_start.to_string()).arg(re_end.to_string()).arg(im_start.to_string()).arg(im_end.to_string())
        .output()
        .expect("Failed to execute process");

    let file_path = "./mandelbrot_set.tiff";
    let mut file = match File::open(file_path){
        Ok(file) => file,
        Err(e) => {
            eprintln!("Error opening file: {}", e);
            return format!("Error opening file: {}", e);
        }
    };
    let mut buffer = Vec::new();
    if let Err(e) = file.read_to_end(&mut buffer){
        eprintln!("Error reading file: {}", e);
        return format!("Error reading file: {}", e);
    }

    let base64_encoded = base64::encode(&buffer);
    format!("{}", base64_encoded)
    
    // if output.status == 0 {
        
    // }else{
    //     format!("error")
    // }

}

fn main() {
    tauri::Builder::default()
        .invoke_handler(tauri::generate_handler![generate_mandelbrot])
        .run(tauri::generate_context!())
        .expect("error while running tauri application");
}
