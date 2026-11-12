#include <cuda_runtime.h>
#include <device_launch_parameters.h>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>

#include "CUDAIterationCalculator.h"
#include "FixedPointShared.h"

// The fixed-point arithmetic itself (add, sub, compare, multiply) is shared with the CPU build:
// fpa::core in FixedPointShared.h, so it is tested once by the unit tests.


__constant__ uint32_t deviceMaxIter;
__constant__ size_t deviceN;
__constant__ double deviceEscapeSq;            // R^2 (double-precision kernel)
__constant__ uint32_t deviceThresholdWhole;    // R^2 as fixed-point words (high-precision kernel)
__constant__ uint32_t deviceThresholdFraction;

__global__ void calculateIters(const Complex* points, int32_t* iterations, float* magSq) {
	const size_t idx = threadIdx.x + blockIdx.x * blockDim.x;
	if (idx >= deviceN)
		return;

	const double x0 = points[idx].real;
	const double y0 = points[idx].imag;

	double x2 = 0;
	double y2 = 0;

	double x = 0;
	double y = 0;

	int32_t result = -1;
	float escapeValue = 0;
	for (uint32_t i = 0; i < deviceMaxIter; i++) {
		y = (x + x) * y + y0;
		x = x2 - y2 + x0;
		x2 = x * x;
		y2 = y * y;
		if (x2 + y2 > deviceEscapeSq) {
			result = i;
			escapeValue = (float)(x2 + y2);
			break;
		}
	}
	iterations[idx] = result;
	magSq[idx] = escapeValue;

	return;
}

// High-precision kernel for F fraction words. `points` holds, per point, the real words followed by the
// imaginary words (1 + F words each, most significant first).
template<int F>
__global__ void calculateItersHp(const uint32_t* points, int32_t* iterations, float* magSq)
{
	constexpr int S = fpa::WHOLE_PART + F;   // words per number

	const size_t idx = threadIdx.x + blockIdx.x * blockDim.x;
	if (idx >= deviceN)
		return;

	const uint32_t* c = points + idx * 2 * S;

	uint32_t x0[S];
	uint32_t y0[S];
	for (int i = 0; i < S; i++) {
		x0[i] = c[i];
		y0[i] = c[S + i];
	}

	uint32_t x2[S] = {};
	uint32_t y2[S] = {};

	uint32_t x[S] = {};
	uint32_t y[S] = {};

	uint32_t temp[S];
	uint32_t threshold[S] = { deviceThresholdWhole, deviceThresholdFraction };   // R^2; the other fraction words are 0

	int32_t result = -1;
	float escapeValue = 0;
	for (size_t i = 0; i < deviceMaxIter; i++) {
		fpa::core::add<F>(x, x, temp);
		fpa::core::mul<F>(temp, y, temp);
		fpa::core::add<F>(temp, y0, y);
		//y = (x + x) * y + y0;

		fpa::core::sub<F>(x2, y2, temp);
		fpa::core::add<F>(temp, x0, x);
		//x = x2 - y2 + x0;

		fpa::core::sqr<F>(x, x2);
		//x2 = x * x;

		fpa::core::sqr<F>(y, y2);
		//y2 = y * y;

		fpa::core::add<F>(x2, y2, temp);
		if (fpa::core::gt<F>(temp, threshold)) {
			result = i;
			// |z|^2 as a float: the whole word and the first fraction word carry more than enough bits
			escapeValue = (float)temp[0] + (float)temp[1] * (1.0f / 4294967296.0f);
			break;
		}
	}

	iterations[idx] = result;
	magSq[idx] = escapeValue;

	return;
}

