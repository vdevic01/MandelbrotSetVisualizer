cmake_minimum_required(VERSION 3.20)
project(mandelbrot_cuda_worker CUDA CXX)

set(VIZ_SRC "${CMAKE_CURRENT_SOURCE_DIR}/src")

set(SOURCES
    src/main_worker.cpp
    ${VIZ_SRC}/calculators/CUDAUnifiedKernel.cu
)

add_executable(mandelbrot_cuda_worker ${SOURCES})

target_include_directories(mandelbrot_cuda_worker PRIVATE
    ${VIZ_SRC}/calculators
    ${VIZ_SRC}/util
)

target_compile_features(mandelbrot_cuda_worker PRIVATE cxx_std_17)
target_compile_definitions(mandelbrot_cuda_worker PRIVATE ENABLE_CUDA)

find_package(CUDAToolkit REQUIRED)
target_link_libraries(mandelbrot_cuda_worker CUDA::cudart)
set_target_properties(mandelbrot_cuda_worker PROPERTIES CUDA_SEPARABLE_COMPILATION ON)
