// const { invoke } = window.__TAURI__.tauri;

// let greetInputEl;
// let greetMsgEl;

// async function greet() {
//   // Learn more about Tauri commands at https://tauri.app/v1/guides/features/command
//   greetMsgEl.textContent = await invoke("greet", { name: greetInputEl.value });
// }

// window.addEventListener("DOMContentLoaded", () => {
//   greetInputEl = document.querySelector("#greet-input");
//   greetMsgEl = document.querySelector("#greet-msg");
//   document.querySelector("#greet-form").addEventListener("submit", (e) => {
//     e.preventDefault();
//     greet();
//   });
// });
const { invoke } = window.__TAURI__.tauri;
const {appDataDir, join} = window.__TAURI__.tauri;
const { convertFileSrc } = window.__TAURI__.tauri;
// import { appDataDir, join } from '@tauri-apps/api/path';
// import { convertFileSrc } from '@tauri-apps/api/tauri';


const appDataDirPath = await appDataDir();
const filePath = await join(appDataDirPath, 'assets/video.mp4');
const assetUrl = convertFileSrc(filePath);

const canvas = document.getElementById("main-canvas");


async function generateMandelbrot(){
  const data = await invoke("generate_mandelbrot", {reStart:RE_START, reEnd:RE_END, imStart:IM_START, imEnd:IM_END});
  img = loadImage('data:image/png;base64, iVBORw0KGgoAAAANSUhEUgAAAAUAAAAFCAYAAACNbyblAAAAHElEQVQI12P4//8/w38GIAXDIBKE0DHxgljNBAAO9TXL0Y4OHwAAAABJRU5ErkJggg==');
} 