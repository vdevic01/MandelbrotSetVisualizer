#include <complex>
#include <opencv2/opencv.hpp>
#include <utility>
#include <vector>
#include <chrono>

#include "OpenCLWrapper.h"

using namespace std;

//const double RE_START = -2.0;
//const double RE_END = 1.0;
//const double IM_START = -1;
//const double IM_END = 1;
const double RE_START = -0.153004885037500013708;
const double RE_END = -0.152809695287500013708;
const double IM_START = 1.039611370300000000002;
const double IM_END = 1.039757762612500000002;

const int MAX_ITER = 700;

const int IMAGE_WIDTH = 6144;
const int IMAGE_HEIGHT = 4096;
const int IMAGE_SIZE = IMAGE_WIDTH * IMAGE_HEIGHT;

const int PALETTE_LENGTH = 150;

struct Color {
    unsigned char red;
    unsigned char green;
    unsigned char blue;
};

class ColorPalette {
public:
    vector<Color> colors;
    double length;

    ColorPalette(vector<Color> colors, int length) {
        this->colors = std::move(colors);
        this->length = length;
    }

    Color fit(int val) {
        auto valAdj = (double)(val % (int)this->length);
        const int N = this->colors.size();
        const double STEP = this->length / (N - 1);
        Color* left = nullptr;
        Color* right = nullptr;
        double ratio;
        for (int i = 0; i < N; i++) {
            if (STEP * i > valAdj) {
                left = &this->colors[i - 1];
                right = &this->colors[i];
                ratio = (valAdj - STEP * (i - 1)) / STEP;
                break;
            }
        }
        if (left == nullptr || right == nullptr) {
            exit(2);
        }
        Color output{};
        output.red = (int)(left->red + (right->red - left->red) * ratio);
        output.green = (int)(left->green + (right->green - left->green) * ratio);
        output.blue = (int)(left->blue + (right->blue - left->blue) * ratio);
        return output;
    }
};

void createColorImage(Color* pixels) {
    cv::Mat image(IMAGE_HEIGHT, IMAGE_WIDTH, CV_8UC3);

    for (int y = 0; y < IMAGE_HEIGHT; ++y) {
        for (int x = 0; x < IMAGE_WIDTH; ++x) {
            Color pixel_value = pixels[y * IMAGE_WIDTH + x];

            auto& pixel = image.at<cv::Vec3b>(y, x); // Access the pixel at (x, y)
            pixel[0] = pixel_value.blue; // Blue channel
            pixel[1] = pixel_value.green; // Green channel
            pixel[2] = pixel_value.red; // Red channel
        }
    }

    cv::imwrite("mandelbrot_set.tiff", image);
}

double mapVal(double value, double inMin, double inMax, double outMin, double outMax) {
    return (value - inMin) * (outMax - outMin) / (inMax - inMin) + outMin;
}

int complexPointIter(complex<double>& c) {
    complex<double> z(0, 0);
    for (int i = 0; i < MAX_ITER; i++) {
        z = z * z + c;
        if (abs(z) > 2) {
            return i;
        }
    }
    return -1;
}

void createMandelbrotSet() {
    auto start = chrono::high_resolution_clock::now();  
    auto* points = new Complex[IMAGE_HEIGHT * IMAGE_WIDTH];
    auto* iters = new int[IMAGE_HEIGHT * IMAGE_WIDTH];

    vector<Color> colors;

    Color c1{};
    c1.red = 7;
    c1.green = 6;
    c1.blue = 38;
    Color c2{};
    c2.red = 240;
    c2.green = 43;
    c2.blue = 213;

    //Color c1;
    //c1.red = 20;
    //c1.green = 170;
    //c1.blue = 20;
    //Color c2;
    //c2.red = 255;
    //c2.green = 255;
    //c2.blue = 255;

    colors.push_back(c2);
    colors.push_back(c1);
    colors.push_back(c2);
    ColorPalette palette(colors, PALETTE_LENGTH);

    for (int i = 0; i < IMAGE_HEIGHT; i++) {
        double imaginaryPart = mapVal(i, 0, IMAGE_HEIGHT, IM_START, IM_END);
        for (int j = 0; j < IMAGE_WIDTH; j++) {
            double realPart = mapVal(j, 0, IMAGE_WIDTH, RE_START, RE_END);
            int idx = j + (i * IMAGE_WIDTH);
            points[idx] = { realPart, imaginaryPart };
        }
    }
    calculateIters(points, iters, IMAGE_SIZE, MAX_ITER);

    delete[] points;

    auto* pixels = new Color[IMAGE_HEIGHT * IMAGE_WIDTH];

    for (int i = 0; i < IMAGE_SIZE; i++) {
        if (iters[i] == -1) {
            Color black{};
            black.red = 0;
            black.green = 0;
            black.blue = 0;
            pixels[i] = black;
        }
        else {
            pixels[i] = palette.fit(iters[i]);
        }
    }

    delete[] iters;

    createColorImage(pixels);

    delete[] pixels;

    auto end = chrono::high_resolution_clock::now();
    auto duration = chrono::duration_cast<chrono::seconds>(end - start);
    std::cout << "Time taken: " << duration.count() << " seconds" << std::endl;

}

int main() {
    createMandelbrotSet();
}