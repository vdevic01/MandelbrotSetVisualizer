#pragma once

#include "IterationCalculator.h"

class SequentialIterationCalculator : public IterationCalculator {
public:
    int calculate(const std::vector<Complex>& points, std::vector<int>& iters, unsigned int maxIter) const override;
    int calculate(const std::vector<ComplexHP>& points, std::vector<int>& iters, unsigned int maxIter) const override;
};
