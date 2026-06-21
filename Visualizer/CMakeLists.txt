cmake_minimum_required(VERSION 3.20)

option(ENABLE_OPENCL "Enable OpenCL backend (OPENCL_LOCAL mode)" OFF)
option(ENABLE_CUDA   "Enable CUDA backend (CUDA_LOCAL / CUDA_REMOTE modes)" OFF)

set(SOURCES
    src/main.cpp
    src/Mode.h
    src/Sampler.cpp
    src/Sampler.h
    src/calculators/IterationCalculator.h
    src/calculators/SequentialIterationCalculator.cpp
    src/calculators/SequentialIterationCalculator.h
    src/calculators/OpenCLIterationCalculator.h
    src/calculators/OpenCLIterationCalculator.cpp
    src/calculators/CUDAIterationCalculator.h
    src/calculators/CUDAUnifiedKernel.cu
    src/color/ColorManager.cpp
    src/color/ColorManager.h
    src/color/Palettes.h
    src/math/FixedPointArithmetics.cpp
    src/math/FixedPointArithmetics.h
    src/util/fpng.cpp
    src/util/fpng.h
    src/util/errors.h
    src/util/ScopedTimer.h
    src/util/ImageWriter.h
    src/util/ImageWriter.cpp
    src/kernels/OpenCLKernel.cl
    src/kernels/OpenCLKernelHP.cl
)

# Exclude GPU implementation files from compilation by default so the project
# builds without OpenCL/CUDA installed. CLion still indexes them as part of the
# target, which eliminates the "not in any project target" warning.
set_source_files_properties(
    src/calculators/OpenCLIterationCalculator.cpp
    src/calculators/CUDAUnifiedKernel.cu
    PROPERTIES HEADER_FILE_ONLY TRUE
)

if(ENABLE_OPENCL)
    set_source_files_properties(
        src/calculators/OpenCLIterationCalculator.cpp
        PROPERTIES HEADER_FILE_ONLY FALSE
    )
endif()

if(ENABLE_CUDA)
    enable_language(CUDA)
    set_source_files_properties(
        src/calculators/CUDAUnifiedKernel.cu
        PROPERTIES HEADER_FILE_ONLY FALSE
    )
endif()

add_executable(mandelbrot_visualizer ${SOURCES})

target_include_directories(mandelbrot_visualizer PRIVATE
    src
    src/calculators
    src/color
    src/math
    src/util
    src/kernels
)

# C++17 required for if constexpr and std::optional
target_compile_features(mandelbrot_visualizer PRIVATE cxx_std_17)

# fpng uses SSE4.1 intrinsics; MinGW doesn't enable this by default
set_source_files_properties(src/util/fpng.cpp PROPERTIES COMPILE_FLAGS "-msse4.1 -mpclmul")

# OpenMP (optional — gracefully skipped if not found)
find_package(OpenMP)
if(OpenMP_CXX_FOUND)
    target_compile_options(mandelbrot_visualizer PRIVATE -fopenmp)
    target_link_options(mandelbrot_visualizer PRIVATE -fopenmp)
endif()

# Detect Rust target triple so the binary is named correctly for Tauri's sidecar convention
execute_process(
    COMMAND rustc -vV
    OUTPUT_VARIABLE RUSTC_OUTPUT
    ERROR_QUIET
    OUTPUT_STRIP_TRAILING_WHITESPACE
)
string(REGEX MATCH "host: ([^\n\r]+)" _ "${RUSTC_OUTPUT}")
set(RUST_TARGET_TRIPLE "${CMAKE_MATCH_1}")

set(TAURI_BINARIES_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../GUI/MandelbrotSetVisualizer/src-tauri/binaries")

# Deploy the binary to the Tauri sidecar directory after every build
add_custom_command(TARGET mandelbrot_visualizer POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "${TAURI_BINARIES_DIR}"
    COMMAND ${CMAKE_COMMAND} -E copy
        "$<TARGET_FILE:mandelbrot_visualizer>"
        "${TAURI_BINARIES_DIR}/mandelbrot_visualizer-${RUST_TARGET_TRIPLE}.exe"
    COMMENT "Deploying binary to Tauri sidecar directory (${RUST_TARGET_TRIPLE})"
)

if(ENABLE_OPENCL)
    find_package(OpenCL REQUIRED)
    target_link_libraries(mandelbrot_visualizer OpenCL::OpenCL)
    target_compile_definitions(mandelbrot_visualizer PRIVATE ENABLE_OPENCL)

    add_custom_command(TARGET mandelbrot_visualizer POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy
            "${CMAKE_CURRENT_SOURCE_DIR}/src/kernels/OpenCLKernel.cl"
            "$<TARGET_FILE_DIR:mandelbrot_visualizer>/OpenCLKernel.cl"
        COMMAND ${CMAKE_COMMAND} -E copy
            "${CMAKE_CURRENT_SOURCE_DIR}/src/kernels/OpenCLKernelHP.cl"
            "$<TARGET_FILE_DIR:mandelbrot_visualizer>/OpenCLKernelHP.cl"
        COMMAND ${CMAKE_COMMAND} -E copy
            "${CMAKE_CURRENT_SOURCE_DIR}/src/kernels/OpenCLKernel.cl"
            "${TAURI_BINARIES_DIR}/OpenCLKernel.cl"
        COMMAND ${CMAKE_COMMAND} -E copy
            "${CMAKE_CURRENT_SOURCE_DIR}/src/kernels/OpenCLKernelHP.cl"
            "${TAURI_BINARIES_DIR}/OpenCLKernelHP.cl"
        COMMENT "Copying OpenCL kernels to output and Tauri directories"
    )
endif()

if(ENABLE_CUDA)
    find_package(CUDAToolkit REQUIRED)
    target_link_libraries(mandelbrot_visualizer CUDA::cudart)
    target_compile_definitions(mandelbrot_visualizer PRIVATE ENABLE_CUDA)
    set_target_properties(mandelbrot_visualizer PROPERTIES CUDA_SEPARABLE_COMPILATION ON)
endif()
