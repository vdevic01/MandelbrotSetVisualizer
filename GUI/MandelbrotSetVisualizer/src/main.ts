import { invoke } from "@tauri-apps/api/tauri";
import P5 from "p5";

// async function greet() {
//   if (greetMsgEl && greetInputEl) {
//     // Learn more about Tauri commands at https://tauri.app/v1/guides/features/command
//     greetMsgEl.textContent = await invoke("greet", {
//       name: greetInputEl.value,
//     });
//   }
// }

type Boundary = {
  reStart: number,
  reEnd: number,
  imStart: number,
  imEnd: number
}

let p5Client: P5;
let img: any;
const imgUrl: string = "https://asset.localhost/D%3A%2FFakultet%2F8.%20semestar%2FDiplomski%20rad%2FMandelbrotSetVisualizer%2FGUI%2FMandelbrotSetVisualizer%2Fgenerated-files%2Fmandelbrot_set.png";

async function generateMandelbrot(boundary: Boundary){
  const status = await invoke("generate_mandelbrot", boundary);
  img = p5Client.loadImage(imgUrl);
  console.log("All good");
  console.log("status:" + status);
}

let boundary: Boundary = {
  reStart: -2.0,
  reEnd: 1.0,
  imStart: -1.0,
  imEnd: 1.0
}

window.addEventListener("DOMContentLoaded", () => {
  let pressX = -1;
  let pressY = -1;
  const boxRatio = [2, 3];

  let isPressed = false;


  const sketch = (p5: P5) => {
    p5Client = p5;
    p5.setup = () => {
      const canvas = p5.createCanvas(900, 600);
      canvas.parent("main-canvas");
    }

    p5.draw = () => {
      if(img){
        p5.image(img, 0, 0);
      }else{
        p5.background(255);
      }
      if(isPressed){
        let width = p5.mouseX - pressX;
        let height = p5.mouseY - pressY;
        if(p5.abs(width) * boxRatio[0] > p5.abs(height) * boxRatio[1]){
          height = Math.sign(height) * p5.abs(width) * 1.0 / boxRatio[1] * boxRatio[0];
        }else{
          width = Math.sign(width) * p5.abs(height) * 1.0 / boxRatio[0] * boxRatio[1];
        }
        
        p5.noFill();
        p5.stroke(255);
        p5.strokeWeight(4);
        p5.rect(pressX, pressY, width, height);
        p5.stroke(0);
        p5.strokeWeight(2);
        p5.rect(pressX, pressY, width, height);
      }
    }

    p5.mousePressed = () => {
      if(p5.mouseButton == p5.LEFT){
        pressX = p5.mouseX;
        pressY = p5.mouseY;
        isPressed = true;
      }
    }

    p5.mouseReleased = () => {
      if(!isPressed){
        return;
      }
      isPressed = false;

      let relX;
      let relY;

      let width = p5.mouseX - pressX;
      let height = p5.mouseY - pressY;
      console.log("mouseX, mouseY:" + [p5.mouseX, p5.mouseY]);
      if(p5.abs(width) * boxRatio[0] > p5.abs(height) * boxRatio[1]){
        relX = p5.mouseX;
        relY = pressY + Math.sign(height) * p5.abs(width) * 1.0 / boxRatio[1] * boxRatio[0];
      }else{
        relX = pressX + Math.sign(width) * p5.abs(height) * 1.0 / boxRatio[0] * boxRatio[1];
        relY = p5.mouseY;
        console.log([relX, relY]);
      }

      let reStart = Math.min(pressX, relX);
      let reEnd = Math.max(pressX, relX);
      let imStart = Math.max(pressY, relY);
      let imEnd = Math.min(pressY, relY);
      const k1: number = (reEnd - reStart) / (imEnd - imStart);
      console.log("Boundary side ratio (k1):" + k1);

      reStart = p5.map(reStart, 0, p5.width, boundary.reStart, boundary.reEnd);
      reEnd = p5.map(reEnd, 0, p5.width, boundary.reStart, boundary.reEnd);
      imStart = p5.map(imStart, p5.height, 0, boundary.imStart, boundary.imEnd);
      imEnd = p5.map(imEnd, p5.height, 0, boundary.imStart, boundary.imEnd);

      boundary = {
        reStart: reStart,
        reEnd: reEnd,
        imStart: imStart,
        imEnd: imEnd
      };
      console.log(boundary);
      const k2: number = (boundary.reEnd - boundary.reStart) / (boundary.imEnd - boundary.imStart);
      console.log("Boundary side ratio (k2):" + k2);
      generateMandelbrot(boundary);
    }

    generateMandelbrot(boundary);
  }
  
  new P5(sketch);
});

