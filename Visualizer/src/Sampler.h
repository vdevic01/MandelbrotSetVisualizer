#pragma once

#include <vector>
#include "IterationCalculator.h"
#include "FixedPointArithmetics.h"

std::vector<Complex> samplePointsFromComplexPlane(
    int imageHeight, int imageWidth,
    double imStart, double imEnd,
    double reStart, double reEnd,
    int samples);

std::vector<ComplexHP> samplePointsFromComplexPlane(
    int imageHeight, int imageWidth,
    const fpa::uint imStart[fpa::FP_SIZE], const fpa::uint imEnd[fpa::FP_SIZE],
    const fpa::uint reStart[fpa::FP_SIZE], const fpa::uint reEnd[fpa::FP_SIZE],
    int samples);
