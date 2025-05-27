#include <cuda_runtime.h>
#include <device_launch_parameters.h>
#include <iostream>

#include "CUDAIterationCalculator.h"

__constant__ unsigned int deviceMaxIter;

__global__ void calculateIters(const Complex* points, int* iterations, const size_t n){
	const size_t idx = threadIdx.x + blockIdx.x * blockDim.x;
	if (idx >= n)
		return;

	const double x0 = points[idx].real;
	const double y0 = points[idx].imag;

	double x2 = 0;
	double y2 = 0;

	double x = 0;
	double y = 0;

	int result = -1;
	for (int i = 0; i < deviceMaxIter; i++) {
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



int CUDAIterationCalculator::calculate(const std::vector<Complex>& points, std::vector<int>& iters, const unsigned int maxIter) const {

	const size_t N = points.size();

	Complex* devicePointsBuffer;
	int* deviceItersBuffer;

	cudaMalloc(&devicePointsBuffer, N * sizeof(Complex));
	cudaMalloc(&deviceItersBuffer, N * sizeof(int));

	cudaMemcpy(devicePointsBuffer, points.data(), N * sizeof(Complex), cudaMemcpyHostToDevice);
	cudaMemcpyToSymbol(deviceMaxIter, &maxIter, sizeof(unsigned int));

	const int blockSize = 2048;
	int numBlocks = (N + blockSize - 1) / blockSize;

	calculateIters<<<numBlocks, blockSize >>>(devicePointsBuffer, deviceItersBuffer, N);

	cudaMemcpy(iters.data(), deviceItersBuffer, N * sizeof(int), cudaMemcpyDeviceToHost);

	cudaFree(devicePointsBuffer);
	cudaFree(deviceItersBuffer);

	return 0;
}

int CUDAIterationCalculator::calculate(const std::vector<ComplexHP>& points, std::vector<int>& iters, const unsigned int maxIter) const {

	// TODO write the logic here
	return 0;
}