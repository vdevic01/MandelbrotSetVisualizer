#include <iomanip>
#include <iostream>
#include <memory>
#include <optional>
#include <type_traits>

#include "Mode.h"
#include "IterationCalculator.h"
#include "SequentialIterationCalculator.h"
#ifdef ENABLE_OPENCL
#include "OpenCLIterationCalculator.h"
#endif
#include "CUDARemoteIterationCalculator.h"
#ifdef ENABLE_CUDA
#include "CUDAIterationCalculator.h"
#endif
#include "FixedPointArithmetics.h"
#include "ColorManager.h"
#include "Palettes.h"
#include "Sampler.h"
#include "ScopedTimer.h"
#include "ImageWriter.h"

using namespace std;

struct MandelbrotConfig {
    Mode mode = Mode::CPU_PARALLEL;
    bool useHighPrecision = false;
    double reStart = -0.153004885037500013708;
    double reEnd   = -0.152809695287500013708;
    double imStart =  1.039611370300000000002;
    double imEnd   =  1.039757762612500000002;
    fpa::uint reStartHP[fpa::FP_SIZE] = {};
    fpa::uint reEndHP[fpa::FP_SIZE]   = {};
    fpa::uint imStartHP[fpa::FP_SIZE] = {};
    fpa::uint imEndHP[fpa::FP_SIZE]   = {};
    int samples     = 10;
    int maxIter     = 1000;
    int imageWidth  = 900;
    int imageHeight = 600;
    string outputFilename = "./mandelbrot_set.png";
    int paletteLength = 256;
    int paletteId     = 0;
};

