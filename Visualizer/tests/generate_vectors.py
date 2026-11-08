#!/usr/bin/env python3
"""
Generates test vectors for the fixed-point library (src/math/FixedPointArithmetics.*).

The expected results come from exact, arbitrary-precision Python integer arithmetic, written from the
mathematical specification below and NOT translated from the C++ code. `--selfcheck` additionally
cross-validates the reference formulas against a second, independent implementation (the stdlib
`decimal` module), so a mistake in this script cannot silently become the "truth".

Number model (for a format with F fraction words)
-------------------------------------------------
* A number is N = 1 + F words of 32 bits, most significant word first, in two's complement.
* Read as one signed B = 32*N bit integer V, its real value is V / 2^(32*F).

Reference semantics (the contract the tests enforce)
----------------------------------------------------
add, sub, inc, cmpl : exact result, wrapped modulo 2^B (two's complement wrap-around).
gt, gte             : mathematical signed comparison (1 or 0).
mul                 : |a|*|b| >> 32F, sign applied afterwards, i.e. truncation toward zero.
                      Only emitted when the exact result is representable.
div                 : (|a| << 32F) // |b|, sign applied afterwards (truncation toward zero).
                      Only emitted when b != 0 and the exact result is representable.
f2x (double->fixed) : exact value truncated toward zero to 32F fraction bits (a double is exact in
                      binary, so only bits beyond the format are dropped). Only emitted when the
                      result is representable.
x2f (fixed->double) : the correctly rounded double. Tagged `exact` when the fixed value is exactly
                      a double (tests may compare for equality) and `approx` otherwise (tests
                      should allow a few ULP, the C++ code accumulates bit by bit).

Output
------
One text file per format: tests/vectors/fpa_f<F>.txt. Lines starting with '#' are comments.
    format <F>
    <op> <tag> <hex args...>
Each fixed-point number is ONE token of N*8 hex digits (most significant word first). A double is
one token of 16 hex digits holding its IEEE-754 bit pattern (avoids any text<->double parsing issues).
    add|sub|mul|div <tag> a b result
    inc|cmpl        <tag> a result
    gt|gte          <tag> a b 0|1
    f2x             <tag> doubleBits result
    x2f             <tag> a doubleBits

Usage
-----
    python generate_vectors.py                       # (re)generate all formats into tests/vectors
    python generate_vectors.py --formats 3 4         # only some fraction sizes
    python generate_vectors.py --ops add mul         # only some operations
    python generate_vectors.py --random 50           # fewer random cases per operation
    python generate_vectors.py --check               # fail if the committed files are stale
    python generate_vectors.py --selfcheck           # cross-validate the reference formulas
"""

import argparse
import itertools
import math
import random
import struct
import sys
from decimal import Decimal, ROUND_DOWN, getcontext
from fractions import Fraction
from pathlib import Path

WORD_BITS = 32
DEFAULT_FORMATS = [1, 2, 3, 4, 5, 6]
ALL_OPS = ["add", "sub", "inc", "cmpl", "gt", "gte", "mul", "div", "f2x", "x2f"]
DEFAULT_SEED = 20240611
DEFAULT_RANDOM = 100        # random cases per binary operation
EDGE_RANDOM_PAIRS = 150     # pairs drawn at random from the full edge-value set
OUT_DIR = Path(__file__).resolve().parent / "vectors"


# --------------------------------------------------------------------------- number format helpers
class Fmt:
    def __init__(self, fraction_words):
        self.f = fraction_words
        self.n = 1 + fraction_words              # total words
        self.bits = WORD_BITS * self.n           # total bits B
        self.frac_bits = WORD_BITS * fraction_words
        self.one = 1 << self.frac_bits           # the value 1.0
        self.min = -(1 << (self.bits - 1))
        self.max = (1 << (self.bits - 1)) - 1

    def wrap(self, v):
        """Wrap an arbitrary integer into the signed B-bit range."""
        v &= (1 << self.bits) - 1
        return v - (1 << self.bits) if v >> (self.bits - 1) else v

    def fits(self, v):
        return self.min <= v <= self.max

    def hex(self, v):
        """Signed value -> N*8 hex digits (two's complement, most significant word first)."""
        return format(v & ((1 << self.bits) - 1), "0%dx" % (self.n * 8))


def double_bits(x):
    return format(struct.unpack(">Q", struct.pack(">d", x))[0], "016x")


def bits_to_double(h):
    return struct.unpack(">d", struct.pack(">Q", int(h, 16)))[0]


# --------------------------------------------------------------------------- reference operations
def ref_add(fm, a, b):  return fm.wrap(a + b)
def ref_sub(fm, a, b):  return fm.wrap(a - b)
def ref_inc(fm, a):     return fm.wrap(a + 1)
def ref_cmpl(fm, a):    return fm.wrap(-a)


