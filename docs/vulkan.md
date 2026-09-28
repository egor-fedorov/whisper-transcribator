# Experimental Vulkan

Vulkan is a source-build experiment for Linux x86_64 and Windows x64, using the
same pinned whisper.cpp engine and GGML models. No Vulkan release archive or
Docker image is published. CUDA and Metal remain supported as before.

Use **`--device vulkan` explicitly**. `auto` still chooses CUDA, then Metal, then
CPU; it never opts into Vulkan. The first GPU belonging to the requested backend
is selected, even in a mixed CUDA/Vulkan build. `doctor --device vulkan --json`
reports the device name, description and whisper.cpp GPU ordinal, plus all
available devices. The ordinal is diagnostic, not a persistent hardware ID.

Missing GPU/driver/plugin or failed GPU initialization is an error, not a silent
CPU-only run. Use `--device cpu` deliberately if needed. VAD and operations not
supported by the GPU backend can still run on CPU. Resuming with a different
backend is rejected through the existing `/run/device` compatibility check;
there is no output/checkpoint schema migration.

## Build

Install the ordinary [source-build dependencies](../CONTRIBUTING.md) and a Vulkan
GPU driver. The SDK supplies build tools and headers, **not a GPU driver**.
The prototype CI pins LunarG SDK 1.4.357.0, with hashes in
`packaging/vulkan-sdk.json`; system SDKs also work when they provide Vulkan
headers, a loader library, `glslc` and the `SPIRV-Headers` CMake package.
Keep the build directory, including its adjacent ggml plugins and dependencies.

Linux, after extracting the SDK and sourcing its `setup-env.sh`:

```bash
cmake --preset vulkan -DWT_WERROR=ON
cmake --build --preset vulkan -j4
ctest --preset vulkan
.build/vulkan/whisper-transcribator doctor --device vulkan --json
.build/vulkan/whisper-transcribator lecture.mkv --device vulkan --model small
```

To compare CUDA and Vulkan in the same Linux binary, also configure with
`-DWT_CUDA=ON -DCMAKE_CUDA_ARCHITECTURES=89` for an RTX 4060 Ti; choose the CUDA
architecture appropriate to other hardware. This requires a CUDA toolkit and
its supported host compiler. Do not mix incompatible compilers in one build directory.

Windows, from the x64 MSVC developer PowerShell described in CONTRIBUTING,
after building FFmpeg and checking out the pinned vcpkg:

```powershell
./packaging/vulkan-sdk.ps1
./packaging/windows-build.ps1 -Vulkan -Build .build/windows-vulkan
.build/windows-vulkan/bin/whisper-transcribator.exe doctor --device vulkan --json
```

The SDK helper verifies and caches the official download, installs/extracts only
build tools into `.build`, and sets the current shell's SDK environment. It does
not change the release recipes or install a display driver.

## Validation

The separate Experimental Vulkan workflow compiles Linux and Windows builds,
runs model-free CTest, and verifies CPU operation and explicit Vulkan rejection
with all Vulkan ICDs hidden. It downloads no model and performs no inference.
The ordinary tiny smoke additionally forces an unavailable GPU ordinal and
checks that initialization fails, while CPU initialization of that model works.

Hardware validation uses only the public 11-second JFK sample and its 41-second
streaming fixture. Pass `vulkan` as the last argument to `tests/smoke/smoke.sh`
and `tests/smoke/streaming-smoke.sh`. These check all formats, interruption/resume,
VAD and silence handling; they are not an evaluation of transcription quality.

For a short Linux/NVIDIA comparison with a mixed CUDA/Vulkan binary:

```bash
bash tests/benchmarks/vulkan.sh .build/vulkan/whisper-transcribator \
  .build/vulkan/tests/wt-audio-fixture .build/fixtures/jfk.wav \
  models/whisper-cpp/ggml-small.bin .build/vulkan-benchmark
```

This script rejects audio/models other than the pinned fixtures. It performs one
warmup and three rotating-order measurements per backend, with two CPU threads,
beam size 1, no VAD and identical weights. CLI time includes verification/loading;
round zero is retained separately. RSS comes from GNU time. VRAM is sampled on
the entire NVIDIA device every 100 ms and includes background applications; the
report records baseline, observed peak and their difference, not an exact
per-process peak. `WT_BENCH_GPU` selects the NVIDIA telemetry device (default 0),
not the inference GPU; ensure they match. Use a fresh output directory.

Native Windows GPU execution and AMD/Intel hardware are not yet validated.
Hosted model-free CI, Wine, and software Vulkan are not evidence of hardware
acceleration. Archive distribution remains a separate decision after validation.
