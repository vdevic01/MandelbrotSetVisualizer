#pragma once

// Shared helpers for the fixed-point tests.
//
// Nothing in here (or in the tests) hardcodes the number format: sizes are always derived from the
// fraction size F that a test is parameterised with, so the same tests keep working if the library is
// instantiated for more (or different) fraction sizes later.
//
// A "Number" is a std::vector<uint> of fpa::fpSize(F) words, most significant word first, two's
// complement - exactly the layout the library uses. Tests use heap vectors freely; only the library
// itself is required to avoid the heap.

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <initializer_list>
#include <random>
#include <string>
#include <utility>
#include <vector>

#include "FixedPointArithmetics.h"

namespace fpt {

using fpa::uint;
using Number = std::vector<uint>;

constexpr int kWordHexDigits = fpa::WORD_BITS / 4;
constexpr uint kAllOnes = ~uint(0);
constexpr uint kSignBit = uint(1) << fpa::SIGN_SHIFT;

// All fraction sizes the library supports; used to instantiate every parameterised suite.
inline std::vector<int> allFormats() {
    std::vector<int> formats;
    for (int f = fpa::MIN_FRACTION_PART; f <= fpa::MAX_FRACTION_PART; f++) formats.push_back(f);
    return formats;
}

inline std::string formatName(const ::testing::TestParamInfo<int>& info) {
    return "F" + std::to_string(info.param);
}

// Instantiates a TestWithParam<int> suite for every supported fraction size.
#define FPA_INSTANTIATE_FOR_ALL_FORMATS(Suite)                                                   \
    INSTANTIATE_TEST_SUITE_P(Formats, Suite, ::testing::ValuesIn(::fpt::allFormats()), ::fpt::formatName)

// ------------------------------------------------------------------------------------------ building
inline int words(int f) { return fpa::fpSize(f); }
inline Number filled(int f, uint pattern) { return Number(words(f), pattern); }
inline Number zero(int f) { return filled(f, 0); }

// The smallest positive step: 1 unit in the last place.
inline Number ulp(int f) { Number n = zero(f); n.back() = 1; return n; }
// 1.0: the whole word is 1, every fraction word is 0.
inline Number one(int f) { Number n = zero(f); n[0] = 1; return n; }
// Largest / smallest representable values.
inline Number maxValue(int f) { Number n = filled(f, kAllOnes); n[0] = kSignBit - 1; return n; }
inline Number minValue(int f) { Number n = zero(f); n[0] = kSignBit; return n; }

// Builds a number from the leading words (most significant first); missing words are zero.
inline Number make(int f, std::initializer_list<uint> leading) {
    Number n = zero(f);
    size_t i = 0;
    for (uint w : leading) { if (i < n.size()) n[i++] = w; }
    return n;
}

inline bool isNegative(const Number& n) { return (n[0] >> fpa::SIGN_SHIFT) != 0; }
inline bool isZero(const Number& n) { for (uint w : n) if (w) return false; return true; }

inline std::string hex(const Number& n) {
    std::string s;
    char buf[16];
    for (uint w : n) { std::snprintf(buf, sizeof buf, "%08x", w); s += buf; }
    return s;
}

// Parses a token of fpSize(f)*8 hex digits (most significant word first).
inline Number parseNumber(const std::string& token, int f) {
    if (token.size() != static_cast<size_t>(words(f)) * kWordHexDigits)
        throw std::runtime_error("number token has the wrong length for this format: " + token);
    Number n(words(f));
    for (int i = 0; i < words(f); i++)
        n[i] = static_cast<uint>(std::stoul(token.substr(static_cast<size_t>(i) * kWordHexDigits, kWordHexDigits), nullptr, 16));
    return n;
}

inline double parseDouble(const std::string& token) {
    const uint64_t bits = std::stoull(token, nullptr, 16);
    double d;
    static_assert(sizeof d == sizeof bits, "double must be 64 bits");
    std::memcpy(&d, &bits, sizeof d);
    return d;
}

// ------------------------------------------------------------------------------------------ calling
using BinaryFn = void (*)(const uint*, const uint*, uint*, int);
using UnaryFn = void (*)(const uint*, uint*, int);

// Result of op(a, b) written into a freshly filled output buffer.
inline Number apply(BinaryFn op, int f, const Number& a, const Number& b, uint outputFill = 0) {
    Number out = filled(f, outputFill);
    op(a.data(), b.data(), out.data(), f);
    return out;
}
inline Number apply(UnaryFn op, int f, const Number& a, uint outputFill = 0) {
    Number out = filled(f, outputFill);
    op(a.data(), out.data(), f);
    return out;
}

inline Number add(int f, const Number& a, const Number& b) { return apply(fpa::addFixed, f, a, b); }
inline Number sub(int f, const Number& a, const Number& b) { return apply(fpa::subFixed, f, a, b); }
inline Number mul(int f, const Number& a, const Number& b) { return apply(fpa::mulCmplFixed, f, a, b); }
inline Number sqr(int f, const Number& a) { return apply(fpa::sqrCmplFixed, f, a); }
inline Number div(int f, const Number& a, const Number& b) { return apply(fpa::divFixed, f, a, b); }
inline Number inc(int f, const Number& a) { return apply(fpa::incFixed, f, a); }
inline Number neg(int f, const Number& a) { return apply(fpa::cmplFixed, f, a); }
inline bool gt(int f, const Number& a, const Number& b) { return fpa::gtFixed(a.data(), b.data(), f); }
inline bool gte(int f, const Number& a, const Number& b) { return fpa::gteFixed(a.data(), b.data(), f); }
inline Number fromDouble(int f, double d) { Number n = zero(f); fpa::floatingToFixedPoint(d, n.data(), f); return n; }
inline double toDouble(int f, const Number& a) { return fpa::fixedToFloatingPoint(a.data(), f); }

inline Number absValue(int f, const Number& a) { return isNegative(a) ? neg(f, a) : a; }

// True when |a - b| <= maxUlps units in the last place. Assumes a and b are close enough that the
// subtraction does not wrap (all callers use values far from the limits of the range).
inline bool withinUlps(int f, const Number& a, const Number& b, uint maxUlps) {
    const Number d = absValue(f, sub(f, a, b));
    for (size_t i = 0; i + 1 < d.size(); i++)
        if (d[i] != 0) return false;
    return d.back() <= maxUlps;
}

// Comparison with a readable failure message (use with EXPECT_TRUE / ASSERT_TRUE).
inline ::testing::AssertionResult same(const Number& expected, const Number& actual) {
    if (expected == actual) return ::testing::AssertionSuccess();
    return ::testing::AssertionFailure() << "\n  expected " << hex(expected) << "\n  actual   " << hex(actual);
}

// ------------------------------------------------------------------------------------------ random
// Deterministic random numbers (fixed seeds make failures reproducible).
class Rng {
public:
    explicit Rng(uint64_t seed) : gen_(seed) {}

