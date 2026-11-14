// Everything the user can choose that is not the position of the view.

// Settings that only change the colours. Changing one of them does not need new escape iterations.
export const COLOR_KEYS = [
  "paletteLength", "paletteId", "colorSpread", "colorPhase", "histogram", "histogramCycles"
] as const;

export type RenderSettings = {
  mode: string,
  maxIter: number,
  samples: number,
  escapeRadius: number,
  paletteId: number,
  paletteLength: number,
  colorSpread: number,
  colorPhase: number,
  histogram: boolean,
  histogramCycles: number
};

export type ColorSettings = Pick<RenderSettings, typeof COLOR_KEYS[number]>;

export function isColorKey(key: keyof RenderSettings): key is keyof ColorSettings {
  return (COLOR_KEYS as readonly string[]).includes(key);
}

export function pickColorSettings(settings: RenderSettings): ColorSettings {
  return {
    paletteLength: settings.paletteLength,
    paletteId: settings.paletteId,
    colorSpread: settings.colorSpread,
    colorPhase: settings.colorPhase,
    histogram: settings.histogram,
    histogramCycles: settings.histogramCycles
  };
}
