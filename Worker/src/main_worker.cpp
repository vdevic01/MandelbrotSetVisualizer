// RunPod CUDA worker binary.
// Reads boundary coordinates + image dimensions from stdin, samples the complex plane,
// runs the CUDA iteration kernel, and writes results to stdout.
//
// LP stdin:  {"use_hp":0,"max_iter":<N>,"samples":<S>,"width":<W>,"height":<H>,
//             "re_start":<f>,"re_end":<f>,"im_start":<f>,"im_end":<f>}
// HP stdin:  {"use_hp":1,"max_iter":<N>,"samples":<S>,"width":<W>,"height":<H>,
//             "re_start_0":<u>,...,"im_end_3":<u>}
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

static double extractJsonDouble(const string& json, const string& key) {
    const string needle = "\"" + key + "\":";
    size_t pos = json.find(needle);
    if (pos == string::npos) throw runtime_error("Missing JSON field: " + key);
    pos += needle.size();
    return stod(json.substr(pos));
}

int main() {
    try {
        const string jsonInput(istreambuf_iterator<char>(cin), {});

        const bool useHp   = extractJsonInt(jsonInput, "use_hp") != 0;
        const int  maxIter = extractJsonInt(jsonInput, "max_iter");
        const int  samples = extractJsonInt(jsonInput, "samples");
        const int  width   = extractJsonInt(jsonInput, "width");
        const int  height  = extractJsonInt(jsonInput, "height");

        CUDAIterationCalculator calc;

        cudaEvent_t evStart, evStop;
        cudaEventCreate(&evStart);
        cudaEventCreate(&evStop);

        string iterB64;

        using clock = chrono::steady_clock;

        vector<int> iters;
        double samplingMs = 0.0;

        if (useHp) {
            fpa::uint reStart[fpa::FP_SIZE], reEnd[fpa::FP_SIZE];
            fpa::uint imStart[fpa::FP_SIZE], imEnd[fpa::FP_SIZE];
            for (int i = 0; i < fpa::FP_SIZE; i++) {
                reStart[i] = extractJsonUint(jsonInput, "re_start_" + to_string(i));
                reEnd[i]   = extractJsonUint(jsonInput, "re_end_"   + to_string(i));
                imStart[i] = extractJsonUint(jsonInput, "im_start_" + to_string(i));
                imEnd[i]   = extractJsonUint(jsonInput, "im_end_"   + to_string(i));
            }

            auto t0 = clock::now();
            const auto points = samplePointsFromComplexPlane(
                height, width, imStart, imEnd, reStart, reEnd, samples);
            samplingMs = chrono::duration<double, milli>(clock::now() - t0).count();

            iters.resize(points.size());
            cudaEventRecord(evStart);
            calc.calculate(points, iters, static_cast<unsigned int>(maxIter));
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
            calc.calculate(points, iters, static_cast<unsigned int>(maxIter));
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
