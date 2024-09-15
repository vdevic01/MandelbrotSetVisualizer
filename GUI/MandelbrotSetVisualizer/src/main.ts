import { invoke } from "@tauri-apps/api/tauri";
import P5 from "p5";
import Decimal from "decimal.js";

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
  private highPrecission: boolean = false;
  private boxSidesRatio: number[];
  private pressX: number = -1;
  private pressY: number = -1;
  private releaseX: number = -1;
  private releaseY: number = -1;
  private p5Client: P5;
  private img?: P5.Image;
  private paletteLength: number = 250;
  private maxIter: number = 700;
  private paletteId: number = 0;
  private static imgUrl: string = "https://asset.localhost/D%3A%2FFakultet%2F8.%20semestar%2FDiplomski%20rad%2FMandelbrotSetVisualizer%2FGUI%2FMandelbrotSetVisualizer%2Fgenerated-files%2Fmandelbrot_set.png";

  public constructor(boundary: Boundary, boxSidesRatio: number[], p5: P5){
    this.lowPrecissionBoundary = boundary;
    this.boxSidesRatio = boxSidesRatio;
    this.p5Client = p5;
    this.startingBoundary = {... boundary};
    this.generateMandelbrot();
  }
  public setPaletteLength(paletteLength: number){
    this.paletteLength = paletteLength;
    if(this.highPrecission){
      this.generateMandelbrotHighPrecission();
    }else{
      this.generateMandelbrot();
    }
  }
  public getPaletteLength(): number{
    return this.paletteLength;
  }
  public getMaxIter(): number{
    return this.maxIter;
  }
  public setMaxIter(maxIter: number){
    this.maxIter = maxIter;
    if(this.highPrecission){
      this.generateMandelbrotHighPrecission();
    }else{
      this.generateMandelbrot();
    }
  }
  public setPaletteId(id: number){
    this.paletteId = id;
    if(this.highPrecission){
      this.generateMandelbrotHighPrecission();
    }else{
      this.generateMandelbrot();
    }
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

  public fixedToDecimal(fpNum: number[]): Decimal{

    let isNegative = false;
    if((fpNum[0] & 0x80000000) != 0){
      isNegative = true;
      let bigNums = fpNum.map(num => (~BigInt(num)) & 0x00000000FFFFFFFFn);
      bigNums[3] += 1n;
      for(let i = bigNums.length - 1; i>= 1; i--){
        let msb = bigNums[i] >> BigInt(32);
        let lsb = bigNums[i] & 0xFFFFFFFFn;
        bigNums[i - 1] += msb;
        fpNum[i] = Number(lsb);
      }
      fpNum[0] = Number(bigNums[0] & 0xFFFFFFFFn);
    }
    const wholePart = fpNum[0]
    const fractionalParts = fpNum.slice(1)

    let fractionalPart = '';
    fractionalParts.forEach(part => {
      fractionalPart += part.toString(2).padStart(32, '0')
    });

    let fractionalDecimal = new Decimal(0);
    for(let i = 0; i < fractionalPart.length; i++){
      if(fractionalPart[i] === '1'){
        fractionalDecimal = fractionalDecimal.plus(new Decimal(1).dividedBy(new Decimal(2).pow(i + 1)));
      }
    }

    let result = new Decimal(wholePart).plus(fractionalDecimal);
    if(isNegative){
      result = result.mul(-1);
    }

    return result;
  }

  public decimalToFixedPoint(decimal: Decimal): any {
    let isNegative = false;
    if(decimal.isNegative()){
      decimal = decimal.negated();
      isNegative = true;
    }
    const integerPart = decimal.trunc();
    const fractionalPart = decimal.minus(integerPart);
    const scaleFactor = new Decimal(2).pow(32);
    const fractionHigh = fractionalPart.mul(scaleFactor).trunc();
    const remainingFraction = fractionalPart.mul(scaleFactor).minus(fractionHigh);
    const fractionLow1 = remainingFraction.mul(scaleFactor).trunc();
    const fractionLow2 = remainingFraction.mul(scaleFactor).minus(fractionLow1).mul(scaleFactor).trunc();
    
    let nums = [integerPart.toNumber(), fractionHigh.toNumber(), fractionLow1.toNumber(), fractionLow2.toNumber()];
    if(isNegative){
      let bigNums = nums.map(num => (~BigInt(num)) & 0x00000000FFFFFFFFn);
      bigNums[3] += 1n;
      for(let i = bigNums.length - 1; i>= 1; i--){
        let msb = bigNums[i] >> BigInt(32);
        let lsb = bigNums[i] & 0xFFFFFFFFn;
        bigNums[i - 1] += msb;
        nums[i] = Number(lsb);
      }
      nums[0] = Number(bigNums[0] & 0xFFFFFFFFn);
    }

    return nums;
  }
  
  private transformMouseCoordinates(): Boundary{
    let relX;
    let relY;

    let width = this.releaseX - this.pressX;
    let height = this.releaseY - this.pressY;
    console.log("mouseX, mouseY:" + [this.releaseX, this.releaseY]);
    if(Math.abs(width) * this.boxSidesRatio[0] > Math.abs(height) * this.boxSidesRatio[1]){
      relX = this.releaseX;
      relY = this.pressY + Math.sign(height) * Math.abs(width) * 1.0 / this.boxSidesRatio[1] * this.boxSidesRatio[0];
    }else{
      relX = this.pressX + Math.sign(width) * Math.abs(height) * 1.0 / this.boxSidesRatio[0] * this.boxSidesRatio[1];
      relY = this.releaseY;
      console.log([relX, relY]);
    }

    let reStart = Math.min(this.pressX, relX);
    let reEnd = Math.max(this.pressX, relX);
    let imStart = Math.max(this.pressY, relY);
    let imEnd = Math.min(this.pressY, relY);

    return {
      reStart: reStart,
      reEnd: reEnd,
      imStart: imStart,
      imEnd: imEnd
    }
  }

  private highPrecissionMapping(n: Decimal, inMin: Decimal, inMax:Decimal, outMin:Decimal, outMax:Decimal): Decimal{
    const numerator = Decimal.sub(n, inMin).mul(Decimal.sub(outMax, outMin));
    const denominator = Decimal.sub(inMax, inMin);
    const fraction = Decimal.div(numerator, denominator);
    return Decimal.sum(fraction, outMin);
  }

  private updateBoundaryHighPrecission(){
    const {reStart, reEnd, imStart, imEnd} = this.transformMouseCoordinates();
    let reStartDec = new Decimal(reStart);
    let reEndDec = new Decimal(reEnd);
    let imStartDec = new Decimal(imStart);
    let imEndDec = new Decimal(imEnd);
    let zeroDec = new Decimal(0);
    let widthDec = new Decimal(this.p5Client.width);
    let heightDec = new Decimal(this.p5Client.height);
    
    reStartDec = this.highPrecissionMapping(reStartDec, zeroDec, widthDec, this.highPrecissionBoundary!.reStart, this.highPrecissionBoundary!.reEnd);
    reEndDec = this.highPrecissionMapping(reEndDec, zeroDec, widthDec, this.highPrecissionBoundary!.reStart, this.highPrecissionBoundary!.reEnd);
    imStartDec = this.highPrecissionMapping(imStartDec, heightDec, zeroDec, this.highPrecissionBoundary!.imStart, this.highPrecissionBoundary!.imEnd);
    imEndDec = this.highPrecissionMapping(imEndDec, heightDec, zeroDec, this.highPrecissionBoundary!.imStart, this.highPrecissionBoundary!.imEnd);
    this.highPrecissionBoundary = {
      reStart: reStartDec,
      reEnd: reEndDec,
      imStart: imStartDec,
      imEnd: imEndDec
    };
    this.generateMandelbrotHighPrecission();
  }

  private updateBoundaryLowPrecission(){
    let {reStart, reEnd, imStart, imEnd} = this.transformMouseCoordinates();

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
    this.generateMandelbrot();
    if(this.lowPrecissionBoundary.reEnd - this.lowPrecissionBoundary.reStart < 0.0000000000006){
      console.log("!!!Precission limit reached!!!")
      this.highPrecission = true;
      this.highPrecissionBoundary = {
        reStart: new Decimal(this.lowPrecissionBoundary.reStart),
        reEnd: new Decimal(this.lowPrecissionBoundary.reEnd),
        imStart: new Decimal(this.lowPrecissionBoundary.imStart),
        imEnd: new Decimal(this.lowPrecissionBoundary.imEnd)
      }
    }
  }

  public updateBoundary(releaseX: number, releaseY: number){
    this.releaseX = releaseX;
    this.releaseY = releaseY;
    if(this.highPrecission){
      this.updateBoundaryHighPrecission();
    }else{
      this.updateBoundaryLowPrecission();
    }
  }

  private async generateMandelbrot(){
    const args = {
      ...this.lowPrecissionBoundary,
      maxIter: this.maxIter,
      paletteLength: this.paletteLength,
      paletteId: this.paletteId
    };
    const status = await invoke("generate_mandelbrot", args);
    this.img = this.p5Client.loadImage(BoundaryManager.imgUrl);
    console.log("status:" + status);
  }
  private async generateMandelbrotHighPrecission(){
    const args = {
      reStart: this.highPrecissionBoundary?.reStart.toString(),
      reEnd: this.highPrecissionBoundary?.reEnd.toString(),
      imStart: this.highPrecissionBoundary?.imStart.toString(),
      imEnd: this.highPrecissionBoundary?.imEnd.toString(),
      maxIter: this.maxIter,
      paletteLength: this.paletteLength,
      paletteId: this.paletteId
    };
    const status = await invoke("generate_mandelbrot_hp", args);
    this.img = this.p5Client.loadImage(BoundaryManager.imgUrl);
    console.log("status:" + status);
  }

  public reset(){
    this.maxIter = 700;
    this.paletteLength = 250;
    this.highPrecission = false;
    this.lowPrecissionBoundary = {...this.startingBoundary};
    this.generateMandelbrot();
  }
}


