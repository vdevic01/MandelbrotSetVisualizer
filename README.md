# Mandelbrot Set Visualizer

An interactive Mandelbrot set explorer with a Tauri/TypeScript GUI and a high-performance C++ backend. Zoom arbitrarily deep using standard double precision, with automatic promotion to 128-bit fixed-point arithmetic when double precision runs out.

---

## Features

- **Interactive zoom** — draw a rectangle on the canvas to zoom into any region; the selection snaps to the canvas aspect ratio
- **Automatic high-precision mode** — seamlessly switches from 64-bit `double` to 128-bit fixed-point arithmetic when the viewport width drops below ~6×10⁻¹³, allowing zoom depths that double precision cannot represent
- **OpenMP parallelism** — CPU rendering uses all available cores
- **Anti-aliasing** — configurable samples-per-pixel with random sub-pixel jitter
- **8 colour palettes** — cyclic palettes with configurable cycle length
- **Multiple compute backends** — CPU (default), OpenCL, CUDA (optional, requires separate build flags)
- **Headless CLI** — the C++ binary can be used standalone without the GUI

---

## Project structure

```
MandelbrotSetVisualizer/
├── Visualizer/                  C++ backend
│   ├── CMakeLists.txt
│   └── src/
│       ├── main.cpp             Entry point, argument parsing
│       ├── Mode.h               Compute mode enum
│       ├── Sampler.cpp/.h       Complex-plane pixel sampling (LP + HP)
│       ├── calculators/
│       │   ├── IterationCalculator.h          Interface
│       │   ├── SequentialIterationCalculator  CPU/OpenMP implementation
│       │   ├── OpenCLIterationCalculator      OpenCL implementation
│       │   └── CUDAUnifiedKernel.cu           CUDA implementation
│       ├── color/
│       │   ├── ColorManager.cpp/.h            Cyclic palette colouring
│       │   └── Palettes.h                     Palette definitions
│       ├── math/
│       │   └── FixedPointArithmetics.cpp/.h   128-bit fixed-point library
│       ├── kernels/
│       │   ├── OpenCLKernel.cl                OpenCL double-precision kernel
│       │   └── OpenCLKernelHP.cl              OpenCL high-precision kernel
│       └── util/
│           ├── ScopedTimer.h    Scoped wall-clock timer
│           ├── ImageWriter      PNG output via fpng
│           └── fpng             Fast PNG encoder
└── GUI/
    └── MandelbrotSetVisualizer/ Tauri application
        ├── src/
        │   ├── main.ts          Frontend logic (p5.js canvas, BoundaryManager)
        │   └── styles.css
        ├── index.html
        └── src-tauri/
            ├── src/main.rs      Tauri commands, sidecar bridge
            └── tauri.conf.json
```

---

## Dependencies

### C++ backend

| Dependency | Version | Notes |
|---|---|---|
| CMake | 3.20+ | Build system |
| GCC / MinGW-w64 | Any modern | C++17 required |
| OpenMP | — | Bundled with GCC; enables multi-core CPU rendering |
| CUDA Toolkit | 11+ | Optional; enable local GPU rendering with `-DENABLE_CUDA=ON` |
| libcurl | 7.x+ | Always required (used by `CUDA_REMOTE` mode). Install via your package manager: `apt install libcurl4-openssl-dev`, `brew install curl`, or vcpkg/MSYS2 on Windows. The [cpr](https://github.com/libcpr/cpr) wrapper is fetched automatically by CMake via FetchContent — no manual installation needed. |
| OpenCL | — | Optional; enable with `-DENABLE_OPENCL=ON` |

### GUI

| Dependency | Notes |
|---|---|
| Rust + Cargo | Install via [rustup.rs](https://rustup.rs) |
| Node.js + npm | 18+ recommended |
| Tauri CLI | Installed automatically via `npm install` |

---

## Building and running

### 1. Build the C++ backend

```bash
cmake -S Visualizer -B Visualizer/cmake-build-release -DCMAKE_BUILD_TYPE=Release
cmake --build Visualizer/cmake-build-release
```

The `POST_BUILD` step automatically copies the binary to the two locations the Tauri app needs:

- `GUI/MandelbrotSetVisualizer/src-tauri/binaries/` — production bundle
- `GUI/MandelbrotSetVisualizer/src-tauri/target/debug/` — `tauri dev`

To enable GPU backends pass additional flags at configure time:

```bash
# OpenCL
cmake -S Visualizer -B Visualizer/cmake-build-release -DCMAKE_BUILD_TYPE=Release -DENABLE_OPENCL=ON

# CUDA (local GPU)
cmake -S Visualizer -B Visualizer/cmake-build-release -DCMAKE_BUILD_TYPE=Release -DENABLE_CUDA=ON
```

### 2. Run the GUI (development)

```bash
cd GUI/MandelbrotSetVisualizer
npm install        # first time only
npm run tauri dev
```

The app starts with the full Mandelbrot set visible. Drag to draw a selection rectangle and release to zoom in. Use the sidebar to adjust parameters, then click **Apply** or change the dropdown — each change triggers a new render.

### 3. Reset

The **Reset** button in the sidebar returns to the initial full view.

---

## GUI controls

| Control | Default | Description |
|---|---|---|
| Mode | CPU Parallel | Compute backend. Options are populated at startup by querying the binary. |
| Max Iterations | 700 | Upper bound on escape iterations per point. Higher values reveal more detail at deep zoom but are slower. |
| Palette Length | 250 | Number of steps before the colour palette repeats. Shorter lengths produce tighter colour bands. |
| Samples | 1 | Anti-aliasing samples per pixel. Each sample is a random sub-pixel jitter. Values above 4 noticeably reduce noise; values above 16 have diminishing returns. |
| Palette | Navy | Colour palette (see list below). |

---

## Colour palettes

| ID | Name |
|---|---|
| 0 | Navy |
| 1 | Sunset |
| 2 | Ocean |
| 3 | Fire and Ash |
| 4 | Twilight |
| 5 | Garden |
| 6 | Pomegranate |
| 7 | Greyscale |

---

## High-precision mode

Standard `double` provides ~15–16 significant decimal digits. When zooming deep into the set, adjacent pixels start mapping to the same floating-point coordinate and the image degrades. The app detects this automatically: once the viewport width (reEnd − reStart) drops below **6×10⁻¹³**, it promotes all subsequent renders to high-precision mode.

High-precision coordinates are represented as **128-bit signed fixed-point numbers** using 4 × 32-bit words in two's complement, big-endian order:

```
[ integer word ][ fraction word 1 ][ fraction word 2 ][ fraction word 3 ]
   32 bits           32 bits            32 bits            32 bits
```

This gives 1 integer word and 96 bits of fractional precision — approximately **29 significant decimal digits**. The fixed-point library (`FixedPointArithmetics.cpp`) implements addition, subtraction, multiplication, division, and complementing entirely in integer arithmetic, making it portable to GPU kernels without floating-point dependency.

Coordinate conversion between the Tauri frontend (which uses the `decimal.js` arbitrary-precision library) and the C++ backend (which uses the 4-word fixed-point format) is handled by `BoundaryManager.decimalToFixedPoint()` and `fixedToDecimal()` in `main.ts`, and by `fpa::floatingToFixedPoint()` / `fpa::fixedToFloatingPoint()` in C++.

---

## CLI usage

The C++ binary can be invoked directly without the GUI:

```
mandelbrot_visualizer <MODE> <OUTPUT_FILE> <MAX_ITER> <PALETTE_LENGTH> <PALETTE_ID>
                      <SAMPLES> <USE_HP> <COORDS...> [WIDTH HEIGHT]
```

| Argument | Type | Description |
|---|---|---|
| `MODE` | string | `CPU_PARALLEL`, `OPENCL_LOCAL`, `CUDA_LOCAL`, or `CUDA_REMOTE` |
| `OUTPUT_FILE` | path | Output PNG path |
| `MAX_ITER` | int | Maximum escape iterations |
| `PALETTE_LENGTH` | int | Colour cycle length |
| `PALETTE_ID` | int | Palette index (0–7) |
| `SAMPLES` | int | Anti-aliasing samples per pixel (≥ 1) |
| `USE_HP` | 0 or 1 | 0 = double precision, 1 = 128-bit fixed-point |
| `WIDTH HEIGHT` | int int | Optional. Output image dimensions (default: 900×600) |

**Standard precision** (`USE_HP=0`) — four doubles follow:

```
RE_START  RE_END  IM_START  IM_END
```

**High precision** (`USE_HP=1`) — sixteen unsigned 32-bit integers follow (4 words per coordinate, big-endian fixed-point):

```
RE_START[0..3]  RE_END[0..3]  IM_START[0..3]  IM_END[0..3]
```

### Examples

Standard precision, full set:

```bash
mandelbrot_visualizer CPU_PARALLEL out.png 1000 256 0 4 0 \
  -2.0 1.0 -1.0 1.0
```

Standard precision, deep zoom, large output:

```bash
mandelbrot_visualizer CPU_PARALLEL out.png 2000 512 0 4 0 \
  -0.153004885037500013708 -0.152809695287500013708 \
  1.039611370300000000002  1.039757762612500000002 \
  1800 1200
```

High precision:

```bash
mandelbrot_visualizer CPU_PARALLEL out.png 2000 512 0 4 1 \
  4294967295 3637816318 2730300863 4039731417 \
  4294967295 3638654652 981237348  3835888558 \
  1 170129539 4244482999 3226762018 \
  1 170758290 785201715  2000138050 \
  1800 1200
```

List available compute modes at runtime:

```bash
mandelbrot_visualizer --list-modes
```

---

## Compute modes

| Mode | Description |
|---|---|
| `CPU_PARALLEL` | Multi-threaded CPU via OpenMP. Always available. |
| `OPENCL_LOCAL` | GPU via OpenCL. Requires `-DENABLE_OPENCL=ON` at build time and at least one OpenCL platform present at runtime. |
| `CUDA_LOCAL` | GPU via CUDA. Requires `-DENABLE_CUDA=ON` at build time and an NVIDIA GPU. |
| `CUDA_REMOTE` | GPU via CUDA running on a RunPod serverless worker. Always available — no local GPU or CUDA Toolkit needed. Requires valid RunPod credentials configured in the GUI settings. |

The GUI populates the Mode dropdown by running `mandelbrot_visualizer --list-modes` at startup, so only modes available in the current build and on the current hardware are shown.

---

## RunPod remote CUDA deployment

`CUDA_REMOTE` offloads the CUDA iteration kernel to a RunPod serverless worker, allowing GPU-accelerated rendering from machines without a local NVIDIA GPU.

### Architecture

```
Local machine                           RunPod worker
─────────────────────────────────────   ──────────────────────────────────
Tauri GUI → C++ sidecar
  Sampling (CPU)
  HTTP POST: base64-encoded points  ──► handler.py → mandelbrot_cuda_worker
    (~8 MB LP / ~17 MB HP per render)     (CUDA kernel only)
                                    ◄──  JSON response (base64 iterations +
                                         CUDA event timing)
  Coloring + PNG save (CPU)
```

`CUDARemoteIterationCalculator` implements the same `IterationCalculator` interface as every other backend — sampling runs locally, and only the pre-sampled points array is sent to the worker. The worker does no sampling; it just runs the CUDA kernel and returns the flat iteration count array.

### Building and pushing the Docker image

From the **project root**:

```bash
docker build -f RunPod/Dockerfile -t your-dockerhub/mandelbrot-worker:latest .
docker push your-dockerhub/mandelbrot-worker:latest
```

### Deploying to RunPod

1. Go to [RunPod Serverless](https://www.runpod.io/serverless) and create a new endpoint.
2. Point it at your Docker image.
3. Select a GPU type (RTX 3090 or better recommended for HP mode).
4. Copy the **Endpoint ID** — your full endpoint URL is `https://api.runpod.ai/v2/<id>/runsync`.

### Configuring the GUI

1. Build the C++ backend normally — `CUDA_REMOTE` is always compiled in. No CUDA Toolkit or local GPU required.
2. Start the GUI. Select **CUDA Remote** in the Mode dropdown.
3. A **RunPod Settings** button appears — click it to open the settings dialog.
4. Enter the endpoint URL and your RunPod API key, then click **Save**.
5. Credentials are stored in `runpod-settings.json` next to the application and injected as environment variables at render time. They are never passed as command-line arguments or committed to the repository.

### Response format

The worker outputs a single JSON line to stdout:

```json
{
  "execution_time_ms": 42.5,
  "handler_time_ms":  150.0,
  "width": 900,
  "height": 600,
  "samples": 1,
  "iterations": "<base64-encoded int32 array>"
}
```

`execution_time_ms` is measured with CUDA events (GPU kernel only, excluding memory transfers). `handler_time_ms` is the total subprocess wall time measured by the Python handler.
