#pragma once

#include <string>
#include <vector>

#include "FixedPointArithmetics.h"

// Sends boundary coordinates and image dimensions to a RunPod serverless
// worker. Sampling and kernel execution happen remotely — only the small
// boundary description is transmitted, not a pre-sampled points array.
class CUDARemoteIterationCalculator {
public:
    CUDARemoteIterationCalculator(std::string endpoint, std::string apiKey)
        : endpoint_(std::move(endpoint)), apiKey_(std::move(apiKey)) {}

    std::vector<int> calculate(
        double reStart, double reEnd, double imStart, double imEnd,
        int width, int height, int samples, unsigned int maxIter) const;

    // The boundaries are numbers of fpa::fpSize(fractionWords) words each.
    std::vector<int> calculateHP(
        const fpa::uint* reStart, const fpa::uint* reEnd,
        const fpa::uint* imStart, const fpa::uint* imEnd,
        int fractionWords,
        int width, int height, int samples, unsigned int maxIter) const;

private:
    std::string endpoint_;
    std::string apiKey_;

    [[nodiscard]] std::string httpPost(const std::string& body) const;
    static void decodeResponse(const std::string& response, std::vector<int>& iters);
};
