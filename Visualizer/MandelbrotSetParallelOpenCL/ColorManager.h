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
    virtual vector<Color> paint(vector<int>& iters) const = 0;
protected:
    ColorManager(int imageSize) {
        this->imageSize = imageSize;
    }
	int imageSize;
};

class CyclicColorPalette : public ColorManager {
public:
    CyclicColorPalette(int imageSize, vector<Color> colors, int length);
    vector<Color> paint(vector<int>& iters) const override;

private:
    vector<Color> colors;
    double length;
};

class HistogramColorPalette : public ColorManager {
public:
    HistogramColorPalette(int imageSize, int maxIter, vector<Color> colors);
    vector<Color> paint(vector<int>& iters) const override;
private:
    Color interpolateColor(Color& l, Color& r, double val);
    int maxIter;
    vector<Color> colors;
};

class ExponentialColorPalette : public ColorManager {
public:
    ExponentialColorPalette(int imageSize, int maxIter, vector<Color> colors, int length);
    vector<Color> paint(vector<int>& iters) const override;
private:
    int maxIter;
    vector<Color> colors;
    double length;
};
#endif