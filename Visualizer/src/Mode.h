#pragma once

#include <string>
#include <stdexcept>

enum class Mode {
    CPU_PARALLEL,
    OPENCL_LOCAL,
    CUDA_LOCAL,
    CUDA_REMOTE
};

inline Mode modeFromString(const std::string& s) {
    if (s == "CPU_PARALLEL")   return Mode::CPU_PARALLEL;
    if (s == "OPENCL_LOCAL") return Mode::OPENCL_LOCAL;
    if (s == "CUDA_LOCAL")   return Mode::CUDA_LOCAL;
    if (s == "CUDA_REMOTE")  return Mode::CUDA_REMOTE;
    throw std::invalid_argument("Unknown mode: '" + s + "'. Valid values: CPU_PARALLEL, OPENCL_LOCAL, CUDA_LOCAL, CUDA_REMOTE");
}

inline const char* modeToString(Mode mode) {
    switch (mode) {
        case Mode::CPU_PARALLEL:   return "CPU_PARALLEL";
        case Mode::OPENCL_LOCAL: return "OPENCL_LOCAL";
        case Mode::CUDA_LOCAL:   return "CUDA_LOCAL";
        case Mode::CUDA_REMOTE:  return "CUDA_REMOTE";
    }
    return "UNKNOWN";
}
