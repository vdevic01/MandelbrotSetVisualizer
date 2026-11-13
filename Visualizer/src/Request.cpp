#include "Request.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <fstream>
#include <initializer_list>
#include <iomanip>
#include <iostream>
#include <iterator>
#include <sstream>
#include <stdexcept>
#include <vector>

#include <nlohmann/json.hpp>

#include "Palettes.h"

using namespace std;
using json = nlohmann::json;

namespace {

constexpr int REQUEST_VERSION = 1;
constexpr int MAX_IMAGE_SIDE = 32768;

// A problem with the request, worded for the user ("color.spread: must be between 0 and 10, got 11").
struct RequestError : runtime_error {
    using runtime_error::runtime_error;
};

[[noreturn]] void fail(const string& path, const string& message) {
    throw RequestError(path + ": " + message);
}

string join(const string& path, const string& key) {
    return path.empty() ? key : path + "." + key;
}

// ---------------------------------------------------------------------------------------------------------
// Reading values out of the request. Every option is optional unless said otherwise, and every one is
// checked: its type, its range, and that no option the request does not know about is present.
// ---------------------------------------------------------------------------------------------------------

const json* optionalObject(const json& parent, const string& path, const char* key) {
    if (!parent.contains(key)) return nullptr;
    const json& value = parent[key];
    if (!value.is_object()) fail(join(path, key), "must be an object");
    return &value;
}

void checkKeys(const json& object, const string& path, initializer_list<const char*> allowed) {
    for (auto it = object.begin(); it != object.end(); ++it) {
        const bool known = any_of(allowed.begin(), allowed.end(), [&](const char* a) { return it.key() == a; });
        if (!known) fail(join(path, it.key()), "unknown option");
    }
}

int readInt(const json& object, const string& path, const char* key, int fallback, long min, long max) {
    if (!object.contains(key)) return fallback;
    const json& value = object[key];
    if (!value.is_number_integer()) fail(join(path, key), "must be an integer, got " + value.dump());
    const long long v = value.get<long long>();
    if (v < min || v > max)
        fail(join(path, key), "must be between " + to_string(min) + " and " + to_string(max) + ", got " + to_string(v));
    return static_cast<int>(v);
}

double readNumber(const json& object, const string& path, const char* key, double fallback, double min, double max) {
    if (!object.contains(key)) return fallback;
    const json& value = object[key];
    if (!value.is_number()) fail(join(path, key), "must be a number, got " + value.dump());
    const double v = value.get<double>();
    if (!(v >= min && v <= max)) {   // also rejects NaN
        ostringstream message;
        message << "must be between " << min << " and " << max << ", got " << v;
        fail(join(path, key), message.str());
    }
    return v;
}

string readString(const json& object, const string& path, const char* key, const string& fallback) {
    if (!object.contains(key)) return fallback;
    const json& value = object[key];
    if (!value.is_string()) fail(join(path, key), "must be a string, got " + value.dump());
    return value.get<string>();
}

// A coordinate: a JSON number or a decimal string. Strings keep every digit, which deep zooms need.
string readCoordinate(const json& value, const string& path) {
    if (value.is_string()) return value.get<string>();
    if (value.is_number()) return value.dump();
    fail(path, "must be a number or a decimal string, got " + value.dump());
}

double parseDouble(const string& text, const string& path) {
    try {
        size_t parsed = 0;
        const double v = stod(text, &parsed);
        if (parsed != text.size() || !isfinite(v)) throw invalid_argument("not a number");
        return v;
    } catch (const exception&) {
        fail(path, "'" + text + "' is not a number");
    }
}

string lowerLettersOnly(const string& s) {
    string out;
    for (const char c : s)
        if (isalnum(static_cast<unsigned char>(c))) out += static_cast<char>(tolower(static_cast<unsigned char>(c)));
    return out;
}

int readPalette(const json& color, const string& path) {
    if (paletteNames.size() != palettes.size()) throw logic_error("paletteNames does not match palettes");
    if (!color.contains("palette")) return 0;
    const json& value = color["palette"];
    const string where = join(path, "palette");
    if (value.is_number_integer()) {
        const long long id = value.get<long long>();
        if (id < 0 || id >= static_cast<long long>(palettes.size()))
            fail(where, "must be between 0 and " + to_string(palettes.size() - 1) + ", got " + to_string(id));
        return static_cast<int>(id);
    }
    if (value.is_string()) {
        const string wanted = lowerLettersOnly(value.get<string>());
        for (size_t i = 0; i < paletteNames.size(); i++)
            if (lowerLettersOnly(paletteNames[i]) == wanted) return static_cast<int>(i);
        string names;
        for (const string& n : paletteNames) names += (names.empty() ? "" : ", ") + n;
        fail(where, "unknown palette " + value.dump() + " (available: " + names + ")");
    }
    fail(where, "must be a palette id or name, got " + value.dump());
}

// ---------------------------------------------------------------------------------------------------------
// The request tree -> MandelbrotConfig. The only place that knows the format.
// ---------------------------------------------------------------------------------------------------------

MandelbrotConfig configFromRequest(const json& request) {
    if (!request.is_object()) fail("request", "must be a JSON object");
    checkKeys(request, "", {"version", "mode", "output", "saveEscape", "recolourFrom", "image", "view", "precision", "iteration", "color"});

    MandelbrotConfig config;
    readInt(request, "", "version", REQUEST_VERSION, REQUEST_VERSION, REQUEST_VERSION);

    try {
        config.mode = modeFromString(readString(request, "", "mode", modeToString(config.mode)));
    } catch (const invalid_argument& e) {
        fail("mode", e.what());
    }
    config.outputFilename = readString(request, "", "output", config.outputFilename);
    config.saveEscapeFile = readString(request, "", "saveEscape", "");
    config.recolourFrom = readString(request, "", "recolourFrom", "");
    const bool recolour = !config.recolourFrom.empty();
    if (recolour) {
        // The escape file already holds the result of the iteration, so nothing that shapes it can be given.
        for (const char* key : {"mode", "saveEscape", "image", "view", "precision", "iteration"})
            if (request.contains(key)) fail(key, "does not apply when recolouring (recolourFrom): it comes from the escape file");
    }

    if (const json* image = optionalObject(request, "", "image")) {
        checkKeys(*image, "image", {"width", "height", "samples"});
        config.imageWidth  = readInt(*image, "image", "width",  config.imageWidth,  1, MAX_IMAGE_SIDE);
        config.imageHeight = readInt(*image, "image", "height", config.imageHeight, 1, MAX_IMAGE_SIDE);
        config.samples     = readInt(*image, "image", "samples", config.samples,    1, 1024);
    }

    if (const json* iteration = optionalObject(request, "", "iteration")) {
        checkKeys(*iteration, "iteration", {"maxIter", "escapeRadius"});
        config.maxIter      = readInt(*iteration, "iteration", "maxIter", config.maxIter, 1, 1000000000);
        config.escapeRadius = readNumber(*iteration, "iteration", "escapeRadius", config.escapeRadius,
                                         MIN_ESCAPE_RADIUS, MAX_ESCAPE_RADIUS);
    }

    // precision: "double" or {"type": "double"|"fixed", "fractionWords": N}
    string precisionType = "double";
    if (request.contains("precision")) {
        const json& precision = request["precision"];
        if (precision.is_string()) {
            precisionType = precision.get<string>();
        } else if (precision.is_object()) {
            checkKeys(precision, "precision", {"type", "fractionWords"});
            precisionType = readString(precision, "precision", "type", precision.contains("fractionWords") ? "fixed" : "double");
            config.fractionWords = readInt(precision, "precision", "fractionWords", config.fractionWords,
                                           fpa::MIN_FRACTION_PART, fpa::MAX_FRACTION_PART);
            if (precisionType == "double" && precision.contains("fractionWords"))
                fail("precision.fractionWords", "only applies to precision type \"fixed\"");
        } else {
            fail("precision", "must be \"double\", \"fixed\" or an object");
        }
        if (precisionType != "double" && precisionType != "fixed")
            fail("precision.type", "must be \"double\" or \"fixed\", got \"" + precisionType + "\"");
    }
    config.useHighPrecision = precisionType == "fixed";

    // view (required): two numbers or decimal strings for each axis
    if (!recolour && !request.contains("view")) fail("view", "is required (view.re and view.im, each as [start, end])");
    const json* view = recolour ? nullptr : optionalObject(request, "", "view");
    if (view) checkKeys(*view, "view", {"re", "im"});
    string coordinates[2][2];
    const char* axes[2] = {"re", "im"};
    for (int a = 0; view && a < 2; a++) {
        const string where = join("view", axes[a]);
        if (!view->contains(axes[a])) fail(where, "is required, as [start, end]");
        const json& pair = (*view)[axes[a]];
        if (!pair.is_array() || pair.size() != 2) fail(where, "must be an array of two values [start, end]");
        for (int k = 0; k < 2; k++) coordinates[a][k] = readCoordinate(pair[k], where + "[" + to_string(k) + "]");
    }
    fpa::uint* words[2][2] = {{config.reStartHP, config.reEndHP}, {config.imStartHP, config.imEndHP}};
    double* numbers[2][2] = {{&config.reStart, &config.reEnd}, {&config.imStart, &config.imEnd}};
    for (int a = 0; view && a < 2; a++) {
        for (int k = 0; k < 2; k++) {
            const string where = join("view", axes[a]) + "[" + to_string(k) + "]";
            if (config.useHighPrecision) {
                try {
                    fpa::decimalToFixedPoint(coordinates[a][k], words[a][k], config.fractionWords);
                } catch (const exception& e) {
                    fail(where, e.what());
                }
            } else {
                *numbers[a][k] = parseDouble(coordinates[a][k], where);
            }
        }
    }

    // color: the method decides which settings exist
    if (const json* color = optionalObject(request, "", "color")) {
        const string method = readString(*color, "color", "method", "cyclic");
        if (method == "cyclic") {
            checkKeys(*color, "color", {"method", "palette", "phase", "length", "spread"});
            config.colorMethod = ColorMethod::CYCLIC;
            config.paletteLength = readInt(*color, "color", "length", config.paletteLength, 1, 1000000);
            config.colorSpread = readNumber(*color, "color", "spread", config.colorSpread, 0.0, MAX_COLOR_SPREAD);
        } else if (method == "histogram") {
            for (const char* key : {"length", "spread"})
                if (color->contains(key))
                    fail(join("color", key), "only applies to the cyclic color method, not histogram");
            checkKeys(*color, "color", {"method", "palette", "phase", "cycles"});
            config.colorMethod = ColorMethod::HISTOGRAM;
            config.histogramCycles = readNumber(*color, "color", "cycles", config.histogramCycles, 1e-6, MAX_HISTOGRAM_CYCLES);
        } else {
            fail("color.method", "must be \"cyclic\" or \"histogram\", got \"" + method + "\"");
        }
        config.colorPhase = readNumber(*color, "color", "phase", config.colorPhase, 0.0, 1.0);
        config.paletteId = readPalette(*color, "color");
    }
    return config;
}

// ---------------------------------------------------------------------------------------------------------
// Command-line flags -> the same request tree. A flag sets one value at a path of the tree, so the flags and
// the JSON request cannot drift apart: the flag --color-spread is the option color.spread.
// ---------------------------------------------------------------------------------------------------------

enum class Kind { TEXT, INT, NUMBER, RANGE, SIZE, PALETTE };

struct Flag {
    const char* name;
    Kind kind;
    vector<const char*> path;   // where the value goes in the request (RANGE: the array; SIZE: image)
};

const Flag FLAGS[] = {
    {"--mode",             Kind::TEXT,    {"mode"}},
    {"--output",           Kind::TEXT,    {"output"}},
    {"--save-escape",      Kind::TEXT,    {"saveEscape"}},
    {"--recolour-from",    Kind::TEXT,    {"recolourFrom"}},
    {"--size",             Kind::SIZE,    {"image"}},
    {"--samples",          Kind::INT,     {"image", "samples"}},
    {"--re",               Kind::RANGE,   {"view", "re"}},
    {"--im",               Kind::RANGE,   {"view", "im"}},
    {"--precision",        Kind::TEXT,    {"precision", "type"}},
    {"--fraction-words",   Kind::INT,     {"precision", "fractionWords"}},
    {"--max-iter",         Kind::INT,     {"iteration", "maxIter"}},
    {"--escape-radius",    Kind::NUMBER,  {"iteration", "escapeRadius"}},
    {"--color",            Kind::TEXT,    {"color", "method"}},
    {"--palette",          Kind::PALETTE, {"color", "palette"}},
    {"--palette-length",   Kind::INT,     {"color", "length"}},
    {"--color-spread",     Kind::NUMBER,  {"color", "spread"}},
    {"--color-phase",      Kind::NUMBER,  {"color", "phase"}},
    {"--histogram-phase",  Kind::NUMBER,  {"color", "phase"}},   // the old name of --color-phase
    {"--histogram-cycles", Kind::NUMBER,  {"color", "cycles"}},
};

json& at(json& root, const vector<const char*>& path) {
    json* node = &root;
    for (const char* key : path) {
        if (!node->is_object() && !node->is_null()) throw RequestError(string("cannot set ") + key + ": its parent is not an object");
        node = &(*node)[key];
    }
    return *node;
}

bool allDigits(const string& s) {
    return !s.empty() && all_of(s.begin(), s.end(), [](unsigned char c) { return isdigit(c); });
}

void applyFlag(json& request, const Flag& flag, const string& value) {
    const string name = flag.name;
    try {
        size_t parsed = 0;
        switch (flag.kind) {
            case Kind::TEXT:
                at(request, flag.path) = value;
                break;
            case Kind::INT: {
                const long long v = stoll(value, &parsed);
                if (parsed != value.size()) throw invalid_argument("");
                at(request, flag.path) = v;
                break;
            }
            case Kind::NUMBER: {
                const double v = stod(value, &parsed);
                if (parsed != value.size()) throw invalid_argument("");
                at(request, flag.path) = v;
                break;
            }
            case Kind::RANGE: {   // START:END, written with a colon so that a negative START is not mistaken for a flag
                const size_t colon = value.find(':');
                if (colon == string::npos || colon == 0 || colon + 1 == value.size())
                    throw RequestError(name + " must be START:END, for example " + name + "=-2:1 (got '" + value + "')");
                at(request, flag.path) = json::array({value.substr(0, colon), value.substr(colon + 1)});
                break;
            }
            case Kind::SIZE: {    // WIDTHxHEIGHT
                const size_t x = value.find_first_of("xX");
                if (x == string::npos || !allDigits(value.substr(0, x)) || !allDigits(value.substr(x + 1)))
                    throw RequestError(name + " must be WIDTHxHEIGHT, for example " + name + "=1800x1200 (got '" + value + "')");
                at(request, flag.path)["width"]  = stoll(value.substr(0, x));
                at(request, flag.path)["height"] = stoll(value.substr(x + 1));
                break;
            }
            case Kind::PALETTE:
                if (allDigits(value)) at(request, flag.path) = stoll(value);
                else                  at(request, flag.path) = value;
                break;
        }
    } catch (const RequestError&) {
        throw;
    } catch (const exception&) {
        throw RequestError(name + " must be " + (flag.kind == Kind::INT ? "an integer" : "a number") + ", got '" + value + "'");
    }
}

json parseJson(const string& text, const string& source) {
    try {
        return json::parse(text);
    } catch (const json::parse_error& e) {
        throw RequestError(source + " is not valid JSON: " + e.what());
    }
}

json loadRequestFile(const string& path) {
    ostringstream text;
    if (path == "-") {
        text << cin.rdbuf();
    } else {
        ifstream file(path, ios::binary);
        if (!file) throw RequestError("cannot open request file '" + path + "'");
        text << file.rdbuf();
    }
    return parseJson(text.str(), path == "-" ? "the request on stdin" : "request file '" + path + "'");
}

json requestFromCommandLine(int argc, char* argv[]) {
    json request = json::object();
    vector<pair<const Flag*, string>> flags;
    bool fractionWordsGiven = false;

    for (int i = 1; i < argc; i++) {
        const string arg = argv[i];
        string name = arg, value;
        bool hasValue = false;
        const size_t equals = arg.find('=');
        if (arg.rfind("--", 0) == 0 && equals != string::npos) {
            name = arg.substr(0, equals);
            value = arg.substr(equals + 1);
            hasValue = true;
        }
        if (arg.rfind("--", 0) != 0)
            throw RequestError("unexpected argument '" + arg + "'. Options are named, for example --mode and --output; "
                               "run with --help to see them");
        if (!hasValue) {
            if (i + 1 >= argc) throw RequestError(name + " needs a value");
            value = argv[++i];
        }

        if (name == "--request") {
            request = loadRequestFile(value);
            continue;
        }
        const Flag* flag = nullptr;
        for (const Flag& f : FLAGS)
            if (name == f.name) flag = &f;
        if (!flag) throw RequestError("unknown option '" + name + "'. Run with --help to see the options");
        if (name == "--fraction-words") fractionWordsGiven = true;
        flags.emplace_back(flag, value);
    }

    // Flags override the request file, whatever their order on the command line.
    for (const auto& [flag, value] : flags) applyFlag(request, *flag, value);
    // --fraction-words alone means fixed point.
    if (fractionWordsGiven && request.is_object() && !(request.contains("precision") && request["precision"].contains("type")))
        request["precision"]["type"] = "fixed";
    return request;
}

}  // namespace

optional<MandelbrotConfig> parseCommandLine(int argc, char* argv[]) {
    try {
        return configFromRequest(requestFromCommandLine(argc, argv));
    } catch (const RequestError& e) {
        cerr << "Error: " << e.what() << "\n";
        return nullopt;
    } catch (const json::exception& e) {
        cerr << "Error: invalid request: " << e.what() << "\n";
        return nullopt;
    }
}

optional<MandelbrotConfig> parseRequestJson(const string& jsonText) {
    try {
        return configFromRequest(parseJson(jsonText, "the request"));
    } catch (const RequestError& e) {
        cerr << "Error: " << e.what() << "\n";
        return nullopt;
    } catch (const json::exception& e) {
        cerr << "Error: invalid request: " << e.what() << "\n";
        return nullopt;
    }
}

void printConfig(const MandelbrotConfig& config) {
    constexpr int W = 36;
    const auto line = [&](const string& label, const auto& value) { cout << left << setw(W) << (label + ":") << value << "\n"; };
    const bool recolour = !config.recolourFrom.empty();
    if (recolour) line("Recolouring", config.recolourFrom);
    else          line("Mode", modeToString(config.mode));
    line("Output file", config.outputFilename);
    if (!config.saveEscapeFile.empty()) line("Escape file", config.saveEscapeFile);
    if (!recolour) {
        line("Image size", to_string(config.imageWidth) + "x" + to_string(config.imageHeight));
        line("Samples", config.samples);
        line("Max iterations", config.maxIter);
        line("Escape radius", config.escapeRadius);
        line("Precision", config.useHighPrecision ? "fixed point, " + to_string(config.fractionWords) + " fraction words" : "double");
        if (config.useHighPrecision) {
            const auto words = [&](const fpa::uint* v) {
                string out = "{";
                for (int i = 0; i < fpa::fpSize(config.fractionWords); i++) out += (i ? "," : "") + to_string(v[i]);
                return out + "}";
            };
            line("reStartHP", words(config.reStartHP));
            line("reEndHP", words(config.reEndHP));
            line("imStartHP", words(config.imStartHP));
            line("imEndHP", words(config.imEndHP));
        } else {
            const auto exact = [](double v) { ostringstream s; s << setprecision(17) << v; return s.str(); };
            line("reStart", exact(config.reStart));
            line("reEnd", exact(config.reEnd));
            line("imStart", exact(config.imStart));
            line("imEnd", exact(config.imEnd));
        }
    }
    line("Palette", paletteNames[static_cast<size_t>(config.paletteId)]);
    if (config.colorMethod == ColorMethod::CYCLIC) {
        line("Color method", "cyclic");
        line("Palette length", config.paletteLength);
        line("Color spread", config.colorSpread);
        line("Color phase", config.colorPhase);
    } else {
        line("Color method", "histogram");
        line("Color phase", config.colorPhase);
        line("Histogram cycles", config.histogramCycles);
    }
}

