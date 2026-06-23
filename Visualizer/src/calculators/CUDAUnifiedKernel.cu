#include <cuda_runtime.h>
#include <device_launch_parameters.h>
#include <cstdint>
#include <iostream>

#include "CUDAIterationCalculator.h"

#pragma region Fixed Point Arithmetics

constexpr uint32_t WHOLE_PART = 1;
constexpr uint32_t FRACTION_PART = 3;
constexpr uint32_t WHOLE_BITS = WHOLE_PART * 32;
constexpr uint32_t FRACTION_BITS = FRACTION_PART * 32;
constexpr uint32_t FP_SIZE = WHOLE_PART + FRACTION_PART;
constexpr uint32_t FP_BUFFER_SIZE = FP_SIZE * 2;

__device__ void addFixed(const uint32_t* a, const uint32_t* b, uint32_t c[FP_SIZE]) {
	uint32_t carry = 0;
	for (int i = FP_SIZE - 1; i >= 0; i--) {
		uint64_t temp = (uint64_t)a[i] + b[i] + carry;
		carry = temp >> 32;
		c[i] = temp;
	}
};

__device__ void incFixed(const uint32_t* a, uint32_t c[FP_SIZE]) {
	uint32_t carry = 1;
	for (int i = FP_SIZE - 1; i >= 0; i--) {
		uint64_t temp = (uint64_t)a[i] + carry;
		carry = temp >> 32;
		c[i] = (uint32_t)temp;
	}
}

__device__ void cmplFixed(const uint32_t* a, uint32_t c[FP_SIZE]) {
	for (int i = 0; i < FP_SIZE; i++) {
		c[i] = ~a[i];
	}
	incFixed(c, c);
}

__device__ void subFixed(const uint32_t* a, const uint32_t* b, uint32_t c[FP_SIZE]) {
	cmplFixed(b, c);
	addFixed(a, c, c);
}

__device__ bool gtFixed(const uint32_t* a, const uint32_t* b) {
	uint32_t signA = a[0] >> 31;
	uint32_t signB = b[0] >> 31;
	if (signA != signB) {
		return signA == 0;
	}
	uint32_t diff[FP_SIZE];
	subFixed(b, a, diff);
	uint32_t sign = diff[0] >> 31;
	return sign == 1;
}

__device__ void mulCmplFixed(const uint32_t* a, const uint32_t* b, uint32_t c[FP_SIZE]) {
	uint64_t result[FP_BUFFER_SIZE];
	for (int i = 0; i < FP_BUFFER_SIZE; i++) {
		result[i] = 0;
	}

	const uint32_t* aAbs = a;
	const uint32_t* bAbs = b;
	char aSign = a[0] >> 31;
	char bSign = b[0] >> 31;
	bool negate = false;
	uint32_t tempA[FP_SIZE];
	uint32_t tempB[FP_SIZE];
	if (aSign != bSign) {
		if (aSign == 1) {
			cmplFixed(a, tempA);
			aAbs = tempA;
		}
		else {
			cmplFixed(b, tempB);
			bAbs = tempB;
		}
		negate = true;
	}
	else if (aSign == 1 && bSign == 1) {
		cmplFixed(a, tempA);
		cmplFixed(b, tempB);
		aAbs = tempA;
		bAbs = tempB;
	}

	for (int i = FP_SIZE - 1; i >= 0; i--) {
		if (aAbs[i] == 0)
			continue;
		for (int j = FP_SIZE - 1; j >= 0; j--) {
			if (bAbs[j] == 0)
				continue;
			uint64_t temp = (uint64_t)aAbs[i] * bAbs[j];
			uint64_t tempLSB = temp & 0x00000000FFFFFFFF;
			uint64_t tempMSB = temp >> 32;
			result[i + j + 1] += tempLSB;
			result[i + j] += tempMSB;
		}
	}
	for (int i = FP_BUFFER_SIZE - 1; i >= 1; i--) {
		result[i - 1] += result[i] >> 32;
	}
	const int leftBound = WHOLE_PART * 2 - 1;
	const int rightBound = leftBound + FP_SIZE;
	for (int i = leftBound; i < rightBound; i++) {
		c[i - leftBound] = result[i];
	}
	if (negate)
		cmplFixed(c, c);
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
void calculateItersInternal(const std::vector<T_ComplexType>& points, std::vector<int>& iters, const unsigned int maxIter) {
	const size_t N = points.size();

	T_ComplexType* devicePointsBuffer;
	int* deviceItersBuffer;

	cudaMalloc(&devicePointsBuffer, N * sizeof(T_ComplexType));
	cudaMalloc(&deviceItersBuffer, N * sizeof(int));

	cudaMemcpy(devicePointsBuffer, points.data(), N * sizeof(T_ComplexType), cudaMemcpyHostToDevice);
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
	calculateItersInternal<Complex>(points, iters, maxIter);
}

void CUDAIterationCalculator::calculate(const std::vector<ComplexHP>& points, std::vector<int>& iters, unsigned int maxIter) const {
	calculateItersInternal<ComplexHP>(points, iters, maxIter);
}