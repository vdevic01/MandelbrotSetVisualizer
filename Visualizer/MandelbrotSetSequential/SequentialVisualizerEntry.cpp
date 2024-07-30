#include <complex>
#include <opencv2/opencv.hpp>
#include <utility>
#include <vector>
#include <iomanip>

using namespace std;
//const double RE_START = -2.0;
//const double RE_END = 2.0;
//const double IM_START = -2;
//const double IM_END = 2;
double RE_START = -0.153004885037500013708;
double RE_END = -0.152809695287500013708;
double IM_START = 1.039611370300000000002;
double IM_END = 1.039757762612500000002;

const int MAX_ITER = 500;

const int IMAGE_WIDTH = 1000;
const int IMAGE_HEIGHT = 1000;

const int PALETTE_LENGTH = 130;

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

void createGrayscaleImage(const int* pixels) {
    cv::Mat image(IMAGE_HEIGHT, IMAGE_WIDTH, CV_8UC1);

    for (int y = 0; y < IMAGE_HEIGHT; ++y) {
        for (int x = 0; x < IMAGE_WIDTH; ++x) {
            int pixel_value = pixels[y * IMAGE_WIDTH + x];
            image.at<uchar>(y, x) = pixel_value;
        }
    }

    cv::imwrite("mandelbrot_set.tiff", image);
    //cv::imshow("Grayscale Image", image);
    //cv::waitKey(0);
}

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

int calculateEscapeIterOptimized(complex<double>& c) {
    double x0 = c.real();
    double y0 = c.imag();

    double x2 = 0;
    double y2 = 0;

    double x = 0;
    double y = 0;

    for (int i = 0; i < MAX_ITER; i++) {
        y = (x + x) * y + y0;
        x = x2 - y2 + x0;
        x2 = x * x;
        y2 = y * y;
        if (x2 + y2 > 4) {
            return i;
        }
    }
    return -1;
}

void createMandelbrotSet() {
    auto* pixels = new Color[IMAGE_HEIGHT * IMAGE_WIDTH];

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

    colors.push_back(c1);
    colors.push_back(c2);
    colors.push_back(c1);
    ColorPalette palette(colors, PALETTE_LENGTH);

    chrono::steady_clock::time_point start;
    chrono::steady_clock::time_point end;
    chrono::steady_clock::time_point startX;
    chrono::steady_clock::time_point endX;
    
    start = chrono::high_resolution_clock::now();
    unsigned long long totalTime = 0;
    for (int i = 0; i < IMAGE_HEIGHT; i++) {
        for (int j = 0; j < IMAGE_WIDTH; j++) {
            double realPart = mapVal(j, 0, IMAGE_WIDTH, RE_START, RE_END);
            double imaginaryPart = mapVal(i, 0, IMAGE_HEIGHT, IM_START, IM_END);
            complex<double> point(realPart, imaginaryPart);
            startX = chrono::high_resolution_clock::now();
            int totalIters = complexPointIterOptimized(point);
            endX = chrono::high_resolution_clock::now();
            totalTime += chrono::duration_cast<chrono::nanoseconds>(endX - startX).count();
            int idx = j + (i * IMAGE_WIDTH);
            if (totalIters == -1) {
                Color black{};
                black.red = 0;
                black.green = 0;
                black.blue = 0;
                pixels[idx] = black;
            }
            else {
                pixels[idx] = palette.fit(totalIters);
            }     
        }
    }
    createColorImage(pixels);
    end = chrono::high_resolution_clock::now();
    std::cout << setprecision(16) << "Pixel mapping: " << totalTime << " ns\n\n";
    delete[] pixels;
}

void createColorPalette() {
    vector<Color> colors;
    Color c1{};
    c1.red = 252;
    c1.green = 186;
    c1.blue = 3;
    Color c2{};
    c2.red = 111;
    c2.green = 179;
    c2.blue = 242;
    colors.push_back(c1);
    colors.push_back(c2);
    colors.push_back(c1);
    ColorPalette palette(colors, 250);
}

int main() {
    createMandelbrotSet();
}