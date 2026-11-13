#pragma once

#include <string>

#include "IterationCalculator.h"

// The escape results of a render, saved so that the image can be painted again with other colour settings
// without iterating again. Everything the colouring needs is in the file: the image size, the samples per
// pixel and the escape radius the values were calculated with.
struct EscapeData {
    int width = 0;
    int height = 0;
    int samples = 1;
    double escapeRadius = DEFAULT_ESCAPE_RADIUS;
    EscapeResult escape;
};

// Both throw std::runtime_error with a message for the user when the file cannot be written, read or is not valid.
void saveEscapeFile(const std::string& path, const EscapeData& data);
EscapeData loadEscapeFile(const std::string& path);
