#pragma once

#ifndef CALCULATE_ITERS_H
#define CALCULATE_ITERS_H

struct Complex {
    double real;
    double imag;
};

struct ComplexHP {
    unsigned int real[4]; // One 4 bytes for whole part and 12 bytes for fraction part, using big endian
    unsigned int imag[4];
};

int calculateIters(Complex* points, int* iters, unsigned int size, unsigned int max_iter);
int calculateItersHighPrecision(ComplexHP* points, int* iters, unsigned int size, unsigned int max_iter);
#endif
