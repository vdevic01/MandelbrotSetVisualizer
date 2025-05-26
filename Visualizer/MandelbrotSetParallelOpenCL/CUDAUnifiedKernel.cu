#include "cuda_runtime.h"

#include "CUDAIterationCalculator.h"

__global__ void calculateIters(const Complex* IN, int* OUT, const unsigned int max_iter){

}

int CUDAIterationCalculator::calculate(
	const std::vector<Complex>& points, std::vector<int>& iters,
	const unsigned int size, const unsigned int max_iter) const {

	// TODO write the logic here
	return 0;
}

int CUDAIterationCalculator::calculate(
	const std::vector<ComplexHP>& points, std::vector<int>& iters,
	const unsigned int size, const unsigned int max_iter) const {

	// TODO write the logic here
	return 0;
}