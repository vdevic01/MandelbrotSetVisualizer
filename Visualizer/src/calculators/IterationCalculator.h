#pragma once

#include <cstddef>
#include <vector>
#include <string>

#ifndef ITERATION_CALCULATOR_H
#define ITERATION_CALCULATOR_H

struct Complex {
    double real;
    double imag;
};

// Sample points in fixed-point format (high precision). Every number has 1 whole word plus `fractionWords`
// fraction words, most significant word first, two's complement. The points are stored back to back as
// [real words][imaginary words] per point, in one flat array (the GPU backends copy it as it is).
struct HpPoints {
    int fractionWords = 0;
    std::vector<unsigned int> words;

    size_t wordsPerNumber() const { return 1 + static_cast<size_t>(fractionWords); }
    size_t size() const { return fractionWords > 0 ? words.size() / (2 * wordsPerNumber()) : 0; }

    unsigned int* real(size_t i) { return words.data() + i * 2 * wordsPerNumber(); }
    unsigned int* imag(size_t i) { return real(i) + wordsPerNumber(); }
    const unsigned int* real(size_t i) const { return words.data() + i * 2 * wordsPerNumber(); }
    const unsigned int* imag(size_t i) const { return real(i) + wordsPerNumber(); }
};

// Escape radius R: a point has escaped once |z| is greater than R. 2 is the smallest correct value; larger
// values give smoother colouring. The upper limit keeps |z|^2 inside the single whole word of the
// fixed-point format: |z|^2 at escape is at most (R^2 + 2)^2, which must stay below 2^31.
constexpr double DEFAULT_ESCAPE_RADIUS = 2.0;
constexpr double MIN_ESCAPE_RADIUS = 2.0;
constexpr double MAX_ESCAPE_RADIUS = 128.0;

constexpr bool isSupportedEscapeRadius(double radius) {
    return radius >= MIN_ESCAPE_RADIUS && radius <= MAX_ESCAPE_RADIUS;
}

// What the escape-time calculation returns, one entry per sample point.
struct EscapeResult {
    std::vector<int> iters;      // iteration at which the point escaped, or -1 if it never did
    std::vector<float> magSq;    // the escape value: |z|^2 at that iteration (0 if the point never escaped)

    EscapeResult() = default;
    explicit EscapeResult(size_t count) : iters(count), magSq(count) {}

    size_t size() const { return iters.size(); }
    void resize(size_t count) { iters.resize(count); magSq.resize(count); }
};

// The escape threshold R^2 for the fixed-point kernels, as the whole word and the first fraction word of a
// fixed-point number (the remaining fraction words are 0).
inline void escapeThresholdWords(double escapeRadius, unsigned int& whole, unsigned int& fraction) {
    const double threshold = escapeRadius * escapeRadius;
    whole = static_cast<unsigned int>(threshold);
    fraction = static_cast<unsigned int>((threshold - whole) * 4294967296.0);
}

class IterationCalculator {
public:
    virtual ~IterationCalculator() = default;

    // `result` must already have one entry per point.
    virtual void calculate(const std::vector<Complex>& points, EscapeResult& result, unsigned int maxIter, double escapeRadius) const = 0;
    virtual void calculate(const HpPoints& points, EscapeResult& result, unsigned int maxIter, double escapeRadius) const = 0;
};

#endif // ITERATION_CALCULATOR_H