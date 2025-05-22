#pragma once

#include <vector>

#ifndef CALCULATE_ITERS_H
#define CALCULATE_ITERS_H

struct Complex {
    double real;
    double imag;
};

struct ComplexHP {
    unsigned int real[4]; // 4 bytes for whole part and 12 bytes for fraction part, using big endian
    unsigned int imag[4];
};

int calculateIters(const std::vector<Complex>& points, std::vector<int>& iters, const unsigned int size, const unsigned int max_iter);
int calculateItersHighPrecision(const std::vector<ComplexHP>& points, std::vector<int>& iters, const unsigned int size, const unsigned int max_iter);
#endif
