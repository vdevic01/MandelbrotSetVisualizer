// RunPod CUDA worker binary.
// Reads a JSON job from stdin, runs the CUDA iteration kernel, writes results to stdout.
//
// Stdin:  {"use_hp":<0|1>,"max_iter":<N>,"points":"<base64-encoded Complex[] or ComplexHP[]>"}
// Stdout: {"execution_time_ms":<ms>,"iterations":"<base64-encoded int32[]>"}
//
// execution_time_ms is measured with CUDA events (kernel dispatch only, excluding
// host-device memory transfers and base64 encode/decode).

#include <cuda_runtime.h>

#include <cstdint>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <vector>

#include "CUDAIterationCalculator.h"
#include "base64.h"

using namespace std;

// ---------------------------------------------------------------------------
// Minimal JSON field extractors
// ---------------------------------------------------------------------------

static string extractJsonString(const string& json, const string& key) {
    const string needle = "\"" + key + "\":\"";
    size_t pos = json.find(needle);
    if (pos == string::npos) throw runtime_error("Missing JSON field: " + key);
    pos += needle.size();
    size_t end = json.find('"', pos);
    return json.substr(pos, end - pos);
}

static int extractJsonInt(const string& json, const string& key) {
    const string needle = "\"" + key + "\":";
    size_t pos = json.find(needle);
    if (pos == string::npos) throw runtime_error("Missing JSON field: " + key);
    pos += needle.size();
    return stoi(json.substr(pos));
}

// ---------------------------------------------------------------------------
// Main
// ---------------------------------------------------------------------------

int main() {
    try {
        // Read entire stdin as the JSON request
        const string jsonInput(istreambuf_iterator<char>(cin), {});

        const bool useHp   = extractJsonInt(jsonInput, "use_hp") != 0;
        const int  maxIter = extractJsonInt(jsonInput, "max_iter");
        const string pointsB64 = extractJsonString(jsonInput, "points");
        const vector<uint8_t> rawPoints = base64::decode(pointsB64);

        CUDAIterationCalculator calc;

        cudaEvent_t evStart, evStop;
        cudaEventCreate(&evStart);
        cudaEventCreate(&evStop);

        string iterB64;

        if (useHp) {
            const size_t n = rawPoints.size() / sizeof(ComplexHP);
            vector<ComplexHP> points(n);
            memcpy(points.data(), rawPoints.data(), rawPoints.size());

            vector<int> iters(n);
            cudaEventRecord(evStart);
            calc.calculate(points, iters, static_cast<unsigned int>(maxIter));
            cudaEventRecord(evStop);

            const auto* raw = reinterpret_cast<const uint8_t*>(iters.data());
            iterB64 = base64::encode(raw, iters.size() * sizeof(int32_t));
        } else {
            const size_t n = rawPoints.size() / sizeof(Complex);
            vector<Complex> points(n);
            memcpy(points.data(), rawPoints.data(), rawPoints.size());

            vector<int> iters(n);
            cudaEventRecord(evStart);
            calc.calculate(points, iters, static_cast<unsigned int>(maxIter));
            cudaEventRecord(evStop);

            const auto* raw = reinterpret_cast<const uint8_t*>(iters.data());
            iterB64 = base64::encode(raw, iters.size() * sizeof(int32_t));
        }

        cudaEventSynchronize(evStop);
        float kernelMs = 0.0f;
        cudaEventElapsedTime(&kernelMs, evStart, evStop);
        cudaEventDestroy(evStart);
        cudaEventDestroy(evStop);

        cout << "{\"execution_time_ms\":" << fixed << setprecision(3) << kernelMs
             << ",\"iterations\":\"" << iterB64 << "\"}" << endl;

    } catch (const exception& e) {
        cerr << "Worker error: " << e.what() << "\n";
        return 1;
    }

    return 0;
}
