import type { RenderSettings } from "./render-settings";

// Size of the image shown on screen, in pixels.
export const CANVAS_WIDTH = 900;
export const CANVAS_HEIGHT = 600;

// The part of the complex plane shown when the app starts (and after Reset).
export const STARTING_BOUNDARY = { reStart: -2.0, reEnd: 1.0, imStart: -1.0, imEnd: 1.0 } as const;

// How many earlier views undo can go back through.
export const MAX_UNDO_VIEWS = 100;

// A mouse drag smaller than this (in pixels, on either side) is a click, not a zoom box.
export const MIN_SELECTION_PX = 4;

export const DEFAULT_SETTINGS: Readonly<RenderSettings> = {
  mode: "CPU_PARALLEL",
  maxIter: 700,
  samples: 1,
  escapeRadius: 2,
  paletteId: 0,
  paletteLength: 250,
  colorSpread: 0,
  colorPhase: 0,
  histogram: false,
  histogramCycles: 1
};

// Accepted ranges of the numeric settings (the renderer enforces the same ones, see the README).
export const ESCAPE_RADIUS_RANGE = { min: 2, max: 128 } as const;
export const COLOR_SPREAD_RANGE = { min: 0, max: 10 } as const;

// Size limits of an exported image, per side.
export const EXPORT_MIN_SIDE = 100;
export const EXPORT_MAX_SIDE = 8000;

export const MODE_LABELS: Readonly<Record<string, string>> = {
  CPU_PARALLEL: "CPU Parallel",
  OPENCL_LOCAL: "OpenCL",
  CUDA_LOCAL: "CUDA Local",
  CUDA_REMOTE: "CUDA Remote"
};

// The mode in which rendering happens on a remote worker.
export const REMOTE_MODE = "CUDA_REMOTE";
