#pragma once

#include <vector>

#include "IterationCalculator.h"

// Smooth value of a point that never escaped (every escaped point has a value >= 0).
constexpr float SMOOTH_NOT_ESCAPED = -1.0f;

// Turns the escape iteration and the escape value of every sample into one continuous ("smooth") value:
//
//     smooth = iteration + 1 - log2( ln|z| / ln R )
//
// where |z| is the magnitude at escape and R is the escape radius. The escape iteration alone is a whole
// number, which gives visible bands of colour. The correction term says how far past the escape radius the
// point landed: it is 0 when |z| = R and 1 when |z| = R^2, so the value changes continuously from one
// iteration band to the next. A larger escape radius makes the result smoother.
std::vector<float> computeSmoothValues(const EscapeResult& escape, double escapeRadius);
