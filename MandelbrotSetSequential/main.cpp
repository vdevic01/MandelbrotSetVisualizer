#include <iostream>
#include <complex>
#include <opencv2/opencv.hpp>

using namespace std;

const double RE_START = -2.0;
const double RE_END = 1.0;
const double IM_START = -1;
const double IM_END = 1;

const int MAX_ITER = 150;

const int IMAGE_WIDTH = 900;
const int IMAGE_HEIGHT = 600;

const int PALETTE_LENGTH = 500;

void createGrayscaleImage(int* pixels) {
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
    return MAX_ITER;
}

int main() {
    int* pixels = new int[IMAGE_HEIGHT * IMAGE_WIDTH];

    for (int i = 0; i < IMAGE_HEIGHT; i++) {
        for (int j = 0; j < IMAGE_WIDTH; j++) {
            double realPart = mapVal(j, 0, IMAGE_WIDTH, RE_START, RE_END);
            double imaginaryPart = mapVal(i, 0, IMAGE_HEIGHT, IM_START, IM_END);
            complex<double> point(realPart, imaginaryPart);
            int totalIters = complexPointIter(point);
            int idx = j + (i * IMAGE_WIDTH);

            if (totalIters == -1) {
                pixels[idx] = 0;
            }
            else {               
                pixels[idx] = ceil(mapVal(totalIters, MAX_ITER, 0, 0, 255));
            }
        }
    }
    createGrayscaleImage(pixels);
}