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

    std::vector<int> calculateHP(
        const fpa::uint reStart[fpa::FP_SIZE], const fpa::uint reEnd[fpa::FP_SIZE],
        const fpa::uint imStart[fpa::FP_SIZE], const fpa::uint imEnd[fpa::FP_SIZE],
        int width, int height, int samples, unsigned int maxIter) const;

private:
    std::string endpoint_;
    std::string apiKey_;

    [[nodiscard]] std::string httpPost(const std::string& body) const;
    static void decodeResponse(const std::string& response, std::vector<int>& iters);
};
