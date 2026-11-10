import { invoke } from "@tauri-apps/api/tauri";
import P5 from "p5";
import Decimal from "decimal.js";
import { join } from '@tauri-apps/api/path';
import { convertFileSrc } from '@tauri-apps/api/tauri';
import {
  DOUBLE_PRECISION_MIN_WIDTH,
  chooseFractionWords,
  decimalToFixedWords,
  fractionDigits
} from "./precision";

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

// ---------------------------------------------------------------------------------------------------------
// Status bar above the canvas: the precision of the last render, the scale of the view, how long the render took,
// what the remote worker answered, and a message line for warnings and errors.
// ---------------------------------------------------------------------------------------------------------
type StatusKind = "info" | "warning" | "error";

function setField(id: string, text: string, tooltip: string = ""){
  const element = document.getElementById(id);
  if (!element) return;
  element.textContent = text;
  element.title = tooltip || text;
}

// Shows the order of magnitude of the width of the visible region, e.g. 10^-13.2.
function setScale(log10Width: number){
  const element = document.getElementById("status-scale");
  if (!element) return;
  const exponent = log10Width.toFixed(1);
  element.innerHTML = `10<sup>${exponent}</sup>`;
  element.title = `The visible region of the complex plane is about 10^${exponent} wide (the whole set is about 10^0.5)`;
}

// One line in the bar; the full text (an error can be several lines) is in the tooltip.
function setMessage(message: string, kind: StatusKind = "info"){
  const element = document.getElementById("status-message");
  if (!element) return;
  element.textContent = message.split("\n")[0];
  element.title = message;
  element.classList.toggle("warning", kind === "warning");
  element.classList.toggle("error", kind === "error");
}

function formatDuration(milliseconds: number): string {
  return milliseconds < 1000 ? `${Math.round(milliseconds)} ms` : `${(milliseconds / 1000).toFixed(2)} s`;
}

type RendererOutput = {
  stages: string,    // "Pixel mapping 64 ms, Calculating escape iterations 3.2 s, ..." (for a tooltip)
  remote?: string,   // what the remote worker answered, when remote rendering was used
  remoteDetails?: string
};

// Reads what the renderer printed: the time of each stage and, for remote rendering, the worker's answer.
function parseRendererOutput(output: string): RendererOutput {
  const stages: string[] = [];
  let httpStatus: string | undefined;
  let roundTripMs: number | undefined;
  let samplingMs: number | undefined;
  let kernelMs: number | undefined;
  for (const line of output.split(/\r?\n/)) {
    let match = line.match(/^Remote: HTTP (\d+) after (\d+) ms/);
    if (match) {
      httpStatus = match[1];
      roundTripMs = parseInt(match[2]);
      continue;
    }
    match = line.match(/^Remote sampling time:\s+([\d.]+) ms/);
    if (match) {
      samplingMs = parseFloat(match[1]);
      continue;
    }
    match = line.match(/^Remote kernel time:\s+([\d.]+) ms/);
    if (match) {
      kernelMs = parseFloat(match[1]);
      continue;
    }
    match = line.match(/^(.+?):\s+(\d+) ms\s*$/);
    if (match) {
      stages.push(`${match[1]} ${formatDuration(parseInt(match[2]))}`);
    }
  }

  const result: RendererOutput = { stages: stages.join("\n") };
  if (httpStatus !== undefined) {
    const parts = [`HTTP ${httpStatus}`];
    if (roundTripMs !== undefined) parts.push(`round trip ${formatDuration(roundTripMs)}`);
    if (kernelMs !== undefined) parts.push(`kernel ${formatDuration(kernelMs)}`);
    if (samplingMs !== undefined) parts.push(`sampling ${formatDuration(samplingMs)}`);
    result.remote = parts.join(" · ");
    result.remoteDetails = parts.join("\n");
  }
  return result;
}

