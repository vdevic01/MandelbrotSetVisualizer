import { invoke } from "@tauri-apps/api/tauri";
import P5 from "p5";
import Decimal from "decimal.js";

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

type HighPrecissionBoundary = {
  reStart: Decimal,
  reEnd: Decimal,
  imStart: Decimal,
  imEnd: Decimal
}

class BoundaryManager{
  private startingBoundary: Boundary;
  private lowPrecissionBoundary: Boundary;
  private highPrecissionBoundary?: HighPrecissionBoundary;
  private boxSidesRatio: number[];
  private pressX: number = -1;
  private pressY: number = -1;
  private p5Client: P5;
  private img?: P5.Image;
  private static imgUrl: string = "https://asset.localhost/D%3A%2FFakultet%2F8.%20semestar%2FDiplomski%20rad%2FMandelbrotSetVisualizer%2FGUI%2FMandelbrotSetVisualizer%2Fgenerated-files%2Fmandelbrot_set.png";

  public constructor(boundary: Boundary, boxSidesRatio: number[], p5: P5){
    this.lowPrecissionBoundary = boundary;
    this.boxSidesRatio = boxSidesRatio;
    this.p5Client = p5;
    this.startingBoundary = {... boundary};
    this.generateMandelbrot();
  }

  public getImage(): P5.Image | undefined{
    return this.img;
  }
  public getPressX(): number{
    return this.pressX;
  }
  public getPressY(): number{
    return this.pressY;
  }
  public setPressCoordinates(pressX: number, pressY: number){
    this.pressX = pressX;
    this.pressY = pressY;
  }

  public updateBoundary(releaseX: number, releaseY: number){
    let relX;
    let relY;

    let width = releaseX - this.pressX;
    let height = releaseY - this.pressY;
    console.log("mouseX, mouseY:" + [releaseX, releaseY]);
    if(Math.abs(width) * this.boxSidesRatio[0] > Math.abs(height) * this.boxSidesRatio[1]){
      relX = releaseX;
      relY = this.pressY + Math.sign(height) * Math.abs(width) * 1.0 / this.boxSidesRatio[1] * this.boxSidesRatio[0];
    }else{
      relX = this.pressX + Math.sign(width) * Math.abs(height) * 1.0 / this.boxSidesRatio[0] * this.boxSidesRatio[1];
      relY = releaseY;
      console.log([relX, relY]);
    }

    let reStart = Math.min(this.pressX, relX);
    let reEnd = Math.max(this.pressX, relX);
    let imStart = Math.max(this.pressY, relY);
    let imEnd = Math.min(this.pressY, relY);
    const k1: number = (reEnd - reStart) / (imEnd - imStart);
    console.log("Boundary side ratio (k1):" + k1);

    reStart = this.p5Client.map(reStart, 0, this.p5Client.width, this.lowPrecissionBoundary.reStart, this.lowPrecissionBoundary.reEnd);
    reEnd = this.p5Client.map(reEnd, 0, this.p5Client.width, this.lowPrecissionBoundary.reStart, this.lowPrecissionBoundary.reEnd);
    imStart = this.p5Client.map(imStart, this.p5Client.height, 0, this.lowPrecissionBoundary.imStart, this.lowPrecissionBoundary.imEnd);
    imEnd = this.p5Client.map(imEnd, this.p5Client.height, 0, this.lowPrecissionBoundary.imStart, this.lowPrecissionBoundary.imEnd);

    this.lowPrecissionBoundary = {
      reStart: reStart,
      reEnd: reEnd,
      imStart: imStart,
      imEnd: imEnd
    };
    console.log(this.lowPrecissionBoundary);
    const k2: number = (this.lowPrecissionBoundary.reEnd - this.lowPrecissionBoundary.reStart) / (this.lowPrecissionBoundary.imEnd - this.lowPrecissionBoundary.imStart);
    console.log("Boundary side ratio (k2):" + k2);
    this.generateMandelbrot();
  }

  private async generateMandelbrot(){
    const status = await invoke("generate_mandelbrot", this.lowPrecissionBoundary);
    this.img = this.p5Client.loadImage(BoundaryManager.imgUrl);
    console.log("All good");
    console.log("status:" + status);
  }

  public reset(){
    this.lowPrecissionBoundary = {...this.startingBoundary};
    this.generateMandelbrot();
  }
}


window.addEventListener("DOMContentLoaded", () => {
  let pressX = -1;
  let pressY = -1;
  const boxRatio = [2, 3];
  let p5Client: P5;
  let isPressed = false;
  let boundaryManager: BoundaryManager;

  const sketch = (p5: P5) => {
    p5Client = p5;
    boundaryManager = new BoundaryManager({
      reStart: -2.0,
      reEnd: 1.0,
      imStart: -1.0,
      imEnd: 1.0},
      boxRatio,
      p5
    );
    p5.setup = () => {
      const canvas = p5.createCanvas(900, 600);
      canvas.parent("main-canvas");
    }

    p5.draw = () => {
      const img = boundaryManager.getImage();
      if(img){
        p5.image(img, 0, 0);
      }else{
        p5.background(71, 71, 78);
      }
      if(isPressed){
        let width = p5.mouseX - boundaryManager.getPressX();
        let height = p5.mouseY - boundaryManager.getPressY();
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
      if(p5.mouseX < 0 || p5.mouseX > p5.width || p5.mouseY < 0 || p5.mouseY > p5.height){
        return;
      }
      if(p5.mouseButton == p5.LEFT){
        pressX = p5.mouseX;
        pressY = p5.mouseY;
        boundaryManager.setPressCoordinates(pressX, pressY);
        isPressed = true;
      }
    }

    p5.mouseReleased = () => {
      if(!isPressed){
        return;
      }
      isPressed = false;

      boundaryManager.updateBoundary(p5.mouseX, p5.mouseY);
    }
  }
  
  new P5(sketch);

  document.getElementById("button-reset")?.addEventListener("click", () => {
    boundaryManager.reset();
  });
});

