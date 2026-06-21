cmake_minimum_required(VERSION 3.20)

option(ENABLE_OPENCL "Enable OpenCL backend (OPENCL_LOCAL mode)" OFF)
option(ENABLE_CUDA   "Enable CUDA backend (CUDA_LOCAL / CUDA_REMOTE modes)" OFF)

set(SOURCES
    src/main.cpp
    src/calculators/SequentialIterationCalculator.cpp
    src/color/ColorManager.cpp
    src/math/FixedPointArithmetics.cpp
    src/util/fpng.cpp
)

if(ENABLE_OPENCL)
    list(APPEND SOURCES src/calculators/OpenCLIterationCalculator.cpp)
endif()

if(ENABLE_CUDA)
    enable_language(CUDA)
    list(APPEND SOURCES src/calculators/CUDAUnifiedKernel.cu)
endif()

add_executable(mandelbrot_visualizer ${SOURCES})

target_include_directories(mandelbrot_visualizer PRIVATE
    src
    src/calculators
    src/color
    src/math
    src/util
)

# C++17 required for if constexpr and std::optional
target_compile_features(mandelbrot_visualizer PRIVATE cxx_std_17)

# OpenMP (optional — gracefully skipped if not found)
find_package(OpenMP)
if(OpenMP_CXX_FOUND)
    target_link_libraries(mandelbrot_visualizer OpenMP::OpenMP_CXX)
endif()

if(ENABLE_OPENCL)
    find_package(OpenCL REQUIRED)
    target_link_libraries(mandelbrot_visualizer OpenCL::OpenCL)
    target_compile_definitions(mandelbrot_visualizer PRIVATE ENABLE_OPENCL)

    # Copy OpenCL kernel files next to the executable at build time
    add_custom_command(TARGET mandelbrot_visualizer POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy
            "${CMAKE_CURRENT_SOURCE_DIR}/src/kernels/OpenCLKernel.cl"
            "$<TARGET_FILE_DIR:mandelbrot_visualizer>/OpenCLKernel.cl"
        COMMAND ${CMAKE_COMMAND} -E copy
            "${CMAKE_CURRENT_SOURCE_DIR}/src/kernels/OpenCLKernelHP.cl"
            "$<TARGET_FILE_DIR:mandelbrot_visualizer>/OpenCLKernelHP.cl"
        COMMENT "Copying OpenCL kernels to output directory"
    )
endif()

if(ENABLE_CUDA)
    find_package(CUDAToolkit REQUIRED)
    target_link_libraries(mandelbrot_visualizer CUDA::cudart)
    target_compile_definitions(mandelbrot_visualizer PRIVATE ENABLE_CUDA)
    set_target_properties(mandelbrot_visualizer PROPERTIES CUDA_SEPARABLE_COMPILATION ON)
endif()
