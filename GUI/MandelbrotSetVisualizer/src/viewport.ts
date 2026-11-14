import Decimal from "decimal.js";
import { DOUBLE_PRECISION_MIN_WIDTH } from "./precision";
import type { PixelBox } from "./selection";

// Which part of the complex plane is shown. Imaginary values decrease downwards on the screen, so imStart is the
// bottom edge and imEnd the top one.
export type Boundary = { reStart: number, reEnd: number, imStart: number, imEnd: number };
export type PreciseBoundary = { reStart: Decimal, reEnd: Decimal, imStart: Decimal, imEnd: Decimal };

// A view is either in double precision or, once it is too small for a double, in arbitrary precision (rendered with
// fixed-point numbers). Boundaries are replaced, never changed in place, so views can be shared and stored freely.
export type Viewport =
  | { kind: "double", boundary: Boundary }
  | { kind: "precise", boundary: PreciseBoundary };

export type Size = { width: number, height: number };

export function toPreciseBoundary(b: Boundary): PreciseBoundary {
  return {
    reStart: new Decimal(b.reStart),
    reEnd: new Decimal(b.reEnd),
    imStart: new Decimal(b.imStart),
    imEnd: new Decimal(b.imEnd)
  };
}

export function isSameViewport(a: Viewport, b: Viewport): boolean {
  return a.kind === "double" && b.kind === "double"
    && a.boundary.reStart === b.boundary.reStart && a.boundary.reEnd === b.boundary.reEnd
    && a.boundary.imStart === b.boundary.imStart && a.boundary.imEnd === b.boundary.imEnd;
}

// Linear map of `n` from [inMin, inMax] to [outMin, outMax].
function mapDouble(n: number, inMin: number, inMax: number, outMin: number, outMax: number): number {
  return (n - inMin) / (inMax - inMin) * (outMax - outMin) + outMin;
}

function mapPrecise(n: number, inMin: number, inMax: number, outMin: Decimal, outMax: Decimal): Decimal {
  return new Decimal(n).sub(inMin).mul(outMax.sub(outMin)).div(new Decimal(inMax).sub(inMin)).add(outMin);
}

// The view that a box drawn on the canvas selects. Switches to arbitrary precision when the result is too small for
// a double.
export function zoomViewport(viewport: Viewport, box: PixelBox, canvas: Size): Viewport {
  if (viewport.kind === "precise") {
    const b = viewport.boundary;
    return {
      kind: "precise",
      boundary: {
        reStart: mapPrecise(box.left, 0, canvas.width, b.reStart, b.reEnd),
        reEnd: mapPrecise(box.right, 0, canvas.width, b.reStart, b.reEnd),
        imStart: mapPrecise(box.bottom, canvas.height, 0, b.imStart, b.imEnd),
        imEnd: mapPrecise(box.top, canvas.height, 0, b.imStart, b.imEnd)
      }
    };
  }

  const b = viewport.boundary;
  const zoomed: Boundary = {
    reStart: mapDouble(box.left, 0, canvas.width, b.reStart, b.reEnd),
    reEnd: mapDouble(box.right, 0, canvas.width, b.reStart, b.reEnd),
    imStart: mapDouble(box.bottom, canvas.height, 0, b.imStart, b.imEnd),
    imEnd: mapDouble(box.top, canvas.height, 0, b.imStart, b.imEnd)
  };
  if (zoomed.reEnd - zoomed.reStart < DOUBLE_PRECISION_MIN_WIDTH) {
    return { kind: "precise", boundary: toPreciseBoundary(zoomed) };
  }
  return { kind: "double", boundary: zoomed };
}

// The same boundary with its visible height scaled by `factor` around the centre (width and centre stay). Used when
// an image has another shape than the canvas.
export function scaleHeight(boundary: Boundary, factor: number): Boundary {
  const centre = (boundary.imStart + boundary.imEnd) / 2;
  return {
    ...boundary,
    imStart: centre + (boundary.imStart - centre) * factor,
    imEnd: centre + (boundary.imEnd - centre) * factor
  };
}

export function scalePreciseHeight(boundary: PreciseBoundary, factor: number): PreciseBoundary {
  const centre = boundary.imStart.plus(boundary.imEnd).div(2);
  return {
    ...boundary,
    imStart: centre.plus(boundary.imStart.minus(centre).mul(factor)),
    imEnd: centre.plus(boundary.imEnd.minus(centre).mul(factor))
  };
}
