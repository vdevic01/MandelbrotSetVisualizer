#include <complex>
#include <opencv2/opencv.hpp>
#include <utility>
#include <vector>
#include <chrono>
#include <boost/multiprecision/cpp_dec_float.hpp>
#include <boost/multiprecision/cpp_int.hpp>
#include <iomanip>
#include <omp.h>
#include <iomanip>

#include "OpenCLWrapper.h"
#include "FixedPointArithmetics.h"
#include "ColorManager.h"
#include "Palettes.h"

using namespace std;
using namespace boost::multiprecision;

struct MandelbrotConfig {
    bool useHighPrecision = false;
    double reStart = -0.153004885037500013708;
    double reEnd = -0.152809695287500013708;
    double imStart = 1.039611370300000000002;
    double imEnd = 1.039757762612500000002;
    cpp_dec_float_50 reStartHP = reStart;
    cpp_dec_float_50 reEndHP = reEnd;
    cpp_dec_float_50 imStartHP = imStart;
    cpp_dec_float_50 imEndHP = imEnd;
    int samples = 1;
    int maxIter = 400;
    int imageWidth = 900;
    int imageHeight = 600;
    string outputFilename = "./mandelbrot_set.png";
    int paletteLength = 256;
    int paletteId = 0;
};

class ScopedTimer {
public:
    ScopedTimer(const string& name) :
        m_name(name),
        m_start(std::chrono::high_resolution_clock::now())
    {
    }

    ~ScopedTimer() {
        auto end = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - m_start);

        const int NAME_COLUMN_WIDTH = 36;

        std::cout << std::left
            << std::setw(NAME_COLUMN_WIDTH)
            << m_name + ":"
            << duration.count() << " ms\n";

         std::cout << "===========================================\n";
    }

private:
    string m_name;
    chrono::high_resolution_clock::time_point m_start;
};

cv::Mat createColorImage(vector<Color>& pixels, const int width, const int height) {
    cv::Mat image(height, width, CV_8UC3);
    uchar* imageData = image.data;

    for (int y = 0; y < height; ++y) {
        uchar* rowPtr = imageData + (height - y - 1) * image.step;

        for (int x = 0; x < width; ++x) {
            int pixelIndex = y * width + x;
            Color pixelValue = pixels[pixelIndex];

            uchar* pixelPtr = rowPtr + x * 3;

            pixelPtr[0] = pixelValue.blue;
            pixelPtr[1] = pixelValue.green;
            pixelPtr[2] = pixelValue.red;
        }
    }
    return image;
}

template<typename T>
T fastRandomFromRange(const T& min, const T& max) {
    const double r = rand() / (RAND_MAX + 1.0);

    if constexpr (is_same_v<T, cpp_dec_float_50>) {
        return min + cpp_dec_float_50(r) * (max - min);
    }
    else {
        return min + static_cast<T>(r) * (max - min);
    }
}

vector<Complex> samplePointsFromComplexPlane(const int imageHeight, const int imageWidth, const double imStart, const double imEnd, const double reStart, const double reEnd, const int samples) {
    vector<Complex> points(imageWidth * imageHeight * samples);

    const double scaleImaginary = (imEnd - imStart) / imageHeight;
    const double scaleReal = (reEnd - reStart) / imageWidth;

    #pragma omp parallel for
    for (int i = 0; i < imageHeight; i++) {
        double imaginaryPartBoundary = imStart + i * scaleImaginary;
        double realPartBoundary = reStart;
        for (int j = 0; j < imageWidth; j++) {
            for (int k = 0; k < samples; k++) {
                double realPart = fastRandomFromRange<double>(realPartBoundary, realPartBoundary + scaleReal);
                double imaginaryPart = fastRandomFromRange<double>(imaginaryPartBoundary, imaginaryPartBoundary + scaleImaginary);
                const int idx = (j + i * imageWidth) * samples + k;
                points[idx] = { realPart, imaginaryPart };
            }
            realPartBoundary += scaleReal;
        }
    }
    return points;
}

