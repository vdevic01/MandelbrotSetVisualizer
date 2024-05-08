#pragma once

#ifndef CALCULATE_ITERS_H
#define CALCULATE_ITERS_H

struct Complex {
    double real;
    double imag;
};

int calculateIters(struct Complex* c, int* max_iters, unsigned int width, unsigned int height);

#endif