def ref_mul(fm, a, b):
    magnitude = (abs(a) * abs(b)) >> fm.frac_bits
    return -magnitude if (a < 0) != (b < 0) else magnitude


def ref_div(fm, a, b):
    magnitude = (abs(a) << fm.frac_bits) // abs(b)
    return -magnitude if (a < 0) != (b < 0) else magnitude


def ref_f2x(fm, x):
    magnitude = math.floor(abs(Fraction(x)) * fm.one)
    return -magnitude if x < 0 else magnitude


def ref_x2f(fm, v):
    return v / fm.one   # int / int is correctly rounded to the nearest double in Python 3


# --------------------------------------------------------------------------- value pools
def edge_values(fm):
    """Hand-picked values that stress boundaries, in the format's raw integer representation."""
    vals = {0, 1, -1, 2, -2, 3, fm.min, fm.min + 1, fm.max, fm.max - 1}
    vals |= {fm.one, -fm.one, fm.one // 2, -(fm.one // 2), fm.one + 1, fm.one - 1,
             -(fm.one + 1), -(fm.one - 1), 2 * fm.one, -2 * fm.one, 3 * fm.one // 2,
             fm.one // 4, fm.one // 3, -(fm.one // 3), (fm.one * 1) // 10}
    # powers of two around every word boundary, positive and negative
    for j in range(fm.n + 1):
        base = 1 << (WORD_BITS * j)
        for d in (-1, 0, 1):
            vals.add(base + d)
            vals.add(-(base + d))
    for k in (0, 1, 30, 31, 32, 33, 63, 64, 65, fm.frac_bits - 1, fm.frac_bits, fm.frac_bits + 1,
              fm.bits - 3, fm.bits - 2):
        if k >= 0:
            vals.add(1 << k)
            vals.add(-(1 << k))
    # whole parts near the top of the range
    for whole in (2 ** 31 - 1, 2 ** 30, 2 ** 16, 255, 7):
        vals.add(whole * fm.one)
        vals.add(-whole * fm.one)
        vals.add(whole * fm.one + 1)
        vals.add(whole * fm.one - 1)
    # repeating bit patterns (alternating bits, all ones in the fraction, a single set bit per word)
    for pattern in (0xAAAAAAAA, 0x55555555, 0xFFFFFFFF, 0x80000000, 0x7FFFFFFF, 0x00000001, 0xFFFF0000):
        v = 0
        for _ in range(fm.n):
            v = (v << WORD_BITS) | pattern
        vals.add(fm.wrap(v))
        # same pattern but with a small whole part
        v2 = 0
        for w in range(fm.n):
            v2 = (v2 << WORD_BITS) | (pattern if w else 1)
        vals.add(fm.wrap(v2))
    return sorted(v for v in vals if fm.fits(v))


def core_values(fm):
    """Small set of the most important values, used for the full cross-product of binary operations."""
    vals = {0, 1, -1, 2, fm.min, fm.min + 1, fm.max, fm.max - 1, fm.one, -fm.one, fm.one // 2,
            -(fm.one // 2), fm.one + 1, fm.one - 1, 2 * fm.one, -2 * fm.one, 1 << (WORD_BITS - 1),
            -(1 << (WORD_BITS - 1)), (1 << WORD_BITS) - 1, 1 << WORD_BITS, 3 * fm.one // 2,
            (2 ** 31 - 1) * fm.one, -(2 ** 31 - 1) * fm.one}
    return sorted(v for v in vals if fm.fits(v))


def random_value(fm, rng):
    """Random signed value with a random bit length, sometimes built from 'interesting' words."""
    if rng.getrandbits(2) == 0:
        specials = [0, 1, 0x7FFFFFFF, 0x80000000, 0xFFFFFFFF, 0xFFFFFFFE]
        v = 0
        for _ in range(fm.n):
            word = rng.choice(specials) if rng.getrandbits(1) else rng.getrandbits(WORD_BITS)
            v = (v << WORD_BITS) | word
        return fm.wrap(v)
    length = 1 + rng.getrandbits(8) % (fm.bits - 1)
    magnitude = rng.getrandbits(length)
    return -magnitude if rng.getrandbits(1) else magnitude


def random_double(rng):
    """Random finite double with |x| < 2^31 spread over many magnitudes."""
    exponent = rng.randrange(-140, 31)
    mantissa = rng.getrandbits(52)
    sign = rng.getrandbits(1)
    bits = (sign << 63) | ((exponent + 1023) << 52) | mantissa
    return struct.unpack(">d", struct.pack(">Q", bits))[0]


def edge_doubles(fm):
    vals = [0.0, -0.0, 1.0, -1.0, 0.5, -0.5, 2.0, 0.1, -0.1, 0.3, 1 / 3, 3.141592653589793, -2.718281828459045,
            123.456, 1e-9, 1e-30, 5e-324, 2.0 ** -32, 2.0 ** -33, 2.0 ** -64, 2.0 ** -96, 2.0 ** -97,
            2.0 ** -(fm.frac_bits), 2.0 ** -(fm.frac_bits + 1), 2.0 ** 30, 2.0 ** 31 - 1, 2.0 ** 31 - 0.5,
            math.nextafter(2.0 ** 31, 0.0), -(2.0 ** 31), -math.nextafter(2.0 ** 31, 0.0), 0.9999999999999999,
            1.0000000000000002, 4294967295.0 / 2, 1e-300]
    return vals


# --------------------------------------------------------------------------- vector generation
def gen_binary(fm, op, rng, n_random, fn, valid):
    """Emits cases for a binary operation: core x core cross-product + edge pairs + random pairs."""
    lines = []
    seen = set()

    def emit(tag, a, b):
        if (a, b) in seen:
            return
        if not valid(fm, a, b):
            return
        seen.add((a, b))
        lines.append("%s %s %s %s %s" % (op, tag, fm.hex(a), fm.hex(b), fm.hex(fn(fm, a, b))))

    core = core_values(fm)
    for a, b in itertools.product(core, core):
        emit("core", a, b)
    edges = edge_values(fm)
    for _ in range(EDGE_RANDOM_PAIRS):
        emit("edge", rng.choice(edges), rng.choice(edges))
    for _ in range(n_random):
        emit("random", random_value(fm, rng), random_value(fm, rng))
    return lines


def gen_compare(fm, op, rng, n_random, predicate):
    lines = []
    seen = set()

    def emit(tag, a, b):
        if (a, b) in seen:
            return
        seen.add((a, b))
        lines.append("%s %s %s %s %d" % (op, tag, fm.hex(a), fm.hex(b), 1 if predicate(a, b) else 0))

    core = core_values(fm)
    for a, b in itertools.product(core, core):
        emit("core", a, b)
    edges = edge_values(fm)
    for _ in range(EDGE_RANDOM_PAIRS):
        emit("edge", rng.choice(edges), rng.choice(edges))
    # neighbours: values one ULP apart, the hardest case for a comparison
    for a in core + edges[: len(edges) // 2]:
        for d in (-1, 1):
            if fm.fits(a + d):
                emit("adjacent", a, a + d)
                emit("adjacent", a + d, a)
    for _ in range(n_random):
        emit("random", random_value(fm, rng), random_value(fm, rng))
    return lines


def gen_unary(fm, op, rng, n_random, fn):
    lines = []
    seen = set()

    def emit(tag, a):
        if a in seen:
            return
        seen.add(a)
        lines.append("%s %s %s %s" % (op, tag, fm.hex(a), fm.hex(fn(fm, a))))

    for a in edge_values(fm):
        emit("edge", a)
    for _ in range(n_random):
        emit("random", random_value(fm, rng))
    return lines


def gen_f2x(fm, rng, n_random):
    lines = []
    seen = set()

    def emit(tag, x):
        bits = double_bits(x)
        if bits in seen:
            return
        result = ref_f2x(fm, x)
        if not fm.fits(result):
            return
        seen.add(bits)
        lines.append("f2x %s %s %s" % (tag, bits, fm.hex(result)))

    for x in edge_doubles(fm):
        emit("edge", x)
    for _ in range(n_random):
        emit("random", random_double(rng))
    # doubles that are exactly representable in this format (round trip must be lossless)
    for _ in range(n_random // 2):
        v = random_value(fm, rng)
        x = ref_x2f(fm, v)
        if abs(x) < 2.0 ** 31 and Fraction(x) * fm.one == v:
            emit("exact", x)
    return lines


def gen_x2f(fm, rng, n_random):
    lines = []
    seen = set()

    def emit(tag, v):
        if v in seen:
            return
        seen.add(v)
        x = ref_x2f(fm, v)
        exact = Fraction(x) * fm.one == v
        lines.append("x2f %s %s %s" % ("exact" if exact else "approx", fm.hex(v), double_bits(x)))

    for v in edge_values(fm):
        emit("edge", v)
    for _ in range(n_random):
        emit("random", random_value(fm, rng))
    # values that are exactly representable as doubles, so strict comparison is possible
    for _ in range(n_random // 2):
        emit("exact", fm.wrap(rng.getrandbits(53) << rng.randrange(0, max(1, fm.bits - 53))) * (1 if rng.getrandbits(1) else -1))
    return lines


def generate(fraction_words, ops, seed, n_random):
    fm = Fmt(fraction_words)
    rng = random.Random(seed * 1000 + fraction_words)   # independent stream per format, stable across runs
    out = [
        "# Generated by tests/generate_vectors.py - DO NOT EDIT BY HAND (run the script to regenerate).",
        "# seed=%d random=%d. See the script's docstring for the exact semantics of every operation." % (seed, n_random),
        "format %d" % fraction_words,
    ]
    makers = {
        "add":  lambda: gen_binary(fm, "add", rng, n_random, ref_add, lambda f, a, b: True),
        "sub":  lambda: gen_binary(fm, "sub", rng, n_random, ref_sub, lambda f, a, b: True),
        "inc":  lambda: gen_unary(fm, "inc", rng, n_random, lambda f, a: ref_inc(f, a)),
        "cmpl": lambda: gen_unary(fm, "cmpl", rng, n_random, lambda f, a: ref_cmpl(f, a)),
        "gt":   lambda: gen_compare(fm, "gt", rng, n_random, lambda a, b: a > b),
        "gte":  lambda: gen_compare(fm, "gte", rng, n_random, lambda a, b: a >= b),
        "mul":  lambda: gen_binary(fm, "mul", rng, n_random, ref_mul, lambda f, a, b: f.fits(ref_mul(f, a, b))),
        "div":  lambda: gen_binary(fm, "div", rng, n_random, ref_div, lambda f, a, b: b != 0 and f.fits(ref_div(f, a, b))),
        "f2x":  lambda: gen_f2x(fm, rng, n_random),
        "x2f":  lambda: gen_x2f(fm, rng, n_random),
    }
    for op in ALL_OPS:       # fixed order => stable output regardless of --ops order
        if op in ops:
            out.append("# --- %s" % op)
            out.extend(makers[op]())
    return "\n".join(out) + "\n"


# --------------------------------------------------------------------------- self check
def selfcheck(seed):
    """Cross-validates the reference formulas against Python's decimal module."""
    getcontext().prec = 600
    rng = random.Random(seed)
    checked = 0
    for f in DEFAULT_FORMATS:
        fm = Fmt(f)
        scale = Decimal(fm.one)
        values = edge_values(fm) + [random_value(fm, rng) for _ in range(300)]
        pairs = [(rng.choice(values), rng.choice(values)) for _ in range(1500)]
        for a, b in pairs:
            da, db = Decimal(a), Decimal(b)
            modulus = Decimal(1 << fm.bits)
            assert (Decimal(ref_add(fm, a, b)) - (da + db)) % modulus == 0, ("add", a, b)   # equal modulo 2^B
            assert (Decimal(ref_sub(fm, a, b)) - (da - db)) % modulus == 0, ("sub", a, b)
            assert fm.fits(ref_add(fm, a, b)) and fm.fits(ref_sub(fm, a, b))
            expected = (da * db / scale).to_integral_value(rounding=ROUND_DOWN)
            assert Decimal(ref_mul(fm, a, b)) == expected, ("mul", a, b)
            if b != 0:
                expected = (da * scale / db).to_integral_value(rounding=ROUND_DOWN)
                assert Decimal(ref_div(fm, a, b)) == expected, ("div", a, b)
            checked += 1
        for x in edge_doubles(fm) + [random_double(rng) for _ in range(300)]:
            expected = (Decimal(x) * scale).to_integral_value(rounding=ROUND_DOWN)   # Decimal(float) is exact
            assert Decimal(ref_f2x(fm, x)) == expected, ("f2x", x)
            checked += 1
        for v in values:
            assert ref_x2f(fm, v) == float(Decimal(v) / scale), ("x2f", v)
            checked += 1
    print("selfcheck OK (%d cases agree with the decimal module)" % checked)


# --------------------------------------------------------------------------- main
def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--formats", type=int, nargs="+", default=DEFAULT_FORMATS, help="fraction sizes in words")
    parser.add_argument("--ops", nargs="+", default=ALL_OPS, choices=ALL_OPS, help="operations to generate")
    parser.add_argument("--random", type=int, default=DEFAULT_RANDOM, help="random cases per operation")
    parser.add_argument("--seed", type=int, default=DEFAULT_SEED)
    parser.add_argument("--out-dir", type=Path, default=OUT_DIR)
    parser.add_argument("--check", action="store_true", help="do not write; fail if files differ from a fresh run")
    parser.add_argument("--selfcheck", action="store_true", help="cross-validate the reference formulas and exit")
    args = parser.parse_args()

    if args.selfcheck:
        selfcheck(args.seed)
        return 0

    stale = []
    for f in args.formats:
        text = generate(f, set(args.ops), args.seed, args.random)
        path = args.out_dir / ("fpa_f%d.txt" % f)
        if args.check:
            if not path.exists() or path.read_text() != text:
                stale.append(path)
        else:
            args.out_dir.mkdir(parents=True, exist_ok=True)
            path.write_text(text, newline="\n")
            print("wrote %s (%d lines)" % (path, text.count("\n")))
    if stale:
        print("stale or missing vector files (rerun generate_vectors.py):")
        for p in stale:
            print("  " + str(p))
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
