#pragma once

#include <vector>
#include "IterationCalculator.h"
#include "FixedPointArithmetics.h"

std::vector<Complex> samplePointsFromComplexPlane(
    int imageHeight, int imageWidth,
    double imStart, double imEnd,
    double reStart, double reEnd,
    int samples);

// High precision: the boundaries are numbers of fpa::fpSize(fractionWords) words each.
// Throws std::invalid_argument for an unsupported fractionWords.
HpPoints samplePointsFromComplexPlane(
    int imageHeight, int imageWidth,
    const fpa::uint* imStart, const fpa::uint* imEnd,
    const fpa::uint* reStart, const fpa::uint* reEnd,
    int samples, int fractionWords);
