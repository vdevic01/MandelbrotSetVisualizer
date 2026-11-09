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

__global__ void calculateIters(const Complex* points, int32_t* iterations) {
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
	for (uint32_t i = 0; i < deviceMaxIter; i++) {
		y = (x + x) * y + y0;
		x = x2 - y2 + x0;
		x2 = x * x;
		y2 = y * y;
		if (x2 + y2 > 4) {
			result = i;
			break;
		}
	}
	iterations[idx] = result;

	return;
}

// High-precision kernel for F fraction words. `points` holds, per point, the real words followed by the
// imaginary words (1 + F words each, most significant first).
template<int F>
__global__ void calculateItersHp(const uint32_t* points, int32_t* iterations)
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
	uint32_t fourFixed[S] = { 4 };   // 4.0: the whole word is 4, the fraction words are 0

	int32_t result = -1;
	for (size_t i = 0; i < deviceMaxIter; i++) {
		fpa::core::add<F>(x, x, temp);
		fpa::core::mul<F>(temp, y, temp);
		fpa::core::add<F>(temp, y0, y);
		//y = (x + x) * y + y0;

		fpa::core::sub<F>(x2, y2, temp);
		fpa::core::add<F>(temp, x0, x);
		//x = x2 - y2 + x0;

		fpa::core::mul<F>(x, x, x2);
		//x2 = x * x;

		fpa::core::mul<F>(y, y, y2);
		//y2 = y * y;

		fpa::core::add<F>(x2, y2, temp);
		if (fpa::core::gt<F>(temp, fourFixed)) {
			result = i;
			break;
		}
	}

	iterations[idx] = result;

	return;
}

template<typename T_ComplexType>
void calculateItersInternal(const T_ComplexType* points, const size_t N, std::vector<int>& iters, const unsigned int maxIter) {

	T_ComplexType* devicePointsBuffer;
	int* deviceItersBuffer;

	cudaMalloc(&devicePointsBuffer, N * sizeof(T_ComplexType));
	cudaMalloc(&deviceItersBuffer, N * sizeof(int));

	cudaMemcpy(devicePointsBuffer, points, N * sizeof(T_ComplexType), cudaMemcpyHostToDevice);
	cudaMemcpyToSymbol(deviceMaxIter, &maxIter, sizeof(unsigned int));
	cudaMemcpyToSymbol(deviceN, &N, sizeof(size_t));

	const int blockSize = 128;
	int numBlocks = (N + blockSize - 1) / blockSize;

	calculateIters << <numBlocks, blockSize >> > (devicePointsBuffer, deviceItersBuffer);

	cudaMemcpy(iters.data(), deviceItersBuffer, N * sizeof(int), cudaMemcpyDeviceToHost);

	cudaFree(devicePointsBuffer);
	cudaFree(deviceItersBuffer);
}

template<int F>
void calculateItersHpInternal(const HpPoints& points, std::vector<int>& iters, const unsigned int maxIter) {
	const size_t N = points.size();

	uint32_t* devicePointsBuffer;
	int* deviceItersBuffer;

	cudaMalloc(&devicePointsBuffer, points.words.size() * sizeof(uint32_t));
	cudaMalloc(&deviceItersBuffer, N * sizeof(int));

	cudaMemcpy(devicePointsBuffer, points.words.data(), points.words.size() * sizeof(uint32_t), cudaMemcpyHostToDevice);
	cudaMemcpyToSymbol(deviceMaxIter, &maxIter, sizeof(unsigned int));
	cudaMemcpyToSymbol(deviceN, &N, sizeof(size_t));

	const int blockSize = 128;
	int numBlocks = (N + blockSize - 1) / blockSize;

	calculateItersHp<F><<<numBlocks, blockSize>>>(devicePointsBuffer, deviceItersBuffer);

	cudaMemcpy(iters.data(), deviceItersBuffer, N * sizeof(int), cudaMemcpyDeviceToHost);

	cudaFree(devicePointsBuffer);
	cudaFree(deviceItersBuffer);
}

void CUDAIterationCalculator::calculate(const std::vector<Complex>& points, std::vector<int>& iters, unsigned int maxIter) const {
	calculateItersInternal<Complex>(points.data(), points.size(), iters, maxIter);
}

void CUDAIterationCalculator::calculate(const HpPoints& points, std::vector<int>& iters, unsigned int maxIter) const {
	// The fraction size is a compile-time constant inside the kernel: pick the matching instantiation once per render.
	switch (points.fractionWords) {
		case 1: calculateItersHpInternal<1>(points, iters, maxIter); break;
		case 2: calculateItersHpInternal<2>(points, iters, maxIter); break;
		case 3: calculateItersHpInternal<3>(points, iters, maxIter); break;
		case 4: calculateItersHpInternal<4>(points, iters, maxIter); break;
		case 5: calculateItersHpInternal<5>(points, iters, maxIter); break;
		case 6: calculateItersHpInternal<6>(points, iters, maxIter); break;
		default:
			throw std::invalid_argument("Unsupported fraction size: " + std::to_string(points.fractionWords) + " words");
	}
}
