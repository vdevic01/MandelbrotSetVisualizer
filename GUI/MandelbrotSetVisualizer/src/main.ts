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
  let boxX = -1;
  let boxY = -1;
  const boxRatio = [9, 16];

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
        let width = p5.mouseX - boxX;
        let height = p5.mouseY - boxY;
        if(p5.abs(width) * boxRatio[0] > p5.abs(height) * boxRatio[1]){
          height = Math.sign(height) * p5.abs(width) * 1.0 / boxRatio[1] * boxRatio[0];
        }else{
          width = Math.sign(width) * p5.abs(height) * 1.0 / boxRatio[0] * boxRatio[1];
        }
        
        p5.noFill();
        p5.stroke(255);
        p5.strokeWeight(4);
        p5.rect(boxX, boxY, width, height);
        p5.stroke(0);
        p5.strokeWeight(2);
        p5.rect(boxX, boxY, width, height);
      }
    }

    p5.mousePressed = () => {
      boxX = p5.mouseX;
      boxY = p5.mouseY;
      isPressed = true;
    }

    p5.mouseReleased = () => {
      isPressed = false;

      let width = p5.mouseX - boxX;
      let height = p5.mouseY - boxY;
      let reStart = Math.min(boxX + width, boxX);
      let reEnd = Math.max(boxX + width, boxX);
      let imStart = Math.max(boxY + height, boxY);
      let imEnd = Math.min(boxY + height, boxY);
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
      generateMandelbrot(boundary);
    }

    generateMandelbrot(boundary);
  }
  
  new P5(sketch);
});

