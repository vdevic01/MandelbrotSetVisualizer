#include "CUDARemoteIterationCalculator.h"

#include <cpr/cpr.h>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <vector>

#include "base64.h"
#include "IterationCalculator.h"

using namespace std;

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

void CUDARemoteIterationCalculator::decodeResponse(const string& response, vector<int>& iters) {
    const double samplingMs = extractJsonDouble(response, "sampling_time_ms");
    const double kernelMs   = extractJsonDouble(response, "execution_time_ms");
    if (samplingMs > 0.0 || kernelMs > 0.0)
        cout << fixed << setprecision(2)
             << "Remote sampling time: " << samplingMs << " ms\n"
             << "Remote kernel time:   " << kernelMs   << " ms\n";

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

vector<int> CUDARemoteIterationCalculator::calculate(
    double reStart, double reEnd, double imStart, double imEnd,
    int width, int height, int samples, unsigned int maxIter) const
{
    ostringstream json;
    json << fixed << setprecision(17)
         << "{\"use_hp\":0"
         << ",\"max_iter\":" << maxIter
         << ",\"samples\":" << samples
         << ",\"width\":" << width
         << ",\"height\":" << height
         << ",\"re_start\":" << reStart
         << ",\"re_end\":" << reEnd
         << ",\"im_start\":" << imStart
         << ",\"im_end\":" << imEnd
         << "}";

    const string body = "{\"input\":" + json.str() + "}";
    cout << "Sending LP boundary to RunPod...\n";

    const string response = httpPost(body);
    vector<int> iters(static_cast<size_t>(width) * height * samples);
    decodeResponse(response, iters);
    return iters;
}

vector<int> CUDARemoteIterationCalculator::calculateHP(
    const fpa::uint reStart[fpa::FP_SIZE], const fpa::uint reEnd[fpa::FP_SIZE],
    const fpa::uint imStart[fpa::FP_SIZE], const fpa::uint imEnd[fpa::FP_SIZE],
    int width, int height, int samples, unsigned int maxIter) const
{
    ostringstream json;
    json << "{\"use_hp\":1"
         << ",\"max_iter\":" << maxIter
         << ",\"samples\":" << samples
         << ",\"width\":" << width
         << ",\"height\":" << height;
    for (int i = 0; i < fpa::FP_SIZE; i++) {
        json << ",\"re_start_" << i << "\":" << reStart[i]
             << ",\"re_end_"   << i << "\":" << reEnd[i]
             << ",\"im_start_" << i << "\":" << imStart[i]
             << ",\"im_end_"   << i << "\":" << imEnd[i];
    }
    json << "}";

    const string body = "{\"input\":" + json.str() + "}";
    cout << "Sending HP boundary to RunPod...\n";

    const string response = httpPost(body);
    vector<int> iters(static_cast<size_t>(width) * height * samples);
    decodeResponse(response, iters);
    return iters;
}
