#include <complex>
#include <utility>
#include <vector>
#include <memory>
#include <chrono>
#include <iomanip>
#include <iostream>

#include "Mode.h"
#include "IterationCalculator.h"
#include "SequentialIterationCalculator.h"
#ifdef ENABLE_OPENCL
#include "OpenCLIterationCalculator.h"
#endif
#ifdef ENABLE_CUDA
#include "CUDAIterationCalculator.h"
#endif
#include "FixedPointArithmetics.h"
#include "ColorManager.h"
#include "Palettes.h"
#include "fpng.h"

using namespace std;

struct MandelbrotConfig {
    Mode mode = Mode::SEQUENTIAL;
    bool useHighPrecision = false;
    double reStart = -0.153004885037500013708;
    double reEnd   = -0.152809695287500013708;
    double imStart =  1.039611370300000000002;
    double imEnd   =  1.039757762612500000002;
    fpa::uint reStartHP[fpa::FP_SIZE];
    fpa::uint reEndHP[fpa::FP_SIZE];
    fpa::uint imStartHP[fpa::FP_SIZE];
    fpa::uint imEndHP[fpa::FP_SIZE];
    int samples = 10;
    int maxIter = 1000;
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
        m_start(chrono::high_resolution_clock::now())
    {}

    ~ScopedTimer() {
        auto end = chrono::high_resolution_clock::now();
        auto duration = chrono::duration_cast<chrono::milliseconds>(end - m_start);
        cout << left << setw(36) << m_name + ":" << duration.count() << " ms\n";
        cout << "===========================================\n";
    }

private:
    string m_name;
    chrono::high_resolution_clock::time_point m_start;
};

void saveColorImageToPng(const vector<Color>& pixels, int width, int height, const string& filename) {
    vector<unsigned char> rgb_data(width * height * 3);

    for (int y = 0; y < height; ++y) {
        int target_y = height - 1 - y;
        for (int x = 0; x < width; ++x) {
            const Color& p = pixels[y * width + x];
            int di = (target_y * width + x) * 3;
            rgb_data[di + 0] = p.red;
            rgb_data[di + 1] = p.green;
            rgb_data[di + 2] = p.blue;
        }
    }

    if (!fpng::fpng_encode_image_to_file(filename.c_str(), rgb_data.data(), width, height, 3))
        throw runtime_error("Failed to write PNG file: " + filename);
}

double fastRandomFromRange(const double& min, const double& max) {
    return min + (rand() / (RAND_MAX + 1.0)) * (max - min);
}

vector<Complex> samplePointsFromComplexPlane(
    int imageHeight, int imageWidth,
    double imStart, double imEnd,
    double reStart, double reEnd,
    int samples)
{
    vector<Complex> points(imageWidth * imageHeight * samples);

    const double scaleImaginary = (imEnd - imStart) / imageHeight;
    const double scaleReal      = (reEnd - reStart) / imageWidth;

    #pragma omp parallel for
    for (int i = 0; i < imageHeight; i++) {
        double imaginaryPartBoundary = imStart + i * scaleImaginary;
        double realPartBoundary = reStart;
        for (int j = 0; j < imageWidth; j++) {
            for (int k = 0; k < samples; k++) {
                double realPart = fastRandomFromRange(realPartBoundary, realPartBoundary + scaleReal);
                double imagPart = fastRandomFromRange(imaginaryPartBoundary, imaginaryPartBoundary + scaleImaginary);
                points[(j + i * imageWidth) * samples + k] = { realPart, imagPart };
            }
            realPartBoundary += scaleReal;
        }
    }
    return points;
}

