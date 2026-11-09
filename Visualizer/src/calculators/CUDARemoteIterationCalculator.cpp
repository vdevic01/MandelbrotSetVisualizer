#include "CUDARemoteIterationCalculator.h"

#include <cpr/cpr.h>
#include <chrono>
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

// What the user can do about an HTTP failure, for the error message.
static string statusHint(long status) {
    switch (status) {
        case 401: case 403: return "check the API key in Settings";
        case 404: return "check the endpoint URL in Settings";
        case 502: case 503: case 504: return "the worker may still be starting or is overloaded; try again";
        default: return status >= 500 ? "the worker reported an error" : "unexpected response";
    }
}

string CUDARemoteIterationCalculator::httpPost(const string& body) const {
    cout << "Remote: POST " << endpoint_ << " (" << body.size() << " bytes), waiting for the worker...\n" << flush;
    const auto start = chrono::steady_clock::now();

    const cpr::Response r = cpr::Post(
        cpr::Url{endpoint_},
        cpr::Bearer{apiKey_},
        cpr::Body{body},
        cpr::Header{{"Content-Type", "application/json"}},
        cpr::Timeout{300000});

    const long long elapsedMs =
        chrono::duration_cast<chrono::milliseconds>(chrono::steady_clock::now() - start).count();

    if (r.error) {
        string hint;
        if (r.error.code == cpr::ErrorCode::OPERATION_TIMEDOUT)
            hint = " (no response within 300 s; the worker may be cold-starting or busy)";
        else if (r.error.code == cpr::ErrorCode::COULDNT_CONNECT || r.error.code == cpr::ErrorCode::COULDNT_RESOLVE_HOST)
            hint = " (could not reach the host; check the endpoint URL and your network)";
        else if (r.error.code == cpr::ErrorCode::URL_MALFORMAT || r.error.code == cpr::ErrorCode::UNSUPPORTED_PROTOCOL)
            hint = " (the endpoint URL is not valid; it must start with https:// or http://)";
        throw runtime_error("HTTP request to " + endpoint_ + " failed after " + to_string(elapsedMs) + " ms: " +
                            r.error.message + hint);
    }

    cout << "Remote: HTTP " << r.status_code << " after " << elapsedMs << " ms (" << r.text.size() << " bytes)\n" << flush;

    if (r.status_code < 200 || r.status_code >= 300) {
        throw runtime_error("Worker returned HTTP " + to_string(r.status_code) + " (" + statusHint(r.status_code) +
                            "): " + r.text.substr(0, 400));
    }

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
    int width, int height, int samples, unsigned int maxIter, double escapeRadius) const
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
         << ",\"escape_radius\":" << escapeRadius
         << "}";

    const string body = json.str();
    cout << "Sending LP boundary to RunPod...\n";

    const string response = httpPost(body);
    vector<int> iters(static_cast<size_t>(width) * height * samples);
    decodeResponse(response, iters);
    return iters;
}

vector<int> CUDARemoteIterationCalculator::calculateHP(
    const fpa::uint* reStart, const fpa::uint* reEnd,
    const fpa::uint* imStart, const fpa::uint* imEnd,
    int fractionWords,
    int width, int height, int samples, unsigned int maxIter, double escapeRadius) const
{
    const int words = fpa::fpSize(fractionWords);
    ostringstream json;
    json << "{\"use_hp\":1"
         << ",\"max_iter\":" << maxIter
         << ",\"samples\":" << samples
         << ",\"width\":" << width
         << ",\"height\":" << height
         << ",\"fraction_words\":" << fractionWords
         << ",\"escape_radius\":" << setprecision(17) << escapeRadius;
    for (int i = 0; i < words; i++) {
        json << ",\"re_start_" << i << "\":" << reStart[i]
             << ",\"re_end_"   << i << "\":" << reEnd[i]
             << ",\"im_start_" << i << "\":" << imStart[i]
             << ",\"im_end_"   << i << "\":" << imEnd[i];
    }
    json << "}";

    const string body = json.str();
    cout << "Sending HP boundary to RunPod...\n";

    const string response = httpPost(body);
    vector<int> iters(static_cast<size_t>(width) * height * samples);
    decodeResponse(response, iters);
    return iters;
}
