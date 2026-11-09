#pragma once

#include "IterationCalculator.h"

class CpuParallelIterationCalculator : public IterationCalculator {
public:
    void calculate(const std::vector<Complex>& points, std::vector<int>& iters, unsigned int maxIter, double escapeRadius) const override;
    void calculate(const HpPoints& points, std::vector<int>& iters, unsigned int maxIter, double escapeRadius) const override;
};