vector<ComplexHP> samplePointsFromComplexPlane(
    int imageHeight, int imageWidth,
    const fpa::uint imStart[fpa::FP_SIZE], const fpa::uint imEnd[fpa::FP_SIZE],
    const fpa::uint reStart[fpa::FP_SIZE], const fpa::uint reEnd[fpa::FP_SIZE],
    int samples)
{
    vector<ComplexHP> points(imageWidth * imageHeight * samples);

    fpa::uint imageHeightHP[fpa::FP_SIZE] = {(fpa::uint)imageHeight, 0, 0, 0};
    fpa::uint imageWidthHP[fpa::FP_SIZE]  = {(fpa::uint)imageWidth,  0, 0, 0};

    fpa::uint temp[fpa::FP_SIZE] = {};
    fpa::uint scaleImaginary[fpa::FP_SIZE] = {};
    fpa::subFixed(imEnd, imStart, temp);
    fpa::divFixed(temp, imageHeightHP, scaleImaginary);

    fpa::uint scaleReal[fpa::FP_SIZE] = {};
    fpa::subFixed(reEnd, reStart, temp);
    fpa::divFixed(temp, imageWidthHP, scaleReal);

    fpa::uint realPartFP[fpa::FP_SIZE];
    fpa::uint imagPartFP[fpa::FP_SIZE];

    #pragma omp parallel for private(realPartFP, imagPartFP)
    for (int i = 0; i < imageHeight; i++) {
        fpa::uint imaginaryPartBoundary[fpa::FP_SIZE] = {};
        fpa::uint iHP[fpa::FP_SIZE] = {};
        fpa::floatingToFixedPoint(static_cast<double>(i), iHP);
        fpa::mulCmplFixed(iHP, scaleImaginary, imaginaryPartBoundary);
        fpa::addFixed(imStart, imaginaryPartBoundary, imaginaryPartBoundary);

        fpa::uint realPartBoundary[fpa::FP_SIZE] = {};
        copy(reStart, reStart + fpa::FP_SIZE, realPartBoundary);

        for (int j = 0; j < imageWidth; j++) {
            for (int k = 0; k < samples; k++) {
                fpa::addFixed(realPartBoundary, scaleReal, realPartFP);
                fpa::randomFromRange(realPartBoundary, realPartFP, realPartFP);

                fpa::addFixed(imaginaryPartBoundary, scaleImaginary, imagPartFP);
                fpa::randomFromRange(imaginaryPartBoundary, imagPartFP, imagPartFP);

                const int idx = (j + i * imageWidth) * samples + k;
                for (int m = 0; m < fpa::FP_SIZE; m++) {
                    points[idx].real[m] = realPartFP[m];
                    points[idx].imag[m] = imagPartFP[m];
                }
            }
            fpa::addFixed(realPartBoundary, scaleReal, realPartBoundary);
        }
    }
    return points;
}

template<typename T_ComplexType>
void createMandelbrotSet(const MandelbrotConfig& config, const ColorManager& colorManager, const IterationCalculator& iterCalculator) {
    cout << "===========================================\n";
    ScopedTimer total_timer("Total generation time");

    const int imageSize = config.imageHeight * config.imageWidth;

    vector<T_ComplexType> points;
    {
        ScopedTimer timer("Pixel mapping");
        if constexpr (is_same_v<T_ComplexType, Complex>) {
            points = samplePointsFromComplexPlane(
                config.imageHeight, config.imageWidth,
                config.imStart, config.imEnd,
                config.reStart, config.reEnd,
                config.samples);
        } else {
            points = samplePointsFromComplexPlane(
                config.imageHeight, config.imageWidth,
                config.imStartHP, config.imEndHP,
                config.reStartHP, config.reEndHP,
                config.samples);
        }
    }

    const int totalPoints = imageSize * config.samples;
    vector<int> iters(totalPoints);
    {
        ScopedTimer timer("Calculating escape iterations");
        iterCalculator.calculate(points, iters, config.maxIter);
    }

    vector<Color> pixels;
    {
        ScopedTimer timer("Coloring");
        pixels = colorManager.paint(iters);
    }

    {
        ScopedTimer timer("Image generation");
        saveColorImageToPng(pixels, config.imageWidth, config.imageHeight, config.outputFilename);
    }
}

unique_ptr<IterationCalculator> makeCalculator(Mode mode) {
    switch (mode) {
        case Mode::SEQUENTIAL:
            return make_unique<SequentialIterationCalculator>();
#ifdef ENABLE_OPENCL
        case Mode::OPENCL_LOCAL:
            return make_unique<OpenCLIterationCalculator>();
#endif
#ifdef ENABLE_CUDA
        case Mode::CUDA_LOCAL:
            return make_unique<CUDAIterationCalculator>();
        case Mode::CUDA_REMOTE:
            cerr << "Error: CUDA_REMOTE is not yet implemented.\n";
            return nullptr;
#endif
        default:
            cerr << "Error: Mode '" << modeToString(mode) << "' is not supported in this build.\n";
            cerr << "Rebuild with -DENABLE_OPENCL=ON or -DENABLE_CUDA=ON to enable GPU modes.\n";
            return nullptr;
    }
}

