#pragma once

#ifndef COLOR_MANAGER
#define COLOR_MANAGER

#include <vector>

using namespace std;

struct Color {
    unsigned char red;
    unsigned char green;
    unsigned char blue;
};

class ColorManager {
public:
    virtual void paint(int* iters, Color pixels[]) = 0;
protected:
    ColorManager(int imageSize) {
        this->imageSize = imageSize;
    }
	int imageSize;
};

class CyclicColorPalette : public ColorManager {
public:
    CyclicColorPalette(int imageSize, vector<Color> colors, int length);
    void paint(int* iters, Color pixels[]) override;

private:
    vector<Color> colors;
    double length;
};

class HistogramColorPalette : public ColorManager {
public:
    HistogramColorPalette(int imageSize, int maxIter, vector<Color> colors);
    void paint(int* iters, Color pixels[]) override;
private:
    Color interpolateColor(Color& l, Color& r, double val);
    int maxIter;
    vector<Color> colors;
};

class ExponentialColorPalette : public ColorManager {
public:
    ExponentialColorPalette(int imageSize, int maxIter, vector<Color> colors, int length);
    void paint(int* iters, Color pixels[]) override;
private:
    int maxIter;
    vector<Color> colors;
    double length;
};
#endif