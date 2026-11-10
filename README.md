# Mandelbrot Set Visualizer

An interactive Mandelbrot set explorer with a Tauri/TypeScript GUI and a high-performance C++ backend. Zoom arbitrarily deep using standard double precision, with automatic promotion to fixed-point arithmetic of up to 192 bits when double precision runs out.

---

## Features

- **Interactive zoom** — draw a rectangle on the canvas to zoom into any region; the selection snaps to the canvas aspect ratio
- **Automatic high-precision mode** — seamlessly switches from 64-bit `double` to fixed-point arithmetic when the viewport width drops below ~6×10⁻¹³ and grows the fixed-point size (2 to 6 words) as you zoom deeper, down to a viewport width of ~4×10⁻⁵⁵
- **OpenMP parallelism** — CPU rendering uses all available cores
- **Anti-aliasing** — configurable samples-per-pixel with random sub-pixel jitter
- **8 colour palettes** — cyclic palettes with configurable cycle length
- **Smooth colouring** — colours follow a continuous escape-time value instead of the whole iteration count, so there are no colour bands
- **Multiple compute backends** — CPU (default), OpenCL, CUDA local, and CUDA remote (optional, see below)
- **Remote GPU rendering** — offload CUDA execution to a remote GPU worker over HTTP; no local NVIDIA GPU required
- **Headless CLI** — the C++ binary can be used standalone without the GUI

---

## Project structure

