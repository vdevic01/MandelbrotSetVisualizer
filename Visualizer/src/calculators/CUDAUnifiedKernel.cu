#include <cuda_runtime.h>
#include <device_launch_parameters.h>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>

#include "CUDAIterationCalculator.h"
#include "FixedPointShared.h"

#pragma region Fixed Point Arithmetics

// The arithmetic itself is shared with the CPU build (FixedPointShared.h), so it is tested once by the
// unit tests. These thin wrappers keep the kernel code below unchanged.
constexpr uint32_t FP_SIZE = fpa::FP_SIZE;   // 1 whole word + 3 fraction words

__device__ __forceinline__ void addFixed(const uint32_t* a, const uint32_t* b, uint32_t* c) {
	fpa::core::add<fpa::FRACTION_PART>(a, b, c);
}

__device__ __forceinline__ void incFixed(const uint32_t* a, uint32_t* c) {
	fpa::core::inc<fpa::FRACTION_PART>(a, c);
}

__device__ __forceinline__ void cmplFixed(const uint32_t* a, uint32_t* c) {
	fpa::core::cmpl<fpa::FRACTION_PART>(a, c);
}

__device__ __forceinline__ void subFixed(const uint32_t* a, const uint32_t* b, uint32_t* c) {
	fpa::core::sub<fpa::FRACTION_PART>(a, b, c);
}

__device__ __forceinline__ bool gtFixed(const uint32_t* a, const uint32_t* b) {
	return fpa::core::gt<fpa::FRACTION_PART>(a, b);
}

__device__ __forceinline__ void mulCmplFixed(const uint32_t* a, const uint32_t* b, uint32_t* c) {
	fpa::core::mul<fpa::FRACTION_PART>(a, b, c);
}

#pragma endregion

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

__global__ void calculateIters(const ComplexHP* points, int32_t* iterations)
{
	const size_t idx = threadIdx.x + blockIdx.x * blockDim.x;
	if (idx >= deviceN)
		return;

	ComplexHP c = points[idx];

	uint32_t x0[FP_SIZE];
	uint32_t y0[FP_SIZE];
	for (size_t i = 0; i < FP_SIZE; i++) {
		x0[i] = c.real[i];
		y0[i] = c.imag[i];
	}

	uint32_t x2[FP_SIZE] = { 0,0,0,0 };
	uint32_t y2[FP_SIZE] = { 0,0,0,0 };

	uint32_t x[FP_SIZE] = { 0,0,0,0 };
	uint32_t y[FP_SIZE] = { 0,0,0,0 };

	uint32_t temp[FP_SIZE];
	uint32_t fourFixed[FP_SIZE] = { 4, 0, 0, 0 };

	int32_t result = -1;
	for (size_t i = 0; i < deviceMaxIter; i++) {
		addFixed(x, x, temp);
		mulCmplFixed(temp, y, temp);
		addFixed(temp, y0, y);
		//y = (x + x) * y + y0;

		subFixed(x2, y2, temp);
		addFixed(temp, x0, x);
		//x = x2 - y2 + x0;

		mulCmplFixed(x, x, x2);
		//x2 = x * x;

		mulCmplFixed(y, y, y2);
		//y2 = y * y;

		addFixed(x2, y2, temp);
		if (gtFixed(temp, fourFixed)) {
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

void CUDAIterationCalculator::calculate(const std::vector<Complex>& points, std::vector<int>& iters, unsigned int maxIter) const {
	calculateItersInternal<Complex>(points.data(), points.size(), iters, maxIter);
}

void CUDAIterationCalculator::calculate(const HpPoints& points, std::vector<int>& iters, unsigned int maxIter) const {
	// The kernel is still written for exactly 3 fraction words (same layout as ComplexHP).
	if (points.fractionWords != fpa::FRACTION_PART)
		throw std::runtime_error("The CUDA backend supports only " + std::to_string(fpa::FRACTION_PART) + " fraction words for now");
	calculateItersInternal<ComplexHP>(reinterpret_cast<const ComplexHP*>(points.words.data()), points.size(), iters, maxIter);
}