# Build this from the project root:
#   docker build -f RunPod/Dockerfile -t mandelbrot-worker .

# ── Stage 1: build the worker binary ─────────────────────────────────────────
FROM nvidia/cuda:12.6.0-devel-ubuntu24.04 AS builder

RUN apt-get update && apt-get install -y --no-install-recommends \
        cmake build-essential \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /build

COPY Visualizer/src/calculators/IterationCalculator.h     src/calculators/
COPY Visualizer/src/calculators/CUDAIterationCalculator.h src/calculators/
COPY Visualizer/src/calculators/CUDAUnifiedKernel.cu      src/calculators/
COPY Visualizer/src/util/base64.h                         src/util/
COPY Visualizer/src/Sampler.h                             src/
COPY Visualizer/src/Sampler.cpp                           src/
COPY Visualizer/src/math/FixedPointArithmetics.h          src/math/
COPY Visualizer/src/math/FixedPointArithmetics.cpp        src/math/
COPY RunPod/CMakeLists.txt                                ./
COPY RunPod/src/                                          src/

RUN cmake -B build -DCMAKE_BUILD_TYPE=Release \
    && cmake --build build --parallel

# ── Stage 2: minimal runtime image ───────────────────────────────────────────
FROM nvidia/cuda:12.6.0-runtime-ubuntu24.04

RUN apt-get update && apt-get install -y --no-install-recommends \
        python3 python3-pip \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /app

COPY --from=builder /build/build/mandelbrot_cuda_worker ./
COPY RunPod/handler.py       ./
COPY RunPod/requirements.txt ./

RUN pip3 install --no-cache-dir --break-system-packages -r requirements.txt

CMD ["python3", "-u", "handler.py"]