optional<MandelbrotConfig> parseCommandLine(int argc, char* argv[]) {
    MandelbrotConfig config;
    if (argc > 1) {
        try {
            config.mode = modeFromString(argv[1]);
            cout << "Mode:            " << modeToString(config.mode) << "\n";

            config.outputFilename = argv[2];
            config.maxIter        = stoi(argv[3]);
            config.paletteLength  = stoi(argv[4]);

            cout << "Output file:     " << config.outputFilename << "\n";
            cout << "Max iterations:  " << config.maxIter << "\n";
            cout << "Palette length:  " << config.paletteLength << "\n";

            int paletteId = stoi(argv[5]);
            if (paletteId < 0 || paletteId >= (int)palettes.size()) {
                cerr << "Error: Invalid palette ID. Must be between 0 and " << palettes.size() - 1 << ".\n";
                return nullopt;
            }
            config.paletteId = paletteId;
            cout << "Palette ID:      " << paletteId << "\n";

            int samples = stoi(argv[6]);
            if (samples < 1) {
                cerr << "Error: Samples must be >= 1.\n";
                return nullopt;
            }
            config.samples = samples;
            cout << "Samples:         " << samples << "\n";

            config.useHighPrecision = (stod(argv[7]) != 0.0);
            cout << "High precision:  " << boolalpha << config.useHighPrecision << "\n";

            if (config.useHighPrecision) {
                config.reStartHP[0] = stoul(argv[8]);  config.reStartHP[1] = stoul(argv[9]);
                config.reStartHP[2] = stoul(argv[10]); config.reStartHP[3] = stoul(argv[11]);

                config.reEndHP[0] = stoul(argv[12]); config.reEndHP[1] = stoul(argv[13]);
                config.reEndHP[2] = stoul(argv[14]); config.reEndHP[3] = stoul(argv[15]);

                config.imStartHP[0] = stoul(argv[16]); config.imStartHP[1] = stoul(argv[17]);
                config.imStartHP[2] = stoul(argv[18]); config.imStartHP[3] = stoul(argv[19]);

                config.imEndHP[0] = stoul(argv[20]); config.imEndHP[1] = stoul(argv[21]);
                config.imEndHP[2] = stoul(argv[22]); config.imEndHP[3] = stoul(argv[23]);

                cout << "reStartHP: {" << config.reStartHP[0] << "," << config.reStartHP[1] << "," << config.reStartHP[2] << "," << config.reStartHP[3] << "}\n";
                cout << "reEndHP:   {" << config.reEndHP[0]   << "," << config.reEndHP[1]   << "," << config.reEndHP[2]   << "," << config.reEndHP[3]   << "}\n";
                cout << "imStartHP: {" << config.imStartHP[0] << "," << config.imStartHP[1] << "," << config.imStartHP[2] << "," << config.imStartHP[3] << "}\n";
                cout << "imEndHP:   {" << config.imEndHP[0]   << "," << config.imEndHP[1]   << "," << config.imEndHP[2]   << "," << config.imEndHP[3]   << "}\n";
            } else {
                config.reStart = stod(argv[8]);
                config.reEnd   = stod(argv[9]);
                config.imStart = stod(argv[10]);
                config.imEnd   = stod(argv[11]);

                cout << "reStart: " << config.reStart << "\n";
                cout << "reEnd:   " << config.reEnd   << "\n";
                cout << "imStart: " << config.imStart << "\n";
                cout << "imEnd:   " << config.imEnd   << "\n";
            }
        }
        catch (const exception& e) {
            cerr << "Error parsing arguments: " << e.what() << "\n";
            cerr << "Usage: " << argv[0]
                 << " <MODE> <OUTPUT_FILE> <MAX_ITER> <PALETTE_LENGTH> <PALETTE_ID>"
                    " <SAMPLES> <USE_HP(0/1)> <RE_START> <RE_END> <IM_START> <IM_END>\n"
                 << "  MODE: SEQUENTIAL | OPENCL_LOCAL | CUDA_LOCAL | CUDA_REMOTE\n";
            return nullopt;
        }
    }
    return config;
}

int main(int argc, char* argv[]) {
    auto configOpt = parseCommandLine(argc, argv);
    if (!configOpt) return 1;

    const MandelbrotConfig config = configOpt.value();
    const CyclicColorPalette colorManager(
        config.imageHeight * config.imageWidth,
        palettes[config.paletteId],
        config.paletteLength,
        config.samples);

    const auto iterCalculator = makeCalculator(config.mode);
    if (!iterCalculator) return 1;

    if (config.useHighPrecision)
        createMandelbrotSet<ComplexHP>(config, colorManager, *iterCalculator);
    else
        createMandelbrotSet<Complex>(config, colorManager, *iterCalculator);

    return 0;
}
