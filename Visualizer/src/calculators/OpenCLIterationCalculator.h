#pragma once

#include "IterationCalculator.h"

#ifndef OPENCL_ITERATION_CALCULATOR_H
#define OPENCL_ITERATION_CALCULATOR_H

class OpenCLIterationCalculator : public IterationCalculator {
public:
    void calculate(const std::vector<Complex>& points, std::vector<int>& iters, unsigned int maxIter, double escapeRadius) const override;
    void calculate(const HpPoints& points, std::vector<int>& iters, unsigned int maxIter, double escapeRadius) const override;
};

#endif // OPENCL_ITERATION_CALCULATOR_H