string usageText(const char* program) {
    ostringstream u;
    u << "Usage: " << program << " --output FILE --re START:END --im START:END [options]\n"
      << "       " << program << " --request FILE|-  [options]      (a JSON request; options override it)\n"
      << "       " << program << " --list-modes | --help\n\n"
      << "Options (the JSON path of each is in brackets; every option has a default except the view):\n"
      << "  --mode M              CPU_PARALLEL | OPENCL_LOCAL | CUDA_LOCAL | CUDA_REMOTE   [mode]\n"
      << "  --output FILE         PNG to write, default ./mandelbrot_set.png                [output]\n"
      << "  --save-escape FILE    also save the escape results, to recolour later           [saveEscape]\n"
      << "  --recolour-from FILE  paint a saved escape file again without iterating: only the\n"
      << "                        colour options and --output apply; the size comes from the file  [recolourFrom]\n"
      << "  --size WxH            image size in pixels, default 900x600                     [image.width, image.height]\n"
      << "  --samples N           samples per pixel (anti-aliasing), default 10             [image.samples]\n"
      << "  --re START:END        real range, decimal numbers (required)                    [view.re]\n"
      << "  --im START:END        imaginary range (required)                                [view.im]\n"
      << "  --precision P         double (default) | fixed                                  [precision.type]\n"
      << "  --fraction-words N    fixed point size, " << fpa::MIN_FRACTION_PART << ".." << fpa::MAX_FRACTION_PART
      << ", default " << fpa::FRACTION_PART << "; implies --precision fixed  [precision.fractionWords]\n"
      << "  --max-iter N          iteration limit, default 1000                             [iteration.maxIter]\n"
      << "  --escape-radius R     stop once |z| > R, " << MIN_ESCAPE_RADIUS << ".." << MAX_ESCAPE_RADIUS
      << ", default " << DEFAULT_ESCAPE_RADIUS << "                [iteration.escapeRadius]\n"
      << "  --color METHOD        cyclic (default) | histogram                              [color.method]\n"
      << "  --palette P           palette id or name, default 0 (Navy)                      [color.palette]\n"
      << "  --color-phase P       where in the palette the colouring starts, 0..1, default 0 [color.phase]\n"
      << "  cyclic only:\n"
      << "  --palette-length N    iterations per palette cycle, default 256                 [color.length]\n"
      << "  --color-spread S      spread packed colours with a log, 0.." << MAX_COLOR_SPREAD << ", default 0           [color.spread]\n"
      << "  histogram only:\n"
      << "  --histogram-cycles C  how many times the palette repeats, default 1             [color.cycles]\n\n"
      << "Palettes:";
    for (size_t i = 0; i < paletteNames.size(); i++) u << (i ? ", " : " ") << i << " " << paletteNames[i];
    u << "\n\nFor a deep zoom give the coordinates with every digit, as text in a JSON request (\"re\": [\"-0.1530048850375...\", ...])\n"
      << "or as a flag value (--re=-0.1530048850375...:-0.1528096952875...); fixed point converts them exactly.\n\n"
      << "Example:\n  " << program << " --output out.png --re=-2.2:0.8 --im=-1:1 --size 1800x1200 --max-iter 700\n";
    return u.str();
}