template<typename T_ComplexType>
void calculateItersInternal(const T_ComplexType* points, const size_t N, EscapeResult& result, const unsigned int maxIter, const double escapeRadius) {

	T_ComplexType* devicePointsBuffer;
	int* deviceItersBuffer;
	float* deviceMagSqBuffer;

	cudaMalloc(&devicePointsBuffer, N * sizeof(T_ComplexType));
	cudaMalloc(&deviceItersBuffer, N * sizeof(int));
	cudaMalloc(&deviceMagSqBuffer, N * sizeof(float));

	const double escapeSq = escapeRadius * escapeRadius;
	cudaMemcpy(devicePointsBuffer, points, N * sizeof(T_ComplexType), cudaMemcpyHostToDevice);
	cudaMemcpyToSymbol(deviceMaxIter, &maxIter, sizeof(unsigned int));
	cudaMemcpyToSymbol(deviceN, &N, sizeof(size_t));
	cudaMemcpyToSymbol(deviceEscapeSq, &escapeSq, sizeof(double));

	const int blockSize = 128;
	int numBlocks = (N + blockSize - 1) / blockSize;

	calculateIters << <numBlocks, blockSize >> > (devicePointsBuffer, deviceItersBuffer, deviceMagSqBuffer);

	cudaMemcpy(result.iters.data(), deviceItersBuffer, N * sizeof(int), cudaMemcpyDeviceToHost);
	cudaMemcpy(result.magSq.data(), deviceMagSqBuffer, N * sizeof(float), cudaMemcpyDeviceToHost);

	cudaFree(devicePointsBuffer);
	cudaFree(deviceItersBuffer);
	cudaFree(deviceMagSqBuffer);
}

template<int F>
void calculateItersHpInternal(const HpPoints& points, EscapeResult& result, const unsigned int maxIter, const double escapeRadius) {
	const size_t N = points.size();

	uint32_t* devicePointsBuffer;
	int* deviceItersBuffer;
	float* deviceMagSqBuffer;

	cudaMalloc(&devicePointsBuffer, points.words.size() * sizeof(uint32_t));
	cudaMalloc(&deviceItersBuffer, N * sizeof(int));
	cudaMalloc(&deviceMagSqBuffer, N * sizeof(float));

	unsigned int thresholdWhole, thresholdFraction;
	escapeThresholdWords(escapeRadius, thresholdWhole, thresholdFraction);

	cudaMemcpy(devicePointsBuffer, points.words.data(), points.words.size() * sizeof(uint32_t), cudaMemcpyHostToDevice);
	cudaMemcpyToSymbol(deviceMaxIter, &maxIter, sizeof(unsigned int));
	cudaMemcpyToSymbol(deviceN, &N, sizeof(size_t));
	cudaMemcpyToSymbol(deviceThresholdWhole, &thresholdWhole, sizeof(unsigned int));
	cudaMemcpyToSymbol(deviceThresholdFraction, &thresholdFraction, sizeof(unsigned int));

	const int blockSize = 128;
	int numBlocks = (N + blockSize - 1) / blockSize;

	calculateItersHp<F><<<numBlocks, blockSize>>>(devicePointsBuffer, deviceItersBuffer, deviceMagSqBuffer);

	cudaMemcpy(result.iters.data(), deviceItersBuffer, N * sizeof(int), cudaMemcpyDeviceToHost);
	cudaMemcpy(result.magSq.data(), deviceMagSqBuffer, N * sizeof(float), cudaMemcpyDeviceToHost);

	cudaFree(devicePointsBuffer);
	cudaFree(deviceItersBuffer);
	cudaFree(deviceMagSqBuffer);
}

void CUDAIterationCalculator::calculate(const std::vector<Complex>& points, EscapeResult& result, unsigned int maxIter, double escapeRadius) const {
	calculateItersInternal<Complex>(points.data(), points.size(), result, maxIter, escapeRadius);
}

void CUDAIterationCalculator::calculate(const HpPoints& points, EscapeResult& result, unsigned int maxIter, double escapeRadius) const {
	// The fraction size is a compile-time constant inside the kernel: pick the matching instantiation once per render.
	switch (points.fractionWords) {
		case 1: calculateItersHpInternal<1>(points, result, maxIter, escapeRadius); break;
		case 2: calculateItersHpInternal<2>(points, result, maxIter, escapeRadius); break;
		case 3: calculateItersHpInternal<3>(points, result, maxIter, escapeRadius); break;
		case 4: calculateItersHpInternal<4>(points, result, maxIter, escapeRadius); break;
		case 5: calculateItersHpInternal<5>(points, result, maxIter, escapeRadius); break;
		case 6: calculateItersHpInternal<6>(points, result, maxIter, escapeRadius); break;
		default:
			throw std::invalid_argument("Unsupported fraction size: " + std::to_string(points.fractionWords) + " words");
	}
}
