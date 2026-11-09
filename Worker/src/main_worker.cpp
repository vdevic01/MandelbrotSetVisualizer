// RunPod CUDA worker binary.
// Reads boundary coordinates + image dimensions from stdin, samples the complex plane,
// runs the CUDA iteration kernel, and writes results to stdout.
//
// LP stdin:  {"use_hp":0,"max_iter":<N>,"samples":<S>,"width":<W>,"height":<H>,
//             "re_start":<f>,"re_end":<f>,"im_start":<f>,"im_end":<f>}
// HP stdin:  {"use_hp":1,"max_iter":<N>,"samples":<S>,"width":<W>,"height":<H>,"fraction_words":<F>,
//             "re_start_0":<u>,...,"im_end_<F>":<u>}   (F + 1 words per coordinate; fraction_words defaults to 3)
// Both accept an optional "escape_radius":<f> (default 2).
// stdout:    {"execution_time_ms":<ms>,"iterations":"<base64-encoded int32[]>"}

#include <cuda_runtime.h>

#include <chrono>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <vector>

#include "CUDAIterationCalculator.h"
#include "Sampler.h"
#include "base64.h"

using namespace std;

static int extractJsonInt(const string& json, const string& key) {
    const string needle = "\"" + key + "\":";
    size_t pos = json.find(needle);
    if (pos == string::npos) throw runtime_error("Missing JSON field: " + key);
    pos += needle.size();
    return stoi(json.substr(pos));
}

static unsigned int extractJsonUint(const string& json, const string& key) {
    const string needle = "\"" + key + "\":";
    size_t pos = json.find(needle);
    if (pos == string::npos) throw runtime_error("Missing JSON field: " + key);
    pos += needle.size();
    return static_cast<unsigned int>(stoul(json.substr(pos)));
}

// Optional integer field: returns fallback when the key is absent (older clients do not send it).
static int extractJsonIntOr(const string& json, const string& key, int fallback) {
    const string needle = "\"" + key + "\":";
    size_t pos = json.find(needle);
    if (pos == string::npos) return fallback;
    return stoi(json.substr(pos + needle.size()));
}

static double extractJsonDouble(const string& json, const string& key) {
    const string needle = "\"" + key + "\":";
    size_t pos = json.find(needle);
    if (pos == string::npos) throw runtime_error("Missing JSON field: " + key);
    pos += needle.size();
    return stod(json.substr(pos));
}

// Optional number field: returns fallback when the key is absent (older clients do not send it).
static double extractJsonDoubleOr(const string& json, const string& key, double fallback) {
    const string needle = "\"" + key + "\":";
    size_t pos = json.find(needle);
    if (pos == string::npos) return fallback;
    return stod(json.substr(pos + needle.size()));
}

int main() {
    try {
        const string jsonInput(istreambuf_iterator<char>(cin), {});

        const bool useHp   = extractJsonInt(jsonInput, "use_hp") != 0;
        const int  maxIter = extractJsonInt(jsonInput, "max_iter");
        const int  samples = extractJsonInt(jsonInput, "samples");
        const int  width   = extractJsonInt(jsonInput, "width");
        const int  height  = extractJsonInt(jsonInput, "height");
        const double escapeRadius = extractJsonDoubleOr(jsonInput, "escape_radius", DEFAULT_ESCAPE_RADIUS);
        if (!isSupportedEscapeRadius(escapeRadius))
            throw runtime_error("Unsupported escape_radius: " + to_string(escapeRadius));

        CUDAIterationCalculator calc;

        cudaEvent_t evStart, evStop;
        cudaEventCreate(&evStart);
        cudaEventCreate(&evStop);

        string iterB64;

        using clock = chrono::steady_clock;

        vector<int> iters;
        double samplingMs = 0.0;

        if (useHp) {
            // Number format of the coordinates: 1 whole word + fraction_words fraction words (default 3).
            const int fractionWords = extractJsonIntOr(jsonInput, "fraction_words", fpa::FRACTION_PART);
            if (!fpa::isSupportedFractionPart(fractionWords))
                throw runtime_error("Unsupported fraction_words: " + to_string(fractionWords));
            const int words = fpa::fpSize(fractionWords);

            fpa::uint reStart[fpa::MAX_FP_SIZE], reEnd[fpa::MAX_FP_SIZE];
            fpa::uint imStart[fpa::MAX_FP_SIZE], imEnd[fpa::MAX_FP_SIZE];
            for (int i = 0; i < words; i++) {
                reStart[i] = extractJsonUint(jsonInput, "re_start_" + to_string(i));
                reEnd[i]   = extractJsonUint(jsonInput, "re_end_"   + to_string(i));
                imStart[i] = extractJsonUint(jsonInput, "im_start_" + to_string(i));
                imEnd[i]   = extractJsonUint(jsonInput, "im_end_"   + to_string(i));
            }

            auto t0 = clock::now();
            const auto points = samplePointsFromComplexPlane(
                height, width, imStart, imEnd, reStart, reEnd, samples, fractionWords);
            samplingMs = chrono::duration<double, milli>(clock::now() - t0).count();

            iters.resize(points.size());
            cudaEventRecord(evStart);
            calc.calculate(points, iters, static_cast<unsigned int>(maxIter), escapeRadius);
            cudaEventRecord(evStop);
        } else {
            const double reStart = extractJsonDouble(jsonInput, "re_start");
            const double reEnd   = extractJsonDouble(jsonInput, "re_end");
            const double imStart = extractJsonDouble(jsonInput, "im_start");
            const double imEnd   = extractJsonDouble(jsonInput, "im_end");

            auto t0 = clock::now();
            const auto points = samplePointsFromComplexPlane(
                height, width, imStart, imEnd, reStart, reEnd, samples);
            samplingMs = chrono::duration<double, milli>(clock::now() - t0).count();

            iters.resize(points.size());
            cudaEventRecord(evStart);
            calc.calculate(points, iters, static_cast<unsigned int>(maxIter), escapeRadius);
            cudaEventRecord(evStop);
        }

        const auto* raw = reinterpret_cast<const uint8_t*>(iters.data());
        iterB64 = base64::encode(raw, iters.size() * sizeof(int32_t));

        cudaEventSynchronize(evStop);
        float kernelMs = 0.0f;
        cudaEventElapsedTime(&kernelMs, evStart, evStop);
        cudaEventDestroy(evStart);
        cudaEventDestroy(evStop);

        cout << "{\"sampling_time_ms\":" << fixed << setprecision(3) << samplingMs
             << ",\"execution_time_ms\":" << kernelMs
             << ",\"iterations\":\"" << iterB64 << "\"}" << endl;

    } catch (const exception& e) {
        cerr << "Worker error: " << e.what() << "\n";
        return 1;
    }

    return 0;
}
