#pragma once

#include "IterationCalculator.h"

#ifndef CUDA_ITERATION_CALCULATOR_H
#define CUDA_ITERATION_CALCULATOR_H

class CUDAIterationCalculator : public IterationCalculator {
public:
    void calculate(const std::vector<Complex>& points, std::vector<int>& iters, unsigned int maxIter) const override;
    void calculate(const std::vector<ComplexHP>& points, std::vector<int>& iters, unsigned int maxIter) const override;
};

#endif // CUDA_ITERATION_CALCULATOR_H
