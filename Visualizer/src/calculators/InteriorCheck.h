#pragma once

// Interior check: a point of the main cardioid or of the period-2 bulb is in the Mandelbrot set, so it never
// escapes and the iteration loop can be skipped (the result is the same as running all maxIter iterations:
// "never escaped"). These two shapes cover most of the interior of a typical full view, and the interior is
// where every point costs the full maxIter iterations.
//
//   cardioid:  q * (q + (x - 1/4)) < y^2 / 4   with q = (x - 1/4)^2 + y^2
//   period 2:  (x + 1)^2 + y^2 < 1/16
//
// The tests are exact in real arithmetic. In floating point a point that is closer to the boundary than the
// rounding error could be put on the wrong side, so both regions are shrunk by INTERIOR_MARGIN: a point that
// close to the boundary is simply iterated. (Near the boundary the iteration is also the part that is needed:
// those points escape late or not at all, and the loop settles it.)

constexpr double INTERIOR_MARGIN = 1e-9;

inline bool isInteriorPoint(double x, double y) {
    const double y2 = y * y;
    const double dx = x - 0.25;
    const double q = dx * dx + y2;
    if (q * (q + dx) < 0.25 * y2 - INTERIOR_MARGIN) return true;   // main cardioid
    const double bx = x + 1.0;
    return bx * bx + y2 < 0.0625 - INTERIOR_MARGIN;                 // period-2 bulb
}
