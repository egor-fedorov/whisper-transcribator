# Experimental Vulkan

Vulkan is experimental on Linux x86_64 and Windows x64, using the same pinned whisper.cpp engine and GGML models. Version 0.5.0 adds optional Vulkan archives; CUDA and Metal remain supported as before. No prebuilt Vulkan container image is published.

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

## Archives

Choose `linux-x86_64-vulkan.tar.gz` or `windows-x86_64-vulkan.zip` from the release and verify `SHA256SUMS` as in the [README](../README.md). Keep the complete bundle, including CPU plugins. Linux retains the glibc 2.35 baseline; Windows retains the x64 CPU archive's OS requirement and unsigned-binary limitations.

Install a current Vulkan-capable GPU driver. Linux bundles Ubuntu's Vulkan loader, but no hardware ICD/driver. Windows relies on the driver's `vulkan-1.dll`; neither the loader nor the SDK is bundled. Khronos [recommends using the installed loader](https://vulkan.lunarg.com/doc/view/1.4.328.1/windows/LoaderApplicationInterface.html). A Vulkan SDK is not needed to run either archive.

```bash
bin/whisper-transcribator doctor --device vulkan --json
bin/whisper-transcribator lecture.mkv --device vulkan --model small
```

On Windows use `./bin/whisper-transcribator.exe`. A working driver alone does not guarantee that every device meets the pinned backend's capabilities: check `doctor` first. Linux/NVIDIA release verification is automated; native Windows GPU execution and AMD/Intel devices remain unvalidated. This is an explicitly experimental distribution, not a claim of tested acceleration on every Vulkan GPU. To use CPU instead, pass `--device cpu`.

## Build

Install the ordinary [source-build dependencies](../CONTRIBUTING.md) and a Vulkan
GPU driver. The SDK supplies build tools and headers, **not a GPU driver**.
The prototype CI pins LunarG SDK 1.4.357.0, with hashes in
`packaging/vulkan-sdk.json`; system SDKs also work when they provide Vulkan
headers, a loader library, `glslc` and the `SPIRV-Headers` CMake package.
Keep the build directory, including its adjacent ggml plugins and dependencies.

Linux, after installing the loader development library (`libvulkan-dev` on Ubuntu),
extracting the SDK and sourcing its `setup-env.sh`:

```bash
cmake --preset vulkan -DWT_WERROR=ON
cmake --build --preset vulkan -j4
ctest --preset vulkan
.build/vulkan/whisper-transcribator doctor --device vulkan --json
.build/vulkan/whisper-transcribator lecture.mkv --device vulkan --model small
```

To compare CUDA and Vulkan in the same Linux binary, use a separate directory:

```bash
cmake --preset vulkan -B .build/vulkan-cuda -DWT_CUDA=ON -DCMAKE_CUDA_ARCHITECTURES=89
cmake --build .build/vulkan-cuda -j4
```

Architecture 89 is for an RTX 4060 Ti; choose the architecture appropriate to
other hardware. This requires a CUDA toolkit and its supported host compiler.
Do not mix incompatible compilers in one build directory.

Windows, from the x64 MSVC developer PowerShell described in CONTRIBUTING,
after building FFmpeg and checking out the pinned vcpkg:

```powershell
./packaging/vulkan-sdk.ps1
./packaging/windows-build.ps1 -Vulkan -Build .build/windows-vulkan
.build/windows-vulkan/bin/whisper-transcribator.exe doctor --device vulkan --json
```

The SDK helper verifies and caches the official download, installs/extracts only
build tools into `.build`, and sets the current shell's SDK environment. It does
not install a display driver. Packaging reuses its pinned headers/tools, not SDK runtime binaries.

## Validation

The separate Experimental Vulkan workflow builds Linux and Windows archives, runs model-free CTest, and verifies CPU operation and explicit Vulkan rejection with all Vulkan ICDs hidden. Linux verification uses clean Ubuntu 22.04 without an SDK; Windows ZIP verification uses a fresh runner and restricted PATH, checking imports and source provenance. It downloads no model and performs no inference.
The ordinary tiny smoke additionally forces an unavailable GPU ordinal and
checks that initialization fails, while CPU initialization of that model works.

Hardware validation uses only the public 11-second JFK sample and its 41-second
streaming fixture. Pass `vulkan` as the last argument to `tests/smoke/smoke.sh`
and `tests/smoke/streaming-smoke.sh`. These check all formats, interruption/resume,
VAD and silence handling; they are not an evaluation of transcription quality.

The owner-approved release workflow downloads the exact hosted Linux archive and its separate test helpers, then runs these checks offline in a clean container on the NVIDIA runner. `vulkan-verification.json` ties the result to the archive SHA-256 and source revision. No extra local/manual archive check is required. Windows GPU execution is not a release gate for the experimental ZIP. See [release automation](releasing.md#release-workflow).

For a short Linux/NVIDIA comparison with a mixed CUDA/Vulkan binary:

```bash
bash tests/benchmarks/vulkan.sh .build/vulkan-cuda/whisper-transcribator \
  .build/vulkan-cuda/tests/wt-audio-fixture .build/fixtures/jfk.wav \
  models/whisper-cpp/ggml-small.bin .build/vulkan-benchmark
```

This script rejects audio/models other than the pinned fixtures. It performs one
warmup and three rotating-order measurements per backend, with two CPU threads,
beam size 1, no VAD and identical weights. Install GNU time (`time` on Ubuntu),
or set `WT_BENCH_TIME` to its executable. CLI time includes verification/loading;
round zero is retained separately. RSS comes from GNU time. VRAM is sampled on
the entire NVIDIA device every 100 ms and includes background applications; the
report records baseline, observed peak and their difference, not an exact
per-process peak. `WT_BENCH_GPU` selects the NVIDIA telemetry device (default 0),
not the inference GPU; ensure they match. Use a fresh output directory.

## Local Comparison

2026-09-28, application commit `982b7d19be6a`, Linux x86_64, i5-12400F,
RTX 4060 Ti 16 GiB, NVIDIA 615.71.09, GCC 15.3, CUDA 13.4 (SM 89),
Vulkan SDK 1.4.357.0, FFmpeg 9.0.2. One Release binary loaded the Alder Lake CPU
plugin and both GPU plugins; Vulkan used GPU ordinal 1, CUDA ordinal 0.
No compilation or other test runs overlapped the measurements.

| Device | Three runs (s) | Median (s) | RTF | Peak RSS (MiB) | VRAM above baseline (MiB) |
| --- | --- | --- | --- | --- | --- |
| CPU | 11.66 / 11.82 / 11.75 | 11.75 | 0.287 | 979 | 3 (background variation) |
| CUDA | 1.44 / 1.25 / 1.41 | 1.41 | 0.034 | 699 | 864 |
| Vulkan | 1.28 / 1.25 / 1.42 | 1.28 | 0.031 | 456 | 745 |

RTF is wall time divided by the 41-second input duration (lower is better).
RSS/VRAM are maxima over the three measured runs. Device-wide VRAM baseline
was 1573 MiB; peaks were 1576/2437/2318 MiB for CPU/CUDA/Vulkan. Warmup times
were 11.68/1.39/2.29 seconds and are excluded from the medians.
The two-thread CPU result is a controlled comparison, not maximum CPU throughput.
CUDA/Vulkan ranges overlap: this supports comparable performance on this host,
not a general claim that Vulkan is faster. CUDA remains supported.

Tiny-model hardware smoke passed on CPU, CUDA and Vulkan, including all output
formats and Vulkan interruption/resume in a mixed-backend build. Linux CTest
passed 32/32 on CPU, Vulkan and mixed builds; ASan/UBSan passed 31/31. These
checks do not establish word-error rates or validate every model/driver combination.

Native Windows GPU execution and AMD/Intel hardware are not yet validated.
Hosted model-free CI, Wine, and software Vulkan are not evidence of hardware
acceleration. These limitations remain explicit for the experimental archives.
