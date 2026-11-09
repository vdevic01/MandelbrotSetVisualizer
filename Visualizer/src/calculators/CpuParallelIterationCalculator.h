#pragma once

#include "IterationCalculator.h"

class CpuParallelIterationCalculator : public IterationCalculator {
public:
    void calculate(const std::vector<Complex>& points, EscapeResult& result, unsigned int maxIter, double escapeRadius) const override;
    void calculate(const HpPoints& points, EscapeResult& result, unsigned int maxIter, double escapeRadius) const override;
};
