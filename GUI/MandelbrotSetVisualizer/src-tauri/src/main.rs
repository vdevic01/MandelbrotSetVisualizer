// Prevents additional console window on Windows in release, DO NOT REMOVE!!
#![cfg_attr(not(debug_assertions), windows_subsystem = "windows")]

use tokio::process::Command;

#[tauri::command]
fn get_project_dir() -> String {
    let mut path = std::env::current_exe().unwrap();

    for _ in 0..4 {
        path = path.parent().unwrap().to_path_buf();
    }
    
    return path.display().to_string();
}

#[tauri::command]
async fn generate_mandelbrot(mode: String, re_start: f64, re_end: f64, im_start: f64, im_end: f64, max_iter: i32, palette_length: i32, palette_id: i32, samples: i32) -> String{
    let output = Command::new("./mandelbrot_visualizer.exe")
        .arg(&mode)
        .arg("./../generated-files/mandelbrot_set.png")
        .arg(max_iter.to_string()).arg(palette_length.to_string())
        .arg(palette_id.to_string())
        .arg(samples.to_string())
        .arg("0")
        .arg(re_start.to_string()).arg(re_end.to_string()).arg(im_start.to_string()).arg(im_end.to_string())
        .output()
        .await
        .expect("Failed to execute process");

    println!("\n{}", String::from_utf8_lossy(&output.stdout));
    println!("{}", String::from_utf8_lossy(&output.stderr));
    println!("{}\n", output.status);

    output.status.to_string()
}

#[tauri::command]
async fn generate_mandelbrot_hp(
    mode: String,
    re_start: [u32; 4], re_end:[u32; 4],
    im_start: [u32; 4], im_end: [u32; 4],
    max_iter: i32, palette_length: i32,
    palette_id: i32, samples: i32) -> String{
    let output = Command::new("./mandelbrot_visualizer.exe")
        .arg(&mode)
        .arg("./../generated-files/mandelbrot_set.png")
        .arg(max_iter.to_string()).arg(palette_length.to_string())
        .arg(palette_id.to_string())
        .arg(samples.to_string())
        .arg("1")
        .args(re_start.iter().map(|x| x.to_string()))
        .args(re_end.iter().map(|x| x.to_string()))
        .args(im_start.iter().map(|x| x.to_string()))
        .args(im_end.iter().map(|x| x.to_string()))
        .output()
        .await
        .expect("Failed to execute process");

    println!("\n{}", String::from_utf8_lossy(&output.stdout));
    println!("{}", String::from_utf8_lossy(&output.stderr));
    println!("{}\n", output.status);
    output.status.to_string()
}

fn main() {
    tauri::Builder::default()
        .invoke_handler(tauri::generate_handler![generate_mandelbrot, generate_mandelbrot_hp, get_project_dir])
        .run(tauri::generate_context!())
        .expect("error while running tauri application");
}