window.addEventListener("DOMContentLoaded", () => {
  let pressX = -1;
  let pressY = -1;
  const boxRatio = [2, 3];
  let isPressed = false;
  let boundaryManager: BoundaryManager;

  const sketch = (p5: P5) => {
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
  const inputPaletteLength: HTMLInputElement = document.getElementById("input-palette-length") as HTMLInputElement;
  const inputMaxIter: HTMLInputElement = document.getElementById("input-max-iter") as HTMLInputElement;
  const buttonPaletteLengthCancel: HTMLButtonElement = document.getElementById("button-palette-length-cancel") as HTMLButtonElement;
  const buttonMaxIterCancel: HTMLButtonElement = document.getElementById("button-max-iter-cancel") as HTMLButtonElement;
  document.getElementById("button-reset")?.addEventListener("click", () => {
    boundaryManager.reset();
    inputMaxIter.value = "700";
    inputPaletteLength.value = "250";
  });
  document.getElementById("button-palette-length")?.addEventListener("click", () => {
    const paletteLength: number = parseInt(inputPaletteLength.value);
    buttonPaletteLengthCancel.disabled = true;
    boundaryManager.setPaletteLength(paletteLength);
  });
  document.getElementById("button-max-iter")?.addEventListener("click", () => {
    const maxIter: number = parseInt(inputMaxIter.value);
    buttonMaxIterCancel.disabled = true;
    boundaryManager.setMaxIter(maxIter);
  });
  buttonMaxIterCancel.addEventListener("click", () => {
    inputMaxIter.value = boundaryManager.getMaxIter().toString();
    buttonMaxIterCancel.disabled = true;
  });
  buttonPaletteLengthCancel.addEventListener("click", () => {
    inputPaletteLength.value = boundaryManager.getPaletteLength().toString();
    buttonPaletteLengthCancel.disabled = true;
  });
  document.getElementById("select-color-palette")?.addEventListener("change", () => {
    const paletteId: number = parseInt((document.getElementById("select-color-palette") as HTMLInputElement)?.value);
    boundaryManager.setPaletteId(paletteId);
  });
  inputPaletteLength.addEventListener("input", () => {
    buttonPaletteLengthCancel.disabled = false;
  });
  inputMaxIter.addEventListener("input", () => {
    buttonMaxIterCancel.disabled = false;
  });
});