```
MandelbrotSetVisualizer/
├── Visualizer/                  C++ backend
│   ├── CMakeLists.txt
│   ├── tests/                   Unit tests for the fixed-point library (GoogleTest)
│   │   ├── generate_vectors.py  Generates golden test vectors (independent Python reference)
│   │   └── vectors/             Generated vectors, one file per fraction size
│   └── src/
│       ├── main.cpp             Entry point, argument parsing
│       ├── Mode.h               Compute mode enum
│       ├── Sampler.cpp/.h       Complex-plane pixel sampling (LP + HP)
│       ├── calculators/
│       │   ├── IterationCalculator.h               Interface (CPU/OpenCL/CUDA)
│       │   ├── CpuParallelIterationCalculator       CPU/OpenMP implementation
│       │   ├── OpenCLIterationCalculator            OpenCL implementation
│       │   ├── CUDAUnifiedKernel.cu                CUDA implementation
│       │   └── CUDARemoteIterationCalculator        HTTP client for remote CUDA worker
│       ├── color/
│       │   ├── ColorManager.cpp/.h                 Cyclic palette colouring
│       │   ├── SmoothColoring.h                    Smooth value from escape iteration and escape value
│       │   └── Palettes.h                          Palette definitions
│       ├── math/
│       │   ├── FixedPointArithmetics.cpp/.h        Fixed-point library API (runtime fraction size)
│       │   ├── FixedPointShared.h                  Arithmetic shared by CPU and CUDA (add, sub, mul, ...)
│       │   └── FixedPointCore.h                    CPU-only arithmetic (division, conversions, ...)
│       ├── kernels/
│       │   ├── OpenCLKernel.cl                     OpenCL double-precision kernel
│       │   └── OpenCLKernelHP.cl                   OpenCL high-precision kernel
│       └── util/
│           ├── ScopedTimer.h    Scoped wall-clock timer
│           ├── base64.h         Header-only base64 encode/decode
│           ├── ImageWriter      PNG output via fpng
│           └── fpng             Fast PNG encoder
├── Worker/                      Remote CUDA worker
│   ├── Dockerfile               Multi-stage CUDA build
│   ├── CMakeLists.txt           Worker binary build
│   ├── handler.py               HTTP server (serves / and /ping)
│   ├── requirements.txt
│   └── src/
│       └── main_worker.cpp      Reads boundary JSON, samples, runs CUDA kernel
├── benchmarks/                  Local performance benchmark (see benchmarks/README.md)
├── .github/workflows/           CI: unit tests on every pull request
└── GUI/
    └── MandelbrotSetVisualizer/ Tauri application
        ├── src/
        │   ├── main.ts          Frontend logic (p5.js canvas, BoundaryManager)
        │   ├── precision.ts     Number format for a zoom level, Decimal to fixed-point words
        │   └── styles.css
        ├── index.html
        └── src-tauri/
            ├── src/main.rs      Tauri commands, sidecar bridge, remote settings
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
| GoogleTest | — | Only for the unit tests (`-DBUILD_FPA_TESTS=ON`); fetched automatically by CMake |
| Python 3 | 3.9+ | Only to regenerate the test vectors and to run the benchmark script |

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

## Testing the fixed-point library

The high-precision arithmetic (`Visualizer/src/math/`) has its own GoogleTest suite, which runs for every supported fraction size. It is not part of the normal build; enable it with `-DBUILD_FPA_TESTS=ON` (needs network access once, to fetch GoogleTest). In CLion, add a CMake profile with that option.

```bash
cmake -S Visualizer -B Visualizer/cmake-build-tests -DCMAKE_BUILD_TYPE=Debug -DBUILD_FPA_TESTS=ON
cmake --build Visualizer/cmake-build-tests --target fpa_tests
ctest --test-dir Visualizer/cmake-build-tests --output-on-failure
```

Most expected values come from `Visualizer/tests/vectors/`, generated by an independent Python reference (`python Visualizer/tests/generate_vectors.py`; `--check` verifies the committed vectors are current, `--selfcheck` cross-validates the reference). The rest are hand-derived edge cases, algebraic invariants, cross-format consistency checks and regression tests for fixed bugs. On Linux/macOS, `-DFPA_SANITIZE=ON` additionally builds the tested library with AddressSanitizer and UBSan.

The tests run automatically on every pull request to `main` (GitHub Actions workflow `Tests`: a Release build and an ASan + UBSan build). To make a failing run block merging, require the two checks in the repository's branch protection rules for `main`.

---

## Benchmarking

`benchmarks/bench.py` measures how fast a commit renders the fixed single- and high-precision scenes and compares two commits, with an HTML chart. It runs locally only, because timings are not comparable between machines. See [benchmarks/README.md](benchmarks/README.md).

---

## GUI controls

| Control | Default | Description |
|---|---|---|
| Mode | CPU Parallel | Compute backend. Options are populated at startup by querying the binary. |
| Max Iterations | 700 | Upper bound on escape iterations per point. Higher values reveal more detail at deep zoom but are slower. |
| Palette Length | 250 | Number of steps before the colour palette repeats. Shorter lengths produce tighter colour bands. |
| Escape Radius | 2 | Iteration of a point stops once `|z|` is above this value. Range 2 to 128. Larger values make the colouring smoother. |
| Samples | 1 | Anti-aliasing samples per pixel. Each sample is a random sub-pixel jitter. Values above 4 noticeably reduce noise; values above 16 have diminishing returns. |
| Palette | Navy | Colour palette (see list below). |

### Status bar

The bar above the canvas shows, for the last render:

| Field | Meaning |
|---|---|
| Precision | The number format used: double, or fixed point with its number of fraction words and digits |
| Scale | The width of the visible region of the complex plane as a power of ten (the whole set is about 10^0.5) |
| Last render | How long the render took in total; hover for the time of each stage |
| Remote | For CUDA Remote: the worker's HTTP status, the round trip, and the kernel and sampling times it reported |

A line below shows warnings (such as reaching the maximum precision) and errors, including errors from the remote worker; hover it for the full text. The window is taller than the canvas by the height of the bar, so the bar never covers part of the image.

### Exporting an image

**Export image** (bottom of the sidebar) renders the view you are looking at at a larger size. You choose the width and the height (100 to 8000 px each). The centre of the image is the centre of the view on screen and the visible width stays the same, so a taller or wider shape shows more above and below (or less). It uses the current mode, iterations, samples and palette, picks the number format for the larger image (a bigger image has smaller pixels, so it may need more fraction words than the on-screen render), and saves `generated-files/exports/export_<timestamp>.png` (separate from the image shown in the app). The image on screen and the status bar are not affected.

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

Standard `double` provides ~15–16 significant decimal digits. When zooming deep into the set, adjacent pixels start mapping to the same floating-point coordinate and the image degrades. The app detects this automatically: once the viewport width (reEnd − reStart) drops below **6×10⁻¹³**, it switches to fixed-point arithmetic, and it keeps increasing the fixed-point size as you zoom deeper.

### Fixed-point format

In this project a **word** is always **32 bits**, one `unsigned int` (`fpa::uint`), whatever the processor's register size. That size is deliberate: the product of two 32-bit words fits exactly in a 64-bit integer, and GPUs are natively 32-bit, so the same arithmetic runs on the CPU and in the CUDA and OpenCL kernels. (Other libraries call this unit a "limb".)

Coordinates are **signed fixed-point numbers** made of such words in two's complement, big-endian: one integer word followed by `N` fraction words (`N` is the fraction size, 1 to 6). For example `N = 3` means a 96-bit fraction stored in 3 unsigned integers:

```
[ integer word ][ fraction word 1 ] ... [ fraction word N ]
```

| Fraction words N | Bits | Resolution (smallest step) | Decimal digits |
|---|---|---|---|
| 1 | 64 | 2.3×10⁻¹⁰ | ~9 (coarser than a double: never used) |
| 2 | 96 | 5.4×10⁻²⁰ | ~19 |
| 3 | 128 | 1.3×10⁻²⁹ | ~28 |
| 4 | 160 | 2.9×10⁻³⁹ | ~38 |
| 5 | 192 | 6.8×10⁻⁴⁹ | ~48 |
| 6 | 224 | 1.6×10⁻⁵⁸ | ~57 |

Larger sizes cost more time per operation, so the app always uses the smallest size that is precise enough.

### Choosing the size automatically

One pixel of the 900-pixel-wide canvas covers `viewport width / 900`. A format is used as long as one pixel is at least **3 resolution steps** wide (`SAFETY_STEPS_PER_PIXEL` in `precision.ts`). That is the same quality as the original double → fixed-point switch, and it works out to these switch points (viewport width):

| Format | Used until the viewport width drops below |
|---|---|
| `double` | 6×10⁻¹³ |
| 2 fraction words | 1.5×10⁻¹⁶ |
| 3 fraction words | 3.4×10⁻²⁶ |
| 4 fraction words | 7.9×10⁻³⁶ |
| 5 fraction words | 1.8×10⁻⁴⁵ |
| 6 fraction words | 4.3×10⁻⁵⁵ (the limit) |

Below the limit rendering continues with 6 words and the status line above the canvas warns that the image is no longer accurate. The status line also shows which format each render used.

The margin of 3 steps per pixel comes from measurements, not a guess: `benchmarks/precision_calibration.py` renders views with each size at a known number of steps per pixel and counts the pixels that differ from a 6-word reference image. At 3 steps about 3% of the pixels differ (visually indistinguishable), at 100 steps about 0.3%, at 1000 steps about 0.04%. A larger margin would give an even cleaner image but would switch to the slower, larger formats earlier; change `SAFETY_STEPS_PER_PIXEL` to trade one for the other.

### Implementation notes

The fixed-point library (`Visualizer/src/math/`) implements addition, subtraction, multiplication, division, comparison and complementing entirely in integer arithmetic, with no heap allocation. Every function in `FixedPointArithmetics.h` takes an optional `fractionWords` argument (default 3). Results are truncated toward zero, and division throws `std::overflow_error` when the quotient does not fit.

The arithmetic the GPU needs (add, subtract, compare, multiply) lives in `FixedPointShared.h`, which the CPU build and the CUDA kernel both use, so it is tested once by the unit tests. The OpenCL high-precision kernel has its own copy, which the host compiles for the size of each render. All backends (CPU, CUDA local and remote, OpenCL) support sizes 1 to 6.

The frontend uses `decimal.js` for the viewport coordinates, configured for 100 significant digits (its default of 20 would cap the zoom depth far below what the formats can represent). `precision.ts` converts them to fixed-point words with `decimalToFixedWords()`, and the C++ side converts doubles with `fpa::floatingToFixedPoint()` / `fpa::fixedToFloatingPoint()`.

---

## CLI usage

The C++ binary can be invoked directly without the GUI:

```
mandelbrot_visualizer <MODE> <OUTPUT_FILE> <MAX_ITER> <PALETTE_LENGTH> <PALETTE_ID>
                      <SAMPLES> <USE_HP> <COORDS...> [WIDTH HEIGHT] [--fraction-words N]
                      [--escape-radius R] [--color-spread S]
                      [--histogram [--histogram-phase P] [--histogram-cycles C]]
