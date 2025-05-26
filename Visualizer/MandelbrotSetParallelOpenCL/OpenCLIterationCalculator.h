#pragma once

#include "IterationCalculator.h"

#ifndef OPENCL_ITERATION_CALCULATOR_H
#define OPENCL_ITERATION_CALCULATOR_H

class OpenCLIterationCalculator : public IterationCalculator {
public:
    // Override the specific calculateIters functions for Complex and ComplexHP
    int calculate(const std::vector<Complex>& points, std::vector<int>& iters, const unsigned int size, const unsigned int max_iter, const char* kernelFilename, const char* compile_options = NULL) const override;
    int calculate(const std::vector<ComplexHP>& points, std::vector<int>& iters, const unsigned int size, const unsigned int max_iter, const char* kernelFilename, const char* compile_options = NULL) const override;
};

#endif // OPENCL_ITERATION_CALCULATOR_H