template<typename T_ComplexType>
void createMandelbrotSet(const MandelbrotConfig& config, const ColorManager& colorManager, const IterationCalculator& iterCalculator) {
    cout << "=====================================================\n";
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

#ifdef ENABLE_OPENCL
static bool isOpenCLAvailable() {
    cl_uint numPlatforms = 0;
    return clGetPlatformIDs(0, nullptr, &numPlatforms) == CL_SUCCESS && numPlatforms > 0;
}
#endif

#ifdef ENABLE_CUDA
static bool isCUDAAvailable() {
    int deviceCount = 0;
    return cudaGetDeviceCount(&deviceCount) == cudaSuccess && deviceCount > 0;
}
#endif

static void listAvailableModes() {
    cout << "CPU_PARALLEL\n";
#ifdef ENABLE_OPENCL
    if (isOpenCLAvailable()) cout << "OPENCL_LOCAL\n";
#endif
#ifdef ENABLE_CUDA
    if (isCUDAAvailable()) cout << "CUDA_LOCAL\n";
#endif
    cout << "CUDA_REMOTE\n";
}

unique_ptr<IterationCalculator> makeCalculator(Mode mode) {
    switch (mode) {
        case Mode::CPU_PARALLEL:
            return make_unique<SequentialIterationCalculator>();
#ifdef ENABLE_OPENCL
        case Mode::OPENCL_LOCAL:
            return make_unique<OpenCLIterationCalculator>();
#endif
#ifdef ENABLE_CUDA
        case Mode::CUDA_LOCAL:
            return make_unique<CUDAIterationCalculator>();
#endif
        case Mode::CUDA_REMOTE: {
            const char* endpoint = getenv("RUNPOD_ENDPOINT");
            const char* apiKey   = getenv("RUNPOD_API_KEY");
            if (!endpoint || !apiKey || endpoint[0] == '\0' || apiKey[0] == '\0') {
                cerr << "Error: CUDA_REMOTE requires RUNPOD_ENDPOINT and RUNPOD_API_KEY environment variables.\n";
                return nullptr;
            }
            return make_unique<CUDARemoteIterationCalculator>(endpoint, apiKey);
        }
        default:
            cerr << "Error: Mode '" << modeToString(mode) << "' is not supported in this build.\n";
            cerr << "Rebuild with -DENABLE_OPENCL=ON or -DENABLE_CUDA=ON to enable local GPU modes.\n";
            return nullptr;
    }
}

optional<MandelbrotConfig> parseCommandLine(int argc, char* argv[]) {
    MandelbrotConfig config;
    if (argc > 1) {
        try {
            constexpr int W = 36;
            config.mode = modeFromString(argv[1]);
            cout << left << setw(W) << "Mode:"           << modeToString(config.mode) << "\n";

            config.outputFilename = argv[2];
            config.maxIter        = stoi(argv[3]);
            config.paletteLength  = stoi(argv[4]);

            cout << left << setw(W) << "Output file:"    << config.outputFilename << "\n";
            cout << left << setw(W) << "Max iterations:" << config.maxIter << "\n";
            cout << left << setw(W) << "Palette length:" << config.paletteLength << "\n";

            int paletteId = stoi(argv[5]);
            if (paletteId < 0 || paletteId >= (int)palettes.size()) {
                cerr << "Error: Invalid palette ID. Must be between 0 and " << palettes.size() - 1 << ".\n";
                return nullopt;
            }
            config.paletteId = paletteId;
            cout << left << setw(W) << "Palette ID:"     << paletteId << "\n";

            int samples = stoi(argv[6]);
            if (samples < 1) {
                cerr << "Error: Samples must be >= 1.\n";
                return nullopt;
            }
            config.samples = samples;
            cout << left << setw(W) << "Samples:"        << samples << "\n";

            config.useHighPrecision = (stod(argv[7]) != 0.0);
            cout << left << setw(W) << "High precision:" << boolalpha << config.useHighPrecision << noboolalpha << "\n";

            int nextArg;
            if (config.useHighPrecision) {
                config.reStartHP[0] = stoul(argv[8]);  config.reStartHP[1] = stoul(argv[9]);
                config.reStartHP[2] = stoul(argv[10]); config.reStartHP[3] = stoul(argv[11]);

                config.reEndHP[0] = stoul(argv[12]); config.reEndHP[1] = stoul(argv[13]);
                config.reEndHP[2] = stoul(argv[14]); config.reEndHP[3] = stoul(argv[15]);

                config.imStartHP[0] = stoul(argv[16]); config.imStartHP[1] = stoul(argv[17]);
                config.imStartHP[2] = stoul(argv[18]); config.imStartHP[3] = stoul(argv[19]);

                config.imEndHP[0] = stoul(argv[20]); config.imEndHP[1] = stoul(argv[21]);
                config.imEndHP[2] = stoul(argv[22]); config.imEndHP[3] = stoul(argv[23]);

                cout << left << setw(W) << "reStartHP:" << "{" << config.reStartHP[0] << "," << config.reStartHP[1] << "," << config.reStartHP[2] << "," << config.reStartHP[3] << "}\n";
                cout << left << setw(W) << "reEndHP:"   << "{" << config.reEndHP[0]   << "," << config.reEndHP[1]   << "," << config.reEndHP[2]   << "," << config.reEndHP[3]   << "}\n";
                cout << left << setw(W) << "imStartHP:" << "{" << config.imStartHP[0] << "," << config.imStartHP[1] << "," << config.imStartHP[2] << "," << config.imStartHP[3] << "}\n";
                cout << left << setw(W) << "imEndHP:"   << "{" << config.imEndHP[0]   << "," << config.imEndHP[1]   << "," << config.imEndHP[2]   << "," << config.imEndHP[3]   << "}\n";
                nextArg = 24;
            } else {
                config.reStart = stod(argv[8]);
                config.reEnd   = stod(argv[9]);
                config.imStart = stod(argv[10]);
                config.imEnd   = stod(argv[11]);

                cout << left << setw(W) << "reStart:" << config.reStart << "\n";
                cout << left << setw(W) << "reEnd:"   << config.reEnd   << "\n";
                cout << left << setw(W) << "imStart:" << config.imStart << "\n";
                cout << left << setw(W) << "imEnd:"   << config.imEnd   << "\n";
                nextArg = 12;
            }

            if (argc > nextArg + 1) {
                config.imageWidth  = stoi(argv[nextArg]);
                config.imageHeight = stoi(argv[nextArg + 1]);
            }
            cout << left << setw(W) << "Image size:" << config.imageWidth << "x" << config.imageHeight << "\n";
        }
        catch (const exception& e) {
            cerr << "Error parsing arguments: " << e.what() << "\n";
            cerr << "Usage: " << argv[0]
                 << " <MODE> <OUTPUT_FILE> <MAX_ITER> <PALETTE_LENGTH> <PALETTE_ID>"
                    " <SAMPLES> <USE_HP(0/1)> <RE_START> <RE_END> <IM_START> <IM_END>"
                    " [WIDTH HEIGHT]\n"
                 << "  MODE: CPU_PARALLEL | OPENCL_LOCAL | CUDA_LOCAL | CUDA_REMOTE\n";
            return nullopt;
        }
    }
    return config;
}

/**
 * Generates a Mandelbrot set PNG from command-line parameters.
 * Supports standard double-precision and 128-bit fixed-point high-precision modes.
 *
 * Usage:
 *   mandelbrot_visualizer <MODE> <OUTPUT_FILE> <MAX_ITER> <PALETTE_LENGTH> <PALETTE_ID>
 *                         <SAMPLES> <USE_HP> <COORDS...> [WIDTH HEIGHT]
 *
 * Arguments:
 *   MODE           CPU_PARALLEL | OPENCL_LOCAL | CUDA_LOCAL | CUDA_REMOTE
 *   OUTPUT_FILE    Path for the output PNG (e.g. ./out.png)
 *   MAX_ITER       Maximum escape iterations (e.g. 1000)
 *   PALETTE_LENGTH Color palette cycle length (e.g. 256)
 *   PALETTE_ID     Palette index, 0-based (see Palettes.h)
 *   SAMPLES        Samples per pixel for anti-aliasing (>= 1)
 *   USE_HP         0 = double precision, 1 = 128-bit fixed-point
 *
 *   Standard precision (USE_HP=0) — 4 doubles:
 *     RE_START RE_END IM_START IM_END
 *
 *   High precision (USE_HP=1) — 16 unsigned ints (4 words per coordinate,
 *   big-endian 32-bit fixed-point: 1 integer word + 3 fraction words):
 *     RE_START[0..3] RE_END[0..3] IM_START[0..3] IM_END[0..3]
 *
 *   WIDTH HEIGHT   Optional. Image dimensions in pixels. Default: 900x600.
 *
 * Examples:
 *   mandelbrot_visualizer CPU_PARALLEL ./out.png 2000 512 0 4 0 \
 *     -0.153004885037500013708 -0.152809695287500013708 \
 *     1.039611370300000000002  1.039757762612500000002  1800 1200
 *
 *   mandelbrot_visualizer CPU_PARALLEL ./out.png 2000 512 0 4 1 \
 *     4294967295 3637816318 2730300863 4039731417 \
 *     4294967295 3638654652 981237348  3835888558 \
 *     1 170129539 4244482999 3226762018 \
 *     1 170758290 785201715  2000138050 1800 1200
 */
int main(int argc, char* argv[]) {
    if (argc == 2 && string(argv[1]) == "--list-modes") {
        listAvailableModes();
        return 0;
    }

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
