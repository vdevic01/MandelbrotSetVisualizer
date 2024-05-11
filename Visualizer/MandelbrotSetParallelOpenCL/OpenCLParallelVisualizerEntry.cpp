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
double RE_START = -0.153004885037500013708;
double RE_END = -0.152809695287500013708;
double IM_START = 1.039611370300000000002;
double IM_END = 1.039757762612500000002;

int MAX_ITER = 700;

//int IMAGE_WIDTH = 6144;
//int IMAGE_HEIGHT = 4096;
int IMAGE_WIDTH = 900;
int IMAGE_HEIGHT = 600;
int IMAGE_SIZE = IMAGE_WIDTH * IMAGE_HEIGHT;

int PALETTE_LENGTH = 150;

string OUTPUT_FILENAME = "./mandelbrot_set.png";

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
        this->colors = move(colors);
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
    uchar* imageData = image.data;

    for (int y = 0; y < IMAGE_HEIGHT; ++y) {
        uchar* rowPtr = imageData + (IMAGE_HEIGHT - y - 1) * image.step;

        for (int x = 0; x < IMAGE_WIDTH; ++x) {
            int pixelIndex = y * IMAGE_WIDTH + x;
            Color pixel_value = pixels[pixelIndex];

            uchar* pixelPtr = rowPtr + x * 3;

            pixelPtr[0] = pixel_value.blue;
            pixelPtr[1] = pixel_value.green;
            pixelPtr[2] = pixel_value.red;
        }
    }

    cv::imwrite(OUTPUT_FILENAME, image);
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
    auto* points = new Complex[IMAGE_HEIGHT * IMAGE_WIDTH];
    auto* iters = new int[IMAGE_HEIGHT * IMAGE_WIDTH];

    vector<Color> colors;

    Color c1{};
    c1.red = 7;
    c1.green = 6;
    c1.blue = 38;
    Color c2{};
    c2.red = 140;
    c2.green = 143;
    c2.blue = 213;

    colors.push_back(c1);
    colors.push_back(c2);
    colors.push_back(c1);
    ColorPalette palette(colors, PALETTE_LENGTH);

    auto start = chrono::high_resolution_clock::now();
    for (int i = 0; i < IMAGE_HEIGHT; i++) {
        double imaginaryPart = mapVal(i, 0, IMAGE_HEIGHT, IM_START, IM_END);
        for (int j = 0; j < IMAGE_WIDTH; j++) {
            double realPart = mapVal(j, 0, IMAGE_WIDTH, RE_START, RE_END);
            int idx = j + (i * IMAGE_WIDTH);
            points[idx] = { realPart, imaginaryPart };
        }
    }
    auto end = chrono::high_resolution_clock::now();
    auto duration = chrono::duration_cast<chrono::milliseconds>(end - start);
    cout << "Pixel mapping: " << duration.count() << " ms\n\n";
    start = chrono::high_resolution_clock::now();

    calculateIters(points, iters, IMAGE_SIZE, MAX_ITER);

    end = chrono::high_resolution_clock::now();
    duration = chrono::duration_cast<chrono::milliseconds>(end - start);
    cout << "\nCalculating escape iteration: " << duration.count() << " ms" << endl;
    start = chrono::high_resolution_clock::now();

    delete[] points;

    auto* pixels = new Color[IMAGE_SIZE];

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

    end = chrono::high_resolution_clock::now();
    duration = chrono::duration_cast<chrono::milliseconds>(end - start);
    cout << "Coloring: " << duration.count() << " ms" << endl;
    start = chrono::high_resolution_clock::now();

    delete[] iters;

    createColorImage(pixels);

    delete[] pixels;

    end = chrono::high_resolution_clock::now();
    duration = chrono::duration_cast<chrono::milliseconds>(end - start);
    cout << "Image building: " << duration.count() << " ms" << endl;

}

// Command line arguments:
// RE_START, RE_END, IM_START, IM_END, IMG_WIDTH, IMG_HEIGHT, MAX_ITER, OUTPUT_FILENAME
int main(int argc, char* argv[]) {
    if (argc > 1) {
        try {
            RE_START = stod(argv[1]);
        }
        catch (const invalid_argument& e) {
            return 1;
        }
        try {
            RE_END = stod(argv[2]);
        }
        catch (const invalid_argument& e) {
            return 1;
        }
        try {
            IM_START = stod(argv[3]);
        }
        catch (const invalid_argument& e) {
            return 1;
        }
        try {
            IM_END = stod(argv[4]);
        }
        catch (const invalid_argument& e) {
            return 1;
        }
        try {
            OUTPUT_FILENAME = argv[5];
        }
        catch (const invalid_argument& e) {
            return 1;
        }
    }
    createMandelbrotSet();
}