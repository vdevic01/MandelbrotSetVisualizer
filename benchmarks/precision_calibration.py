#!/usr/bin/env python3
"""
Measures how many pixels of a rendered image are wrong for each number format, to justify the zoom levels at
which the app switches to a larger fixed-point size (GUI/MandelbrotSetVisualizer/src/precision.ts).

Method. A format can only represent values on a grid of its resolution: 2^-(32 * F) for F fraction words. Let
r be the number of grid steps that fit in one pixel. For each F and r this script builds a view whose pixel is
exactly r steps wide, renders it with F fraction words and with the largest format (6 words, effectively exact
at these zoom levels), and prints the share of pixels that differ. The views are centred on points where the set
has detail at every scale (the Misiurewicz point i and the antenna tip -2), so the result does not depend on a
lucky flat region. It also measures plain double precision for comparison.

Typical result (200 x 120 pixels, 1000 iterations):

    r (steps per pixel)       10      100     1000    10000
    pixels that differ       0.5-2%  0.1-0.6%  <0.1%   ~0%

The app uses r = 3 ("SAFETY_STEPS_PER_PIXEL"), the same quality as its original double -> fixed-point switch;
the pixel boundaries are computed directly from the view (see Sampler.cpp), so the distortion that a truncated
per-pixel step would add at small r does not occur.

Usage:
    python benchmarks/precision_calibration.py --exe path/to/mandelbrot_visualizer[.exe]

Needs numpy (only for comparing the images). Takes about a minute.
"""

import argparse
import os
import struct
import subprocess
import sys
import tempfile
import zlib
from decimal import Decimal, getcontext
from fractions import Fraction

import numpy as np

getcontext().prec = 120
REFERENCE_WORDS = 6
RATIOS = [1e1, 1e2, 1e3, 1e4, 1e5]
FRACTION_SIZES = [2, 3, 4, 5]
CENTRES = {
    "i (Misiurewicz point)": (Fraction(0), Fraction(1)),
    "-2 (antenna tip)": (Fraction(-2), Fraction(0)),
}


def read_png(path):
    """Minimal PNG reader (8-bit RGB/RGBA, all filter types), enough for the renderer's output."""
    data = open(path, "rb").read()
    pos, idat, width = 8, b"", 0
    while pos < len(data):
        length, kind = struct.unpack(">I4s", data[pos:pos + 8])
        body = data[pos + 8:pos + 8 + length]
        if kind == b"IHDR":
            width, height, _depth, color_type = struct.unpack(">IIBB", body[:10])
        elif kind == b"IDAT":
            idat += body
        pos += 12 + length
    channels = {2: 3, 6: 4, 0: 1}[color_type]
    raw = zlib.decompress(idat)
    stride = width * channels
    out = np.zeros((height, stride), dtype=np.uint8)
    prev = np.zeros(stride, dtype=np.int32)
    p = 0
    for y in range(height):
        filter_type = raw[p]
        line = np.frombuffer(raw[p + 1:p + 1 + stride], dtype=np.uint8).astype(np.int32)
        p += 1 + stride
        if filter_type == 0:
            cur = line
        elif filter_type == 2:
            cur = (line + prev) & 255
        else:
            cur = np.zeros(stride, dtype=np.int32)
            for i in range(stride):
                a = cur[i - channels] if i >= channels else 0
                b = prev[i]
                c = prev[i - channels] if i >= channels else 0
                if filter_type == 1:
                    cur[i] = (line[i] + a) & 255
                elif filter_type == 3:
                    cur[i] = (line[i] + ((a + b) >> 1)) & 255
                else:
                    pa, pb, pc = abs(b - c), abs(a - c), abs(a + b - 2 * c)
                    predictor = a if pa <= pb and pa <= pc else (b if pb <= pc else c)
                    cur[i] = (line[i] + predictor) & 255
        out[y] = cur
        prev = cur
    return out.reshape(height, width, channels)


def decimal_text(x):
    return str(Decimal(x.numerator) / Decimal(x.denominator))


class Renderer:
    def __init__(self, exe, width, height, max_iter):
        self.exe, self.w, self.h, self.max_iter = exe, width, height, max_iter

    def render(self, fraction_words, cx, cy, view_width):
        """fraction_words = None renders in double precision."""
        half_w, half_h = view_width / 2, view_width * self.h / self.w / 2
        bounds = (cx - half_w, cx + half_w, cy - half_h, cy + half_h)
        with tempfile.TemporaryDirectory() as tmp:
            png = os.path.join(tmp, "out.png")
            # The bounds go in as decimal text: the renderer converts them to fixed point exactly.
            args = [self.exe, "--mode", "CPU_PARALLEL", "--output", png, "--max-iter", str(self.max_iter),
                    "--palette-length", "512", "--samples", "1", "--size", "%dx%d" % (self.w, self.h),
                    "--re=%s:%s" % (decimal_text(bounds[0]), decimal_text(bounds[1])),
                    "--im=%s:%s" % (decimal_text(bounds[2]), decimal_text(bounds[3]))]
            args += ["--precision", "double"] if fraction_words is None else ["--precision", "fixed", "--fraction-words", str(fraction_words)]
            run = subprocess.run(args, capture_output=True, text=True)
            if run.returncode != 0:
                sys.exit("renderer failed: " + (run.stdout + run.stderr)[-300:])
            return read_png(png)


def percent_different(a, b):
    return float(np.mean(np.any(a != b, axis=2))) * 100


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--exe", required=True, help="path to the built mandelbrot_visualizer executable")
    parser.add_argument("--size", type=int, nargs=2, default=[200, 120], metavar=("W", "H"), help="image size in pixels")
    parser.add_argument("--max-iter", type=int, default=1000)
    args = parser.parse_args()
    renderer = Renderer(args.exe, args.size[0], args.size[1], args.max_iter)

    print("Pixels (%%) that differ from the %d-word reference, by steps per pixel r:" % REFERENCE_WORDS)
    print("%-24s %-3s " % ("view", "F") + "  ".join("r=%-8.0e" % r for r in RATIOS))
    for name, (cx, cy) in CENTRES.items():
        for f in FRACTION_SIZES:
            resolution = Fraction(1, 1 << (32 * f))
            row = []
            for r in RATIOS:
                view_width = Fraction(r) * resolution * args.size[0]     # exactly r grid steps per pixel
                reference = renderer.render(REFERENCE_WORDS, cx, cy, view_width)
                row.append(percent_different(renderer.render(f, cx, cy, view_width), reference))
            print("%-24s %-3d " % (name, f) + "  ".join("%-10.2f" % v for v in row))

    print()
    print("Double precision: pixels (%) that differ from the reference, by viewport width:")
    widths = [Fraction(10) ** e for e in (-9, -10, -11, -12, -13, -14)]
    print("%-24s " % "view" + "  ".join("w=1e%-6d" % round(float(__import__("math").log10(float(w)))) for w in widths))
    for name, (cx, cy) in CENTRES.items():
        row = [percent_different(renderer.render(None, cx, cy, w), renderer.render(REFERENCE_WORDS, cx, cy, w)) for w in widths]
        print("%-24s " % name + "  ".join("%-10.2f" % v for v in row))


if __name__ == "__main__":
    main()