```

| Argument | Type | Description |
|---|---|---|
| `MODE` | string | `CPU_PARALLEL`, `OPENCL_LOCAL`, `CUDA_LOCAL`, or `CUDA_REMOTE` |
| `OUTPUT_FILE` | path | Output PNG path |
| `MAX_ITER` | int | Maximum escape iterations |
| `PALETTE_LENGTH` | int | Colour cycle length |
| `PALETTE_ID` | int | Palette index (0–7) |
| `SAMPLES` | int | Anti-aliasing samples per pixel (≥ 1) |
| `USE_HP` | 0 or 1 | 0 = double precision, 1 = fixed point (size set by `--fraction-words`) |
| `WIDTH HEIGHT` | int int | Optional. Output image dimensions (default: 900×600) |
| `--fraction-words N` | 1–6 | Optional flag, may appear anywhere. Number of 32-bit fraction words of the fixed-point format used when `USE_HP=1` (default 3). More words allow deeper zooms but are slower. Ignored for double precision. |
| `--escape-radius R` | 2–128 | Optional flag, may appear anywhere. Iteration of a point stops once `|z| > R` (default 2). Larger values make the colouring smoother, at the cost of a few extra iterations per point. |
| `--color-spread S` | 0–10 | Optional flag, may appear anywhere. Spreads the colours that are packed near the set: smooth iteration values `v` above a tenth of the palette length are compressed with `knee + ln(1 + S·(v − knee)) / S`, lower values are left alone (default 0 = off; the GUI uses 0.05). |
| `--histogram` | – | Optional flag. Colours by histogram equalisation instead of the cyclic palette: a sample gets the share of samples with a smaller smooth value, so crowded value ranges (near the set) are stretched over more of the palette. `PALETTE_LENGTH`, `--escape-radius` and `--color-spread` have no visible effect in this mode. |
| `--histogram-phase P` | 0–1 | Optional, with `--histogram` (default 0). Where in the palette the colouring starts; the palette wraps around, so this changes which colour goes where without shortening it. |
| `--histogram-cycles C` | 0–1000 | Optional, with `--histogram` (default 1). How many times the palette repeats from the lowest to the highest value. |

**Standard precision** (`USE_HP=0`) — four doubles follow:

```
RE_START  RE_END  IM_START  IM_END
```

**High precision** (`USE_HP=1`) — unsigned 32-bit integers follow: four coordinates of `1 + N` words each, where `N` is the fraction size (`--fraction-words`, default 3, so 16 numbers). Words are big-endian two's-complement fixed-point, most significant (whole) word first:

```
RE_START[0..N]  RE_END[0..N]  IM_START[0..N]  IM_END[0..N]
```

The number of coordinate words must match `N`; a mismatch is reported as an error.

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
| `CUDA_REMOTE` | GPU via CUDA on a remote worker. Always compiled in — no local GPU or CUDA Toolkit needed. Requires a deployed worker and credentials configured in the GUI settings. |

The GUI populates the Mode dropdown by running `mandelbrot_visualizer --list-modes` at startup, so only modes available in the current build and on the current hardware are shown.

---

## Remote CUDA deployment

`CUDA_REMOTE` offloads pixel sampling and the CUDA iteration kernel to a remote GPU worker, allowing GPU-accelerated rendering from machines without a local NVIDIA GPU.

### Architecture

```
Local machine                              Remote worker (load balancer)
──────────────────────────────────────     ─────────────────────────────────────
Tauri GUI → C++ sidecar                    handler.py (HTTP server, port 80)
  HTTP POST: boundary coords + dims   ──►    mandelbrot_cuda_worker
    (~200 bytes, LP or HP)                      samplePointsFromComplexPlane (CPU)
                                                CUDA kernel
                                     ◄──    JSON: sampling_time_ms,
                                                  execution_time_ms,
                                                  base64 iterations
  Coloring + PNG save (CPU)
