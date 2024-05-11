const { invoke } = window.__TAURI__.tauri;
const {appDataDir, join} = window.__TAURI__.tauri;
const { convertFileSrc } = window.__TAURI__.tauri;

let RE_START = -2.0;
let RE_END = 1.0;
let IM_START = -1.0;
let IM_END = 1.0;

const canvas = document.getElementById("main-canvas");

let boxX = -1;
let boxY = -1;
const boxRatio = [9, 16];
let img;

let isPressed = false;

function setup() {
  createCanvas(900, 600, canvas); // create a canvas
  background(255); // set background color
}

function draw(){
  if(img){
    image(img, 0, 0);
  }else{
    background(255);
  }
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