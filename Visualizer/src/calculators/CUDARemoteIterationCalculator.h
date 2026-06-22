#pragma once

#include <string>
#include <vector>

#include "IterationCalculator.h"

// Implements IterationCalculator over HTTP: serialises the pre-sampled points
// array as base64, POSTs it to a RunPod serverless worker that runs the CUDA
// kernel, and decodes the returned iteration counts.  Sampling always happens
// locally — only the kernel step is offloaded.
class CUDARemoteIterationCalculator : public IterationCalculator {
public:
    CUDARemoteIterationCalculator(std::string endpoint, std::string apiKey)
        : endpoint_(std::move(endpoint)), apiKey_(std::move(apiKey)) {}

    void calculate(const std::vector<Complex>&   points, std::vector<int>& iters, unsigned int maxIter) const override;
    void calculate(const std::vector<ComplexHP>& points, std::vector<int>& iters, unsigned int maxIter) const override;

private:
    std::string endpoint_;
    std::string apiKey_;

    [[nodiscard]] std::string httpPost(const std::string& body) const;
    static void decodeResponse(const std::string& response, std::vector<int>& iters);
};
