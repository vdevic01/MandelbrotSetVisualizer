#pragma once

#include <vector>

#include "IterationCalculator.h"

struct Color {
    unsigned char red;
    unsigned char green;
    unsigned char blue;
};

// paint() takes the escape result of every sample and the escape radius it was calculated with. The colour
// is picked from the smooth value of the sample (see SmoothColoring.h); a sample that never escaped is black.
class ColorManager {
public:
    [[nodiscard]] virtual std::vector<Color> paint(const EscapeResult& escape, double escapeRadius) const = 0;
    virtual ~ColorManager() = default;
protected:
    ColorManager(int imageSize, int samples) : imageSize(imageSize), samples(samples) {}
    int imageSize;
    int samples;
};

class CyclicColorPalette : public ColorManager {
public:
    // colorSpread: strength of the logarithmic spread of the colours (0 = linear, see spreadValue).
    CyclicColorPalette(int imageSize, std::vector<Color> colors, int length, int samples, double colorSpread = 0.0);
    [[nodiscard]] std::vector<Color> paint(const EscapeResult& escape, double escapeRadius) const override;
private:
    std::vector<Color> colors;
    double length;
    float colorSpread;
};

class HistogramColorPalette : public ColorManager {
public:
    // Colours by histogram equalisation of the smooth values (see paint). length is the resolution of the palette.
    // phase (0..1) is where in the palette the first sample starts; the palette wraps around from its end to its start.
    // cycles is how many times the palette repeats from the lowest to the highest value.
    HistogramColorPalette(int imageSize, std::vector<Color> colors, int length, int samples, double phase = 0.0, double cycles = 1.0);
    [[nodiscard]] std::vector<Color> paint(const EscapeResult& escape, double escapeRadius) const override;
private:
    std::vector<Color> colors;
    double length;
    double phase;
    double cycles;
};