```

`CUDARemoteIterationCalculator` sends only the viewport boundary and image dimensions to the worker. Sampling and kernel execution both happen remotely, keeping the request payload at ~200 bytes regardless of image size or sample count.

### Automatic builds from `main`

Our deployed worker rebuilds itself automatically. The serverless endpoint (RunPod) is linked to this GitHub repository: it tracks the **`main`** branch, builds `Worker/Dockerfile` with the repository root as the build context, and picks up the new image when `main` changes. So **merging a change to `main` is what updates the worker**; there is no manual `docker push` step. A change on any other branch does not reach the worker until it is merged.

The Dockerfile copies source files one by one. If worker code starts using a new file from `Visualizer/src`, add a `COPY` line for it in `Worker/Dockerfile`, otherwise the automatic build fails.

### Building and pushing the Docker image manually

Only needed for other platforms, or to test the image locally. Run it from the **project root** (the build context includes files from both `Visualizer/` and `Worker/`):

```bash
docker build -f Worker/Dockerfile -t your-dockerhub/mandelbrot-worker:latest .
docker push your-dockerhub/mandelbrot-worker:latest
```

### Deploying the worker

1. Push the image to any container registry and deploy it on a GPU instance (e.g. RunPod, Vast.ai, AWS EC2 GPU, or any machine with an NVIDIA GPU and Docker).
2. Ensure the container has GPU access and set `PORT=80` in the environment.
3. The worker needs an HTTP load balancer or public port forwarding so the client can reach it.
4. Note the public endpoint URL.

### Configuring the GUI

1. Build the C++ backend normally — `CUDA_REMOTE` is always compiled in. No CUDA Toolkit or local GPU required.
2. Open the **Settings** view (button in the sidebar footer, always accessible).
3. Enter the endpoint URL and your API key, then click **Save**.
4. Select **CUDA Remote** in the Mode dropdown.
5. Credentials are stored in `remote-settings.json` next to the application and injected as `REMOTE_ENDPOINT` / `REMOTE_API_KEY` environment variables at render time. They are never passed as command-line arguments or committed to the repository.

### Request format

The C++ client sends a compact JSON body directly to the worker:

**Standard precision:**
```json
{"use_hp":0,"max_iter":1000,"samples":4,"width":900,"height":600,
 "re_start":-2.0,"re_end":1.0,"im_start":-1.0,"im_end":1.0}
```

**High precision** (`1 + fraction_words` 32-bit fixed-point words per coordinate; `fraction_words` is optional and defaults to 3):
```json
{"use_hp":1,"max_iter":1000,"samples":4,"width":900,"height":600,"fraction_words":3,
 "re_start_0":W,"re_start_1":X,"re_start_2":Y,"re_start_3":Z, ...}
```

### Response format

```json
{
  "sampling_time_ms":  12.3,
  "execution_time_ms": 42.5,
  "handler_time_ms":   58.1,
  "iterations": "<base64-encoded int32 array>"
}
```

`sampling_time_ms` is measured with `chrono::steady_clock` (CPU sampling). `execution_time_ms` is measured with CUDA events (kernel only). `handler_time_ms` is the total subprocess wall time measured by the Python handler.
