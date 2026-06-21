#pragma once

#include "IterationCalculator.h"

#ifndef OPENCL_ITERATION_CALCULATOR_H
#define OPENCL_ITERATION_CALCULATOR_H

class OpenCLIterationCalculator : public IterationCalculator {
public:
    void calculate(const std::vector<Complex>& points, std::vector<int>& iters, unsigned int maxIter) const override;
    void calculate(const std::vector<ComplexHP>& points, std::vector<int>& iters, unsigned int maxIter) const override;
};

#endif // OPENCL_ITERATION_CALCULATOR_H