cmake_minimum_required(VERSION 3.20)
project(MandelbrotVisualizer)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

option(ENABLE_OPENCL "Enable OpenCL backend" OFF)
option(ENABLE_CUDA   "Enable CUDA backend"   OFF)

add_subdirectory(Visualizer)
