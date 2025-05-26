#pragma once

#include <vector>
#include <string>

#ifndef ITERATION_CALCULATOR_H
#define ITERATION_CALCULATOR_H

struct Complex {
    double real;
    double imag;
};

struct ComplexHP {
    unsigned int real[4]; // 4 bytes for whole part and 12 bytes for fraction part, using big endian
    unsigned int imag[4];
};

class IterationCalculator {
public:
    virtual ~IterationCalculator() = default;

    virtual int calculate(const std::vector<Complex>& points, std::vector<int>& iters, const unsigned int size, const unsigned int max_iter) const = 0;
    virtual int calculate(const std::vector<ComplexHP>& points, std::vector<int>& iters, const unsigned int size, const unsigned int max_iter) const = 0;
};

#endif // ITERATION_CALCULATOR_H