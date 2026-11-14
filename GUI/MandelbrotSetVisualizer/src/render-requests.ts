// Builds the arguments of the render commands from the settings and the view.

import type { RenderArgs, RenderCommand } from "./backend";
import { chooseFractionWords, fractionDigits } from "./precision";
import { pickColorSettings, type RenderSettings } from "./render-settings";
import type { Viewport } from "./viewport";

// What a render is about to do, for the status bar.
export type RenderInfo = {
  precision: string,        // e.g. "double (64-bit)" or "fixed point · 3 words (~28 digits)"
  scaleLog10: number,       // log10 of the width of the visible region
  warning?: string          // shown in the message line (for example: maximum precision exceeded)
};

export type RenderRequest = { command: RenderCommand, args: RenderArgs, info: RenderInfo };

// A full render of `viewport` on an image `imageWidthPx` pixels wide. In arbitrary precision it uses the smallest
// fixed-point size that is precise enough for that zoom level and image width.
export function buildRenderRequest(settings: RenderSettings, viewport: Viewport, imageWidthPx: number): RenderRequest {
  if (viewport.kind === "double") {
    const b = viewport.boundary;
    return {
      command: "generate_mandelbrot",
      args: { ...settings, ...b },
      info: { precision: "double (64-bit)", scaleLog10: Math.log10(Math.abs(b.reEnd - b.reStart)) }
    };
  }

  const b = viewport.boundary;
  const viewWidth = b.reEnd.minus(b.reStart);
  const choice = chooseFractionWords(viewWidth, imageWidthPx);
  return {
    command: "generate_mandelbrot_hp",
    args: {
      ...settings,
      // Decimal text keeps every digit; the renderer converts it to fixed point exactly.
      reStart: b.reStart.toFixed(),
      reEnd: b.reEnd.toFixed(),
      imStart: b.imStart.toFixed(),
      imEnd: b.imEnd.toFixed(),
      fractionWords: choice.fractionWords
    },
    info: {
      precision: `fixed point · ${choice.fractionWords} words (~${fractionDigits(choice.fractionWords)} digits)`,
      scaleLog10: viewWidth.abs().log(10).toNumber(),
      warning: choice.limitReached
        ? "Maximum precision reached: zooming in further produces an inaccurate image."
        : undefined
    }
  };
}

// Paints the last render again with other colour settings.
export function buildRecolourArgs(settings: RenderSettings): RenderArgs {
  return { ...pickColorSettings(settings) };
}