    uint word() { return static_cast<uint>(gen_()); }
    int below(int n) { return static_cast<int>(gen_() % static_cast<uint64_t>(n)); }
    bool coin() { return gen_() & 1u; }

    // Random value with a random number of significant bits (so small and large magnitudes are both
    // common), sometimes assembled from "interesting" words (0, 1, 0x7FFFFFFF, 0x80000000, 0xFFFFFFFF).
    Number any(int f) {
        const int n = words(f);
        Number v(n);
        if (below(4) == 0) {
            static const uint specials[] = {0u, 1u, kSignBit - 1, kSignBit, kAllOnes, kAllOnes - 1};
            for (auto& w : v) w = coin() ? specials[below(6)] : word();
            return v;
        }
        for (auto& w : v) w = word();
        int leadingZeroBits = below(n * fpa::WORD_BITS);      // clear this many top bits
        for (int w = 0; w < n && leadingZeroBits > 0; w++) {
            if (leadingZeroBits >= fpa::WORD_BITS) { v[w] = 0; leadingZeroBits -= fpa::WORD_BITS; }
            else { v[w] &= kAllOnes >> leadingZeroBits; leadingZeroBits = 0; }
        }
        return coin() ? neg(f, v) : v;
    }

    // Value whose magnitude is below 2^wholeBits (wholeBits <= 30) with a random fraction and sign.
    Number small(int f, int wholeBits) {
        Number v(words(f));
        for (auto& w : v) w = word();
        v[0] = wholeBits == 0 ? 0 : word() & (kAllOnes >> (fpa::WORD_BITS - wholeBits));
        return coin() ? neg(f, v) : v;
    }

    Number nonZero(int f, int wholeBits) {
        for (;;) { Number v = small(f, wholeBits); if (!isZero(v)) return v; }
    }

private:
    std::mt19937_64 gen_;
};

constexpr int kIterations = 400;   // random iterations per property test

}  // namespace fpt
