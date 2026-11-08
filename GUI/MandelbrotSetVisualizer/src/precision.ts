import Decimal from "decimal.js";

// The viewport coordinates are Decimal numbers, and the deepest fixed-point format (6 fraction words) has
// about 58 significant decimal digits. decimal.js rounds every result to `precision` significant digits (the
// default is only 20), which would silently cap the zoom depth long before the backend runs out of digits.
Decimal.set({ precision: 100 });

// ---------------------------------------------------------------------------------------------------------
// Precision policy: which number format to render with for a given zoom level.
//
// Every number format has a smallest step it can represent (its resolution): about 2.2e-16 for a double, and
// 2^-(32 * F) for a fixed-point format with F 32-bit fraction words. When one pixel is not much larger than
// that step, neighbouring pixels land on the same value and the image degrades.
//
// The rule: a format is good enough while one pixel is at least SAFETY_STEPS_PER_PIXEL times its resolution.
// Measured against a high-precision reference, 3 steps per pixel changes about 3% of the pixels (visually
// indistinguishable); 100 steps change about 0.3% and 1000 steps about 0.04%. 3 matches the quality of the
// original double -> fixed-point switch.
// ---------------------------------------------------------------------------------------------------------

// Number of resolution steps that must fit in one pixel (see above).
export const SAFETY_STEPS_PER_PIXEL = 3;

// Double precision is used while the viewport is at least this wide (unchanged from the original rule).
export const DOUBLE_PRECISION_MIN_WIDTH = 6e-13;

// Fixed-point sizes the app uses. Size 1 is never useful: its resolution (2.3e-10) is coarser than a double's.
export const MIN_FRACTION_WORDS = 2;
export const MAX_FRACTION_WORDS = 6;   // the largest size the backend supports

export type FractionChoice = {
  fractionWords: number,
  // True when even the largest format is not precise enough any more; rendering continues at the largest size.
  limitReached: boolean
};

// Resolution (smallest representable step) of a fixed-point format with `fractionWords` fraction words.
export function resolution(fractionWords: number): Decimal {
  return Decimal.pow(2, -32 * fractionWords);
}

// Approximate number of decimal digits after the point that a format holds.
export function fractionDigits(fractionWords: number): number {
  return Math.floor(32 * fractionWords * Math.LOG10E * Math.LN2);
}

// Smallest sufficient fixed-point size for a viewport of the given width, shown on a canvas that is
// `canvasWidthPx` pixels wide.
export function chooseFractionWords(viewportWidth: Decimal, canvasWidthPx: number): FractionChoice {
  const pixelSize = viewportWidth.abs().div(canvasWidthPx);
  for (let f = MIN_FRACTION_WORDS; f <= MAX_FRACTION_WORDS; f++) {
    if (pixelSize.gte(resolution(f).mul(SAFETY_STEPS_PER_PIXEL))) {
      return { fractionWords: f, limitReached: false };
    }
  }
  return { fractionWords: MAX_FRACTION_WORDS, limitReached: true };
}

// Viewport width below which a format stops being good enough, for a canvas `canvasWidthPx` pixels wide.
export function minViewportWidth(fractionWords: number, canvasWidthPx: number): Decimal {
  return resolution(fractionWords).mul(SAFETY_STEPS_PER_PIXEL).mul(canvasWidthPx);
}

// ---------------------------------------------------------------------------------------------------------
// Conversion between Decimal and the backend's fixed-point words: 1 whole word followed by `fractionWords`
// fraction words, most significant first, 32 bits each, two's complement (the same layout as the C++ library).
// ---------------------------------------------------------------------------------------------------------

// Converts to fixed point, truncating toward zero like the C++ conversion does.
export function decimalToFixedWords(value: Decimal, fractionWords: number): number[] {
  const totalBits = BigInt(32 * (fractionWords + 1));
  const scaled = value.abs().mul(Decimal.pow(2, 32 * fractionWords)).trunc();
  let raw = BigInt(scaled.toFixed(0));
  if (value.isNegative()) {
    raw = -raw;
  }
  raw &= (1n << totalBits) - 1n;   // two's complement within the format's width

  const words: number[] = [];
  for (let i = fractionWords; i >= 0; i--) {
    words.push(Number((raw >> BigInt(32 * i)) & 0xFFFFFFFFn));
  }
  return words;
}
