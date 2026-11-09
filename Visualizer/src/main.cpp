#include <iomanip>
#include <iostream>
#include <memory>
#include <optional>
#include <type_traits>

#include "Mode.h"
#include "IterationCalculator.h"
#include "CpuParallelIterationCalculator.h"
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
    int fractionWords = fpa::FRACTION_PART;   // fraction size of the high-precision format (--fraction-words)
    fpa::uint reStartHP[fpa::MAX_FP_SIZE] = {};
    fpa::uint reEndHP[fpa::MAX_FP_SIZE]   = {};
    fpa::uint imStartHP[fpa::MAX_FP_SIZE] = {};
    fpa::uint imEndHP[fpa::MAX_FP_SIZE]   = {};
    int samples     = 10;
    int maxIter     = 1000;
    int imageWidth  = 900;
    int imageHeight = 600;
    string outputFilename = "./mandelbrot_set.png";
    int paletteLength = 256;
    int paletteId     = 0;
    double escapeRadius = DEFAULT_ESCAPE_RADIUS;   // iteration stops once |z| is above this (--escape-radius)
};

// T_Points is vector<Complex> (double precision) or HpPoints (fixed point).
template<typename T_Points>
void createMandelbrotSet(const MandelbrotConfig& config, const ColorManager& colorManager, const IterationCalculator& iterCalculator) {
    cout << "=====================================================\n";
    ScopedTimer total_timer("Total generation time");

    const int imageSize = config.imageHeight * config.imageWidth;

    T_Points points;
    {
        ScopedTimer timer("Pixel mapping");
        if constexpr (is_same_v<T_Points, vector<Complex>>) {
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
                config.samples, config.fractionWords);
        }
    }

    const int totalPoints = imageSize * config.samples;
    vector<int> iters(totalPoints);
    {
        ScopedTimer timer("Calculating escape iterations");
        iterCalculator.calculate(points, iters, config.maxIter, config.escapeRadius);
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
            return make_unique<CpuParallelIterationCalculator>();
#ifdef ENABLE_OPENCL
        case Mode::OPENCL_LOCAL:
            return make_unique<OpenCLIterationCalculator>();
#endif
#ifdef ENABLE_CUDA
        case Mode::CUDA_LOCAL:
            return make_unique<CUDAIterationCalculator>();
#endif
        default:
            cerr << "Error: Mode '" << modeToString(mode) << "' is not supported in this build.\n";
            cerr << "Rebuild with -DENABLE_OPENCL=ON or -DENABLE_CUDA=ON to enable local GPU modes.\n";
            return nullptr;
    }
}

static unique_ptr<CUDARemoteIterationCalculator> makeRemoteCalculator() {
    const char* endpoint = getenv("REMOTE_ENDPOINT");
    const char* apiKey   = getenv("REMOTE_API_KEY");
    if (!endpoint || !apiKey || endpoint[0] == '\0' || apiKey[0] == '\0') {
        cerr << "Error: CUDA_REMOTE requires REMOTE_ENDPOINT and REMOTE_API_KEY environment variables.\n";
        return nullptr;
    }
    return make_unique<CUDARemoteIterationCalculator>(endpoint, apiKey);
}

static void createMandelbrotSetRemote(
    const MandelbrotConfig& config, const ColorManager& colorManager,
    const CUDARemoteIterationCalculator& calc)
{
    cout << "=====================================================\n";
    ScopedTimer total_timer("Total generation time");

    vector<int> iters;
    {
        ScopedTimer timer("Remote execution (sampling + iterations)");
        if (config.useHighPrecision) {
            iters = calc.calculateHP(
                config.reStartHP, config.reEndHP,
                config.imStartHP, config.imEndHP,
                config.fractionWords,
                config.imageWidth, config.imageHeight,
                config.samples, static_cast<unsigned int>(config.maxIter), config.escapeRadius);
        } else {
            iters = calc.calculate(
                config.reStart, config.reEnd,
                config.imStart, config.imEnd,
                config.imageWidth, config.imageHeight,
                config.samples, static_cast<unsigned int>(config.maxIter), config.escapeRadius);
        }
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

// Parses the command line. The flags are --fraction-words N and --escape-radius R (also as --flag=value);
// they may appear anywhere. Everything else is positional (see the usage message).
optional<MandelbrotConfig> parseCommandLine(int argcIn, char* argvIn[]) {
    int fractionWords = fpa::FRACTION_PART;
    double escapeRadius = DEFAULT_ESCAPE_RADIUS;
    vector<char*> positional;
    for (int i = 0; i < argcIn; i++) {
        const string arg = argvIn[i];
        string flag;
        for (const char* known : {"--fraction-words", "--escape-radius"}) {
            if (arg == known || arg.rfind(string(known) + "=", 0) == 0) flag = known;
        }
        if (flag.empty()) {
            positional.push_back(argvIn[i]);
            continue;
        }
        string value;
        if (arg == flag) {
            if (i + 1 >= argcIn) {
                cerr << "Error: " << flag << " needs a value.\n";
                return nullopt;
            }
            value = argvIn[++i];
        } else {
            value = arg.substr(flag.size() + 1);
        }
        try {
            size_t parsed = 0;
            if (flag == "--fraction-words") fractionWords = stoi(value, &parsed);
            else                            escapeRadius  = stod(value, &parsed);
            if (parsed != value.size()) throw invalid_argument("not a number");
        } catch (const exception&) {
            cerr << "Error: " << flag << " must be a number, got '" << value << "'.\n";
            return nullopt;
        }
        if (!fpa::isSupportedFractionPart(fractionWords)) {
            cerr << "Error: --fraction-words must be between " << fpa::MIN_FRACTION_PART << " and "
                 << fpa::MAX_FRACTION_PART << ", got " << fractionWords << ".\n";
            return nullopt;
        }
        if (!isSupportedEscapeRadius(escapeRadius)) {   // also rejects NaN
            cerr << "Error: --escape-radius must be between " << MIN_ESCAPE_RADIUS << " and "
                 << MAX_ESCAPE_RADIUS << ", got " << value << ".\n";
            return nullopt;
        }
    }
    int argc = static_cast<int>(positional.size());
    char** argv = positional.data();

    MandelbrotConfig config;
    config.fractionWords = fractionWords;
    config.escapeRadius = escapeRadius;
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
                cout << left << setw(W) << "Fraction words:" << config.fractionWords << "\n";

                const int words = fpa::fpSize(config.fractionWords);
                const int required = 8 + 4 * words;
                if (argc < required) {
                    cerr << "Error: high precision with " << config.fractionWords << " fraction words needs "
                         << 4 * words << " coordinate numbers (" << words << " per coordinate), but "
                         << (argc > 8 ? argc - 8 : 0) << " were given.\n";
                    return nullopt;
                }

                auto readWords = [&](int first, fpa::uint* out) {
                    for (int i = 0; i < words; i++) out[i] = static_cast<fpa::uint>(stoul(argv[first + i]));
                };
                readWords(8, config.reStartHP);
                readWords(8 + words, config.reEndHP);
                readWords(8 + 2 * words, config.imStartHP);
                readWords(8 + 3 * words, config.imEndHP);

                auto printWords = [&](const char* label, const fpa::uint* v) {
                    cout << left << setw(W) << label << "{";
                    for (int i = 0; i < words; i++) cout << (i ? "," : "") << v[i];
                    cout << "}\n";
                };
                printWords("reStartHP:", config.reStartHP);
                printWords("reEndHP:", config.reEndHP);
                printWords("imStartHP:", config.imStartHP);
                printWords("imEndHP:", config.imEndHP);
                nextArg = required;
            } else {
                if (argc < 12) {
                    cerr << "Error: double precision needs 4 coordinates (RE_START RE_END IM_START IM_END).\n";
                    return nullopt;
                }
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

            // After the coordinates only WIDTH HEIGHT may follow. Anything else usually means that the number
            // of coordinate words does not match the fraction size (pass --fraction-words N).
            const int extraArgs = argc - nextArg;
            if (extraArgs != 0 && extraArgs != 2) {
                cerr << "Error: found " << extraArgs << " argument(s) after the coordinates, expected none or WIDTH HEIGHT."
                     << (config.useHighPrecision ? " Check that --fraction-words matches the number of coordinate words." : "")
                     << "\n";
                return nullopt;
            }
            if (extraArgs == 2) {
                config.imageWidth  = stoi(argv[nextArg]);
                config.imageHeight = stoi(argv[nextArg + 1]);
                if (config.imageWidth <= 0 || config.imageHeight <= 0) {
                    cerr << "Error: WIDTH and HEIGHT must be positive.\n";
                    return nullopt;
                }
            }
            cout << left << setw(W) << "Image size:" << config.imageWidth << "x" << config.imageHeight << "\n";
            cout << left << setw(W) << "Escape radius:" << config.escapeRadius << "\n";
        }
        catch (const exception& e) {
            cerr << "Error parsing arguments: " << e.what() << "\n";
            cerr << "Usage: " << argv[0]
                 << " <MODE> <OUTPUT_FILE> <MAX_ITER> <PALETTE_LENGTH> <PALETTE_ID>"
                    " <SAMPLES> <USE_HP(0/1)> <RE_START> <RE_END> <IM_START> <IM_END>"
                    " [WIDTH HEIGHT] [--fraction-words N] [--escape-radius R]\n"
                 << "  MODE: CPU_PARALLEL | OPENCL_LOCAL | CUDA_LOCAL | CUDA_REMOTE\n"
                 << "  --escape-radius R: stop iterating once |z| > R (default " << DEFAULT_ESCAPE_RADIUS << ", range "
                 << MIN_ESCAPE_RADIUS << ".." << MAX_ESCAPE_RADIUS << "). Larger values give smoother colours.\n"
                 << "  High precision takes 4 x (1 + N) coordinate numbers (N = fraction words, default "
                 << fpa::FRACTION_PART << ", range " << fpa::MIN_FRACTION_PART << ".." << fpa::MAX_FRACTION_PART << ").\n";
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
 *                         [--fraction-words N] [--escape-radius R]
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
 *   --escape-radius R  Optional. Iteration stops once |z| > R. Default: 2, range 2..128.
 *                      Larger values give smoother colours.
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

    try {
        if (config.mode == Mode::CUDA_REMOTE) {
            const auto remoteCalc = makeRemoteCalculator();
            if (!remoteCalc) return 1;
            createMandelbrotSetRemote(config, colorManager, *remoteCalc);
        } else {
            const auto iterCalculator = makeCalculator(config.mode);
            if (!iterCalculator) return 1;
            if (config.useHighPrecision)
                createMandelbrotSet<HpPoints>(config, colorManager, *iterCalculator);
            else
                createMandelbrotSet<vector<Complex>>(config, colorManager, *iterCalculator);
        }
    } catch (const exception& e) {
        // A clean message and exit code instead of std::terminate, so the GUI can show what went wrong.
        cerr << "Error: " << e.what() << "\n";
        return 1;
    }

    return 0;
}
