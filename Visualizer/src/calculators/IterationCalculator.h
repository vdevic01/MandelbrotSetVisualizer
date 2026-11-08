#pragma once

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

class IterationCalculator {
public:
    virtual ~IterationCalculator() = default;

    virtual void calculate(const std::vector<Complex>& points, std::vector<int>& iters, unsigned int maxIter) const = 0;
    virtual void calculate(const HpPoints& points, std::vector<int>& iters, unsigned int maxIter) const = 0;
};

#endif // ITERATION_CALCULATOR_H