vector<ComplexHP> samplePointsFromComplexPlane(const int imageHeight, const int imageWidth, const cpp_dec_float_50 imStart, const cpp_dec_float_50 imEnd, const cpp_dec_float_50 reStart, const cpp_dec_float_50 reEnd, const int samples) {
    vector<ComplexHP> points(imageWidth * imageHeight);
    const cpp_dec_float_50 zeroHP = 0;
    const cpp_dec_float_50 scaleImaginary = (imEnd - imStart) / cpp_dec_float_50(imageHeight);
    const cpp_dec_float_50 scaleReal = (reEnd - reStart) / cpp_dec_float_50(imageWidth);

    unsigned int realPartFP[4];
    unsigned int imagPartFP[4];

    #pragma omp parallel for private(realPartFP, imagPartFP)
    for (int i = 0; i < imageHeight; i++) {
        cpp_dec_float_50 imaginaryPartBoundary = imStart + cpp_dec_float_50(i) * scaleImaginary;
        cpp_dec_float_50 realPartBoundary = reStart;
        for (int j = 0; j < imageWidth; j++) {

            for (int k = 0; k < samples; k++) {
                cpp_dec_float_50 realPart = fastRandomFromRange<cpp_dec_float_50>(realPartBoundary, realPartBoundary + scaleReal);
                cpp_dec_float_50 imaginaryPart = fastRandomFromRange<cpp_dec_float_50>(imaginaryPartBoundary, imaginaryPartBoundary + scaleImaginary);
                const int idx = (j + i * imageWidth) * samples + k;
                fpa::convertToFixedPoint(realPart, realPartFP);
                fpa::convertToFixedPoint(imaginaryPart, imagPartFP);

                for (int m = 0; m < 4; m++) {
                    points[idx].real[m] = realPartFP[m];
                    points[idx].imag[m] = imagPartFP[m];
                }
            }
            realPartBoundary += scaleReal;
        }
    }

    return points;
}

template<typename T_RealType, typename T_ComplexPointType>
void createMandelbrotSet(const MandelbrotConfig& config, const ColorManager& colorManager) {
    cout << "===========================================\n";
    ScopedTimer total_timer("Total generation time");

    const int imageSize = config.imageHeight * config.imageWidth;

    vector<T_ComplexPointType> points;
    {
        ScopedTimer timer("Pixel mapping");
        if constexpr (is_same_v<T_RealType, double>) {
            points = samplePointsFromComplexPlane(config.imageHeight, config.imageWidth, config.imStart, config.imEnd, config.reStart, config.reEnd, config.samples);
        }
        else {
            points = samplePointsFromComplexPlane(config.imageHeight, config.imageWidth, config.imStartHP, config.imEndHP, config.reStartHP, config.reEndHP, config.samples);
        }
    }

    const int totalPoints = imageSize * config.samples;
    vector<int> iters(totalPoints);
    {
        ScopedTimer timer("\nCalculating escape iterations");
        if constexpr (is_same_v<T_ComplexPointType, Complex>) {
            calculateIters<Complex>(points, iters, totalPoints, config.maxIter, "kernel.cl");
        }
        else {            
            calculateIters<ComplexHP>(points, iters, totalPoints, config.maxIter, "kernelHP.cl");
        }
    }

    vector<Color> pixels;
    {
        ScopedTimer timer("Coloring");
        pixels = colorManager.paint(iters);
    }

    {
        ScopedTimer timer("Image generation");
        cv::Mat image = createColorImage(pixels, config.imageWidth, config.imageHeight);
        cv::imwrite(config.outputFilename, image);
    }
}

optional<MandelbrotConfig> parseCommandLine(int argc, char* argv[]) {
    MandelbrotConfig config;
    if (argc > 1) {
        try {
            config.useHighPrecision = (std::stod(argv[1]) != 0.0);
            if (config.useHighPrecision) {
                config.reStartHP = cpp_dec_float_50(argv[2]);
                config.reEndHP = cpp_dec_float_50(argv[3]);
                config.imStartHP = cpp_dec_float_50(argv[4]);
                config.imEndHP = cpp_dec_float_50(argv[5]);
            }
            else {
                config.reStart = stod(argv[2]);
                config.reEnd = stod(argv[3]);
                config.imStart = stod(argv[4]);
                config.imEnd = stod(argv[5]);
            }
            config.outputFilename = argv[6];
            config.maxIter = stoi(argv[7]);
            config.paletteLength = stoi(argv[8]);
            int paletteId = stoi(argv[9]);
            if (paletteId < 0 || paletteId >= palettes.size()) {
                cerr << "Error: Invalid palette ID. Must be between 0 and " << palettes.size() - 1 << ".\n";
                return nullopt;
            }
            config.paletteId = paletteId;
            int samples = stoi(argv[10]);
            if (samples < 1) {
                cerr << "Error: Invalid samples number. Must be greater than 0.\n";
                return nullopt;
            }
        }
        catch (const exception& e) {
            cerr << "Error parsing arguments: " << e.what() << "\n";
            cerr << "Usage: " << argv[0] << " <USE_HIGH_PRECISION(0/1)> <RE_START> <RE_END> <IM_START> <IM_END> <OUTPUT_FILENAME> <MAX_ITER> <PALETTE_LENGTH> <PALETTE_ID>\n";
            return nullopt;
        }
    }
    return config;
}

/**
 * @brief Main entry point of the Mandelbrot set visualizer.
 *
 * This program generates a Mandelbrot set image based on user-provided parameters
 * from the command line, and saves the output to a PNG file. It supports both
 * standard double-precision and high-precision floating-point calculations.
 *
 * Command-line arguments are parsed in the following order:
 *
 * @param argc The number of command-line arguments.
 * @param argv An array of C-style strings representing the command-line arguments.
 *
 * Expected arguments (order is strict):
 * [1] <USE_HIGH_PRECISION> (int): 0 for standard double-precision, 1 for high-precision
 * (boost::multiprecision::cpp_dec_float_50).
 * [2] <RE_START>         (double or high-precision float): Real component start of the complex plane.
 * [3] <RE_END>           (double or high-precision float): Real component end of the complex plane.
 * [4] <IM_START>         (double or high-precision float): Imaginary component start of the complex plane.
 * [5] <IM_END>           (double or high-precision float): Imaginary component end of the complex plane.
 * NOTE: The type of RE_START/END and IM_START/END depends on <USE_HIGH_PRECISION>.
 * [6] <OUTPUT_FILENAME>  (string): Path and name for the output PNG image file (e.g., "output.png").
 * [7] <MAX_ITER>         (int): Maximum number of iterations for the Mandelbrot calculation.
 * [8] <PALETTE_LENGTH>   (int): The desired length of the color palette to be used.
 * [9] <PALETTE_ID>       (int): An index (0-based) to select a predefined color palette.
 * [10]<SAMPLES>          (int): Number of samples used for each image pixel.
 *
 * Example Usage:
 * ./mandelbrot 0 -2.0 1.0 -1.0 1.0 mandelbrot_double.png 400 256 0
 * ./mandelbrot 1 -0.153004885037500013708 -0.152809695287500013708 1.039611370300000000002 1.039757762612500000002 mandelbrot_hp.png 1000 512 1
 *
 * @return int Returns 0 on successful execution, 1 if argument parsing fails or an invalid palette ID is provided.
 */
int main(int argc, char* argv[]) {
    auto configOpt = parseCommandLine(argc, argv);
    if (!configOpt) {
        return 1;
    }
    const MandelbrotConfig config = configOpt.value();
    const CyclicColorPalette colorManager(config.imageHeight * config.imageWidth, palettes[config.paletteId], config.paletteLength, config.samples);

    if (config.useHighPrecision) {
        createMandelbrotSet<cpp_dec_float_50, ComplexHP>(config, colorManager);
    }
    else {
        createMandelbrotSet<double, Complex>(config, colorManager);
    }
}