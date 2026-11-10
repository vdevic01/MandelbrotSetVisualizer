#pragma once

#include <cmath>

// Smooth value of a point that never escaped (every escaped point has a value >= 0).
constexpr float SMOOTH_NOT_ESCAPED = -1.0f;

// Turns the escape iteration and the escape value of a sample into one continuous ("smooth") value:
//
//     smooth = iteration + 1 - log2( ln|z| / ln R )
//
// where |z| is the magnitude at escape and R is the escape radius. The escape iteration alone is a whole
// number, which gives visible bands of colour. The correction term says how far past the escape radius the
// point landed: it is 0 when |z| = R and 1 when |z| = R^2, so the value changes continuously from one
// iteration band to the next. A larger escape radius makes the result smoother.
//
// It is evaluated for every sample while painting, in float: ln|z| / ln R == ln(|z|^2) / ln(R^2), and
// log2(a / b) == log2(a) - log2(b), where log2(ln(R^2)) is the same for every sample (smoothOffset).
// That leaves two logarithms per sample and no division.

// Spreads the colours of a smooth value: near the set the values are packed into a narrow range, so most of
// the pixels would get the same few colours. Values up to a knee are left alone (they are already spread
// well); above it the value is compressed with a logarithm,
//
//     spread(v) = v                                  for v <= knee
//     spread(v) = knee + ln(1 + s * (v - knee)) / s  for v >  knee
//
// The two parts meet with the same slope at the knee, so there is no visible seam. s = 0 leaves the value
// unchanged; a larger s compresses the high values more.
inline float spreadValue(float value, float strength, float knee) {
    if (strength <= 0.0f || value <= knee) return value;
    return knee + std::log1p(strength * (value - knee)) / strength;
}

// The part of the formula that depends only on the escape radius. Compute it once per render.
inline float smoothOffset(double escapeRadius) {
    return static_cast<float>(std::log2(std::log(escapeRadius * escapeRadius)));
}

// iter: escape iteration (-1 if the sample never escaped). magSq: |z|^2 at escape.
inline float smoothValue(int iter, float magSq, float offset) {
    if (iter < 0) return SMOOTH_NOT_ESCAPED;
    // magSq is above R^2 >= 4 for a point that escaped; the guards only protect against bad input
    // and float rounding.
    float correction = magSq > 1.0f ? std::log2(std::log(magSq)) - offset : 0.0f;
    if (correction < 0.0f) correction = 0.0f;
    const float value = static_cast<float>(iter) + 1.0f - correction;
    // A point far outside the set can overshoot R^2 in a single step; keep the value non-negative.
    return value > 0.0f ? value : 0.0f;
}