// What a render is about to do, for the status bar.
type RenderInfo = {
  precision: string,        // e.g. "double (64-bit)" or "fixed point · 3 words (~28 digits)"
  scaleLog10: number,       // log10 of the width of the visible region
  warning?: string          // shown in the message line (for example: maximum precision exceeded)
};

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
  private samples: number = 1;
  private escapeRadius: number = 2;
  private colorSpread: number = 0.05;
  private histogram: boolean = false;
  private histogramPhase: number = 0;
  private histogramCycles: number = 1;
  private mode: string = 'CPU_PARALLEL';
  private static imgUrl: string;


  public constructor(boundary: Boundary, boxSidesRatio: number[], p5: P5){
    this.lowPrecissionBoundary = boundary;
    this.boxSidesRatio = boxSidesRatio;
    this.p5Client = p5;
    this.startingBoundary = {... boundary};
    this.loadImagePath()
    this.generateMandelbrot();
  }

  public async loadImagePath() {
    const projectDir: string = await invoke("get_project_dir");
    const path = await join(projectDir, "generated-files", "mandelbrot_set.png");
    BoundaryManager.imgUrl = convertFileSrc(path);
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
  public setSamples(samples: number){
    this.samples = samples;
    if(this.highPrecission){
      this.generateMandelbrotHighPrecission();
    }else{
      this.generateMandelbrot();
    }
  }
  public getSamples(): number{
    return this.samples;
  }
  public setEscapeRadius(escapeRadius: number){
    this.escapeRadius = escapeRadius;
    if(this.highPrecission){
      this.generateMandelbrotHighPrecission();
    }else{
      this.generateMandelbrot();
    }
  }
  public getEscapeRadius(): number{
    return this.escapeRadius;
  }
  public setColorSpread(colorSpread: number){
    this.colorSpread = colorSpread;
    if(this.highPrecission){
      this.generateMandelbrotHighPrecission();
    }else{
      this.generateMandelbrot();
    }
  }
  public getColorSpread(): number{
    return this.colorSpread;
  }
  public setHistogramCycles(cycles: number){
    this.histogramCycles = cycles;
    if(this.highPrecission){
      this.generateMandelbrotHighPrecission();
    }else{
      this.generateMandelbrot();
    }
  }
  public setHistogramPhase(phase: number){
    this.histogramPhase = phase;
    if(this.highPrecission){
      this.generateMandelbrotHighPrecission();
    }else{
      this.generateMandelbrot();
    }
  }
  public setHistogram(histogram: boolean){
    this.histogram = histogram;
    if(this.highPrecission){
      this.generateMandelbrotHighPrecission();
    }else{
      this.generateMandelbrot();
    }
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
  public setMode(mode: string){
    this.mode = mode;
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

  private transformMouseCoordinates(): Boundary{
    let relX;
    let relY;

    let width = this.releaseX - this.pressX;
    let height = this.releaseY - this.pressY;
    if(Math.abs(width) * this.boxSidesRatio[0] > Math.abs(height) * this.boxSidesRatio[1]){
      relX = this.releaseX;
      relY = this.pressY + Math.sign(height) * Math.abs(width) * 1.0 / this.boxSidesRatio[1] * this.boxSidesRatio[0];
    }else{
      relX = this.pressX + Math.sign(width) * Math.abs(height) * 1.0 / this.boxSidesRatio[0] * this.boxSidesRatio[1];
      relY = this.releaseY;
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
    if(this.lowPrecissionBoundary.reEnd - this.lowPrecissionBoundary.reStart < DOUBLE_PRECISION_MIN_WIDTH){
      // The view is too small for a double: from here on use fixed point, starting from the current view.
      this.highPrecission = true;
      this.highPrecissionBoundary = {
        reStart: new Decimal(this.lowPrecissionBoundary.reStart),
        reEnd: new Decimal(this.lowPrecissionBoundary.reEnd),
        imStart: new Decimal(this.lowPrecissionBoundary.imStart),
        imEnd: new Decimal(this.lowPrecissionBoundary.imEnd)
      }
      this.generateMandelbrotHighPrecission();
    }else{
      this.generateMandelbrot();
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
      mode: this.mode,
      ...this.lowPrecissionBoundary,
      maxIter: this.maxIter,
      paletteLength: this.paletteLength,
      paletteId: this.paletteId,
      samples: this.samples,
      escapeRadius: this.escapeRadius,
      colorSpread: this.colorSpread,
      histogram: this.histogram,
      histogramPhase: this.histogramPhase,
      histogramCycles: this.histogramCycles
    };
    const width = Math.abs(this.lowPrecissionBoundary.reEnd - this.lowPrecissionBoundary.reStart);
    await this.render("generate_mandelbrot", args, {
      precision: "double (64-bit)",
      scaleLog10: Math.log10(width)
    });
  }

  private async generateMandelbrotHighPrecission(){
    const request = this.highPrecisionRequest(this.highPrecissionBoundary!, this.p5Client.width);
    await this.render("generate_mandelbrot_hp", request.args, request.info);
  }

  // The arguments of a fixed-point render of `boundary` on an image `imageWidthPx` pixels wide. It uses the
  // smallest fixed-point size that is precise enough for that zoom level and image width.
  private highPrecisionRequest(boundary: HighPrecissionBoundary, imageWidthPx: number){
    const viewWidth = boundary.reEnd.minus(boundary.reStart);
    const choice = chooseFractionWords(viewWidth, imageWidthPx);
    const fractionWords = choice.fractionWords;
    const args = {
      mode: this.mode,
      reStart: decimalToFixedWords(boundary.reStart, fractionWords),
      reEnd: decimalToFixedWords(boundary.reEnd, fractionWords),
      imStart: decimalToFixedWords(boundary.imStart, fractionWords),
      imEnd: decimalToFixedWords(boundary.imEnd, fractionWords),
      fractionWords: fractionWords,
      maxIter: this.maxIter,
      paletteLength: this.paletteLength,
      paletteId: this.paletteId,
      samples: this.samples,
      escapeRadius: this.escapeRadius,
      colorSpread: this.colorSpread,
      histogram: this.histogram,
      histogramPhase: this.histogramPhase,
      histogramCycles: this.histogramCycles
    };
    const info: RenderInfo = {
      precision: `fixed point · ${fractionWords} words (~${fractionDigits(fractionWords)} digits)`,
      scaleLog10: viewWidth.abs().log(10).toNumber(),
      warning: choice.limitReached
        ? "Maximum precision reached: zooming in further produces an inaccurate image."
        : undefined
    };
    return { args, info };
  }

  // How much taller (or shorter) than the on-screen view an image of `width` x `height` pixels is. The export
  // keeps the centre and the width of the view, so the visible height is scaled by this factor around the centre.
  private exportHeightFactor(width: number, height: number): number {
    return (height / width) * (this.boxSidesRatio[1] / this.boxSidesRatio[0]);
  }

  // Renders the current view at `width` x `height` pixels into generated-files/exports/<filename>. The centre of the
  // image is the centre of the view on screen and the visible width is the same; a different shape shows more or
  // less above and below. This does not touch the image on screen or the status bar.
  public async exportImage(width: number, height: number, filename: string): Promise<{ path: string, precision: string, warning?: string, stages: string }> {
    const extra = { outputFilename: filename, width: width, height: height };
    const factor = this.exportHeightFactor(width, height);

    // A larger image has smaller pixels, so a view that is fine for a double at 900 pixels may not be at 8000.
    const low = this.lowPrecissionBoundary;
    const needsFixedPoint = this.highPrecission
      || Math.abs(low.reEnd - low.reStart) < DOUBLE_PRECISION_MIN_WIDTH * width / this.p5Client.width;

    let output: string;
    let precision: string;
    let warning: string | undefined;
    if (needsFixedPoint) {
      const view = this.highPrecission ? this.highPrecissionBoundary! : {
        reStart: new Decimal(low.reStart),
        reEnd: new Decimal(low.reEnd),
        imStart: new Decimal(low.imStart),
        imEnd: new Decimal(low.imEnd)
      };
      const centreIm = view.imStart.plus(view.imEnd).div(2);
      const boundary: HighPrecissionBoundary = {
        ...view,
        imStart: centreIm.plus(view.imStart.minus(centreIm).mul(factor)),
        imEnd: centreIm.plus(view.imEnd.minus(centreIm).mul(factor))
      };
      const request = this.highPrecisionRequest(boundary, width);
      precision = request.info.precision;
      warning = request.info.warning;
      output = await invoke<string>("generate_mandelbrot_hp", { ...request.args, ...extra });
    } else {
      precision = "double (64-bit)";
      const centreIm = (low.imStart + low.imEnd) / 2;
      output = await invoke<string>("generate_mandelbrot", {
        mode: this.mode,
        ...low,
        imStart: centreIm + (low.imStart - centreIm) * factor,
        imEnd: centreIm + (low.imEnd - centreIm) * factor,
        maxIter: this.maxIter,
        paletteLength: this.paletteLength,
        paletteId: this.paletteId,
        samples: this.samples,
        escapeRadius: this.escapeRadius,
        colorSpread: this.colorSpread,
        histogram: this.histogram,
        histogramPhase: this.histogramPhase,
        histogramCycles: this.histogramCycles,
        ...extra
      });
    }

    const projectDir: string = await invoke("get_project_dir");
    const path = await join(projectDir, "generated-files", "exports", filename);
    return { path, precision, warning, stages: parseRendererOutput(output).stages };
  }

  // Runs one render and keeps the status bar up to date: what it is rendering with, how long it took, what the
  // remote worker answered, or why it failed (otherwise a failed render looks like nothing happened).
  private async render(command: string, args: Record<string, unknown>, info: RenderInfo){
    const remote = this.mode === "CUDA_REMOTE";
    const started = performance.now();

    setField("status-precision", info.precision);
    setScale(info.scaleLog10);
    setField("status-time", "rendering...");
    setField("status-remote", remote ? "waiting for the worker..." : "not used (local render)");
    setMessage("");

    try {
      const output = await invoke<string>(command, args);
      this.img = this.p5Client.loadImage(BoundaryManager.imgUrl);
      const parsed = parseRendererOutput(output);
      setField("status-time", formatDuration(performance.now() - started), parsed.stages);
      if (remote) {
        setField("status-remote", parsed.remote ?? "no information from the worker", parsed.remoteDetails);
      }
      if (info.warning) {
        setMessage(info.warning, "warning");
      }
    } catch (e) {
      console.error(`Render failed (${command}, mode ${this.mode}):`, e);
      setField("status-time", "failed");
      if (remote) {
        setField("status-remote", "failed (see below)");
      }
      setMessage(String(e), "error");
    }
  }

  public reset(){
    this.maxIter = 700;
    this.paletteLength = 250;
    this.escapeRadius = 2;
    this.colorSpread = 0.05;
    this.highPrecission = false;
    this.lowPrecissionBoundary = {...this.startingBoundary};
    this.generateMandelbrot();
  }
}


const MODE_LABELS: Record<string, string> = {
  "CPU_PARALLEL": "CPU Parallel",
  "OPENCL_LOCAL": "OpenCL",
  "CUDA_LOCAL":  "CUDA Local",
  "CUDA_REMOTE": "CUDA Remote",
};

window.addEventListener("DOMContentLoaded", async () => {
  let pressX = -1;
  let pressY = -1;
  const boxRatio = [2, 3];
  let isPressed = false;
  let settingsOpen = false;   // true while the settings or the export screen covers the canvas
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
      if(settingsOpen) return;
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
      if(settingsOpen) return;
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
  const inputSamples: HTMLInputElement = document.getElementById("input-samples") as HTMLInputElement;
  const inputEscapeRadius: HTMLInputElement = document.getElementById("input-escape-radius") as HTMLInputElement;
  const inputColorSpread: HTMLInputElement = document.getElementById("input-color-spread") as HTMLInputElement;
  const buttonColorSpreadCancel: HTMLButtonElement = document.getElementById("button-color-spread-cancel") as HTMLButtonElement;
  const buttonEscapeRadiusCancel: HTMLButtonElement = document.getElementById("button-escape-radius-cancel") as HTMLButtonElement;
  const buttonPaletteLengthCancel: HTMLButtonElement = document.getElementById("button-palette-length-cancel") as HTMLButtonElement;
  const buttonSamplesCancel: HTMLButtonElement = document.getElementById("button-samples-cancel") as HTMLButtonElement;
  const buttonMaxIterCancel: HTMLButtonElement = document.getElementById("button-max-iter-cancel") as HTMLButtonElement;
  document.getElementById("button-reset")?.addEventListener("click", () => {
    boundaryManager.reset();
    inputMaxIter.value = "700";
    inputPaletteLength.value = "250";
    inputEscapeRadius.value = "2";
    buttonEscapeRadiusCancel.disabled = true;
    inputColorSpread.value = "0.05";
    buttonColorSpreadCancel.disabled = true;
  });
  document.getElementById("button-color-spread")?.addEventListener("click", () => {
    // The renderer accepts 0 to 10 (see the README); keep the field in that range.
    const parsed: number = parseFloat(inputColorSpread.value);
    const colorSpread: number = Number.isFinite(parsed) ? Math.min(10, Math.max(0, parsed)) : boundaryManager.getColorSpread();
    inputColorSpread.value = colorSpread.toString();
    buttonColorSpreadCancel.disabled = true;
    boundaryManager.setColorSpread(colorSpread);
  });
  buttonColorSpreadCancel.addEventListener("click", () => {
    inputColorSpread.value = boundaryManager.getColorSpread().toString();
    buttonColorSpreadCancel.disabled = true;
  });
  // Histogram colouring ignores the palette length, escape radius and colour spread, and only it uses the phase.
  const histogramPhaseInput = document.getElementById("input-histogram-phase") as HTMLInputElement;
  const histogramCyclesInput = document.getElementById("input-histogram-cycles") as HTMLInputElement;
  document.getElementById("input-histogram")?.addEventListener("change", (e) => {
    const on = (e.target as HTMLInputElement).checked;
    for (const id of ["input-palette-length", "button-palette-length", "input-escape-radius", "button-escape-radius",
                      "input-color-spread", "button-color-spread"]) {
      (document.getElementById(id) as HTMLInputElement | HTMLButtonElement).disabled = on;
    }
    // The Cancel buttons are only needed while a field has an unapplied edit.
    buttonPaletteLengthCancel.disabled = true;
    buttonEscapeRadiusCancel.disabled = true;
    buttonColorSpreadCancel.disabled = true;
    histogramPhaseInput.disabled = !on;
    histogramCyclesInput.disabled = !on;
    boundaryManager.setHistogram(on);
  });
  histogramCyclesInput.addEventListener("change", () => {
    boundaryManager.setHistogramCycles(parseFloat(histogramCyclesInput.value));
  });
  histogramPhaseInput.addEventListener("change", () => {
    boundaryManager.setHistogramPhase(parseFloat(histogramPhaseInput.value));
  });
  inputColorSpread.addEventListener("input", () => {
    buttonColorSpreadCancel.disabled = false;
  });
  document.getElementById("button-escape-radius")?.addEventListener("click", () => {
    // The renderer accepts 2 to 128 (see the README); keep the field in that range.
    const parsed: number = parseFloat(inputEscapeRadius.value);
    const escapeRadius: number = Number.isFinite(parsed) ? Math.min(128, Math.max(2, parsed)) : boundaryManager.getEscapeRadius();
    inputEscapeRadius.value = escapeRadius.toString();
    buttonEscapeRadiusCancel.disabled = true;
    boundaryManager.setEscapeRadius(escapeRadius);
  });
  buttonEscapeRadiusCancel.addEventListener("click", () => {
    inputEscapeRadius.value = boundaryManager.getEscapeRadius().toString();
    buttonEscapeRadiusCancel.disabled = true;
  });
  inputEscapeRadius.addEventListener("input", () => {
    buttonEscapeRadiusCancel.disabled = false;
  });
  document.getElementById("button-palette-length")?.addEventListener("click", () => {
    const paletteLength: number = parseInt(inputPaletteLength.value);
    buttonPaletteLengthCancel.disabled = true;
    boundaryManager.setPaletteLength(paletteLength);
  });
  document.getElementById("button-samples")?.addEventListener("click", () => {
    const samples: number = parseInt(inputSamples.value);
    buttonSamplesCancel.disabled = true;
    boundaryManager.setSamples(samples);
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
  document.getElementById("select-mode")?.addEventListener("change", () => {
    const mode: string = (document.getElementById("select-mode") as HTMLSelectElement).value;
    boundaryManager.setMode(mode);
  });
  inputPaletteLength.addEventListener("input", () => {
    buttonPaletteLengthCancel.disabled = false;
  });
  inputMaxIter.addEventListener("input", () => {
    buttonMaxIterCancel.disabled = false;
  });

  try {
    const availableModes: string[] = await invoke("get_available_modes");
    const selectMode = document.getElementById("select-mode") as HTMLSelectElement;
    selectMode.innerHTML = "";
    availableModes.forEach(mode => {
      const option = document.createElement("option");
      option.value = mode;
      option.textContent = MODE_LABELS[mode] ?? mode;
      selectMode.appendChild(option);
    });
  } catch (e) {
    console.error("Failed to detect available modes:", e);
  }

  // ── Settings view ──────────────────────────────────────────────────────────

  const settingsView   = document.getElementById("settings-view") as HTMLElement;
  const inputEndpoint  = document.getElementById("input-remote-endpoint") as HTMLInputElement;
  const inputApiKey    = document.getElementById("input-remote-api-key") as HTMLInputElement;
  const btnRemoteSave  = document.getElementById("button-remote-save") as HTMLButtonElement;
  const remoteStatus   = document.getElementById("remote-save-status") as HTMLElement;

  async function openSettings() {
    try {
      const settings: { endpoint: string; api_key: string } = await invoke("get_remote_settings");
      inputEndpoint.value = settings.endpoint ?? "";
      inputApiKey.value   = settings.api_key  ?? "";
    } catch (e) {
      console.error("Failed to load settings:", e);
    }
    remoteStatus.style.display = "none";
    settingsView.style.display = "flex";
    settingsOpen = true;
  }

  function closeSettings() {
    settingsView.style.display = "none";
    settingsOpen = false;
    isPressed = false;
  }

  document.getElementById("button-settings")?.addEventListener("click", openSettings);
  document.getElementById("button-settings-back")?.addEventListener("click", closeSettings);

  // ── Export view ────────────────────────────────────────────────────────────

  const exportView    = document.getElementById("export-view") as HTMLElement;
  const inputExportW  = document.getElementById("input-export-width") as HTMLInputElement;
  const inputExportH  = document.getElementById("input-export-height") as HTMLInputElement;
  const btnExport     = document.getElementById("button-export-start") as HTMLButtonElement;
  const exportStatus  = document.getElementById("export-status") as HTMLElement;
  const EXPORT_MIN_SIDE = 100;
  const EXPORT_MAX_SIDE = 8000;

  function showExportStatus(text: string, color: string) {
    exportStatus.textContent   = text;
    exportStatus.style.color   = color;
    exportStatus.style.display = "";
  }

  document.getElementById("button-export")?.addEventListener("click", () => {
    exportStatus.style.display = "none";
    exportView.style.display = "flex";
    settingsOpen = true;
  });
  document.getElementById("button-export-back")?.addEventListener("click", () => {
    exportView.style.display = "none";
    settingsOpen = false;
    isPressed = false;
  });

  btnExport.addEventListener("click", async () => {
    const width = parseInt(inputExportW.value);
    const height = parseInt(inputExportH.value);
    const sideOk = (n: number) => !isNaN(n) && n >= EXPORT_MIN_SIDE && n <= EXPORT_MAX_SIDE;
    if (!sideOk(width) || !sideOk(height)) {
      showExportStatus(`Width and height must each be between ${EXPORT_MIN_SIDE} and ${EXPORT_MAX_SIDE}.`, "#eb5757");
      return;
    }
    btnExport.disabled = true;
    btnExport.textContent = "Exporting...";
    showExportStatus("Rendering, this can take a while for large images...", "#aaa");
    const started = performance.now();
    try {
      const result = await boundaryManager.exportImage(width, height, `export_${Date.now()}.png`);
      const seconds = ((performance.now() - started) / 1000).toFixed(1);
      showExportStatus(
        `Saved ${result.path}\n${width} x ${height} px, ${result.precision}, ${seconds} s` +
        (result.warning ? `\n${result.warning}` : ""),
        result.warning ? "#ffe08a" : "#6fcf97");
    } catch (e) {
      console.error("Export failed:", e);
      showExportStatus("Export failed: " + e, "#eb5757");
    } finally {
      btnExport.disabled = false;
      btnExport.textContent = "Export";
    }
  });

  btnRemoteSave.addEventListener("click", async () => {
    const endpoint = inputEndpoint.value.trim();
    const apiKey   = inputApiKey.value.trim();
    try {
      await invoke("save_remote_settings", { endpoint, apiKey });
      remoteStatus.textContent   = "Saved.";
      remoteStatus.style.color   = "#6fcf97";
      remoteStatus.style.display = "";
      setTimeout(() => { remoteStatus.style.display = "none"; }, 2000);
    } catch (e) {
      remoteStatus.textContent   = "Failed to save: " + e;
      remoteStatus.style.color   = "#eb5757";
      remoteStatus.style.display = "";
    }
  });
});

