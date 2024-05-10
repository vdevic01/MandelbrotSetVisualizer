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

const canvas = document.getElementById("main-canvas");

let boxX = -1;
let boxY = -1;
const boxRatio = [9, 16];

let isPressed = false;

function setup() {
  createCanvas(900, 600, canvas); // create a canvas
  background(255); // set background color
}

function draw(){
  background(255);
  if(isPressed){
    let width = mouseX - boxX;
    let height = mouseY - boxY;
    if(abs(width) * boxRatio[0] > abs(height) * boxRatio[1]){
      height = Math.sign(height) * abs(width) * 1.0 / boxRatio[1] * boxRatio[0];
    }else{
      width = Math.sign(width) * abs(height) * 1.0 / boxRatio[0] * boxRatio[1];
    }
    
    noFill();
    stroke(0);
    strokeWeight(2);
    rect(boxX, boxY, width, height);
  }
}

function mousePressed(){
  boxX = mouseX;
  boxY = mouseY;
  isPressed = true;
}

function mouseReleased(){
  isPressed = false;
}