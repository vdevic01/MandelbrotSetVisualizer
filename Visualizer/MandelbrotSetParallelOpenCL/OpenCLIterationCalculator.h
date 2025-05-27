#pragma once

#include "IterationCalculator.h"

#ifndef OPENCL_ITERATION_CALCULATOR_H
#define OPENCL_ITERATION_CALCULATOR_H

class OpenCLIterationCalculator : public IterationCalculator {
public:
    int calculate(const std::vector<Complex>& points, std::vector<int>& iters, const unsigned int maxIter) const override;
    int calculate(const std::vector<ComplexHP>& points, std::vector<int>& iters, const unsigned int maxIter) const override;
};

#endif // OPENCL_ITERATION_CALCULATOR_H