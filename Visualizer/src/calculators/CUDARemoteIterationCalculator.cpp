#include "CUDARemoteIterationCalculator.h"

#include <cpr/cpr.h>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <vector>

#include "base64.h"

using namespace std;

// ---------------------------------------------------------------------------
// Minimal JSON field extractors (response format is under our control)
// ---------------------------------------------------------------------------

static string extractJsonString(const string& json, const string& key) {
    const string needle = "\"" + key + "\":\"";
    size_t pos = json.find(needle);
    if (pos == string::npos) return "";
    pos += needle.size();
    size_t end = json.find('"', pos);
    return json.substr(pos, end - pos);
}

static double extractJsonDouble(const string& json, const string& key) {
    const string needle = "\"" + key + "\":";
    size_t pos = json.find(needle);
    if (pos == string::npos) return 0.0;
    pos += needle.size();
    try { return stod(json.substr(pos)); } catch (...) { return 0.0; }
}

// ---------------------------------------------------------------------------
// HTTP POST via cpr
// ---------------------------------------------------------------------------

string CUDARemoteIterationCalculator::httpPost(const string& body) const {
    const cpr::Response r = cpr::Post(
        cpr::Url{endpoint_},
        cpr::Bearer{apiKey_},
        cpr::Body{body},
        cpr::Header{{"Content-Type", "application/json"}},
        cpr::Timeout{300000});

    if (r.error)
        throw runtime_error("HTTP request failed: " + r.error.message);

    return r.text;
}

// ---------------------------------------------------------------------------
// Response decoding
// ---------------------------------------------------------------------------

void CUDARemoteIterationCalculator::decodeResponse(const string& response, vector<int>& iters) {
    double kernelMs = extractJsonDouble(response, "execution_time_ms");
    if (kernelMs > 0.0)
        cout << fixed << setprecision(2) << "Remote kernel time: " << kernelMs << " ms\n";

    const string b64 = extractJsonString(response, "iterations");
    if (b64.empty()) {
        throw runtime_error(
            "No 'iterations' field in RunPod response.\n"
            "Response (first 400 chars): " + response.substr(0, 400));
    }

    const vector<uint8_t> raw = base64::decode(b64);
    const size_t expectedBytes = iters.size() * sizeof(int32_t);
    if (raw.size() != expectedBytes) {
        throw runtime_error(
            "Response size mismatch: expected " + to_string(expectedBytes) +
            " bytes, got " + to_string(raw.size()));
    }

    memcpy(iters.data(), raw.data(), raw.size());
}

// ---------------------------------------------------------------------------
// calculate() overloads
// ---------------------------------------------------------------------------

template<typename T>
static string buildRequest(const vector<T>& points, unsigned int maxIter, int useHp) {
    const auto* raw = reinterpret_cast<const uint8_t*>(points.data());
    const string b64 = base64::encode(raw, points.size() * sizeof(T));

    ostringstream json;
    json << R"({"input":{"use_hp":)" << useHp
         << ",\"max_iter\":"           << maxIter
         << R"(,"points":")"           << b64 << "\"}}";
    return json.str();
}

void CUDARemoteIterationCalculator::calculate(
    const vector<Complex>& points, vector<int>& iters, unsigned int maxIter) const
{
    cout << "Sending " << points.size() << " LP points to RunPod...\n";
    decodeResponse(httpPost(buildRequest(points, maxIter, 0)), iters);
}

void CUDARemoteIterationCalculator::calculate(
    const vector<ComplexHP>& points, vector<int>& iters, unsigned int maxIter) const
{
    cout << "Sending " << points.size() << " HP points to RunPod...\n";
    decodeResponse(httpPost(buildRequest(points, maxIter, 1)), iters);
}
