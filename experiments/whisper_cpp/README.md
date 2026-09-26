# Native Backend Experiment

This is an independent C++17/whisper.cpp prototype, **not** the production CLI.
The main application remains Python/faster-whisper. Rewriting its orchestration
in Rust alone would not replace CTranslate2/whisper.cpp or make inference faster.

## Build And Run

Linux x86_64, CMake >= 3.22, a C++17 compiler, pkg-config and recent FFmpeg
development libraries are required. CUDA additionally requires the CUDA toolkit.
whisper.cpp is fetched at commit `927cfce34f31707e17f2bff35c349632fb9e2c3a`
(tag v1.9.4, reported runtime version `1.9.4-dev`).

```bash
cmake -S experiments/whisper_cpp -B build/native -DCMAKE_BUILD_TYPE=Release
cmake --build build/native -j4
ctest --test-dir build/native --output-on-failure
build/native/whisper-transcribator-native lecture.mp4 \
  --model models/whisper-cpp/ggml-large-v3.bin --output-dir results \
  --device cpu --language ru --cpu-threads 4 --no-vad
```

For GPU add `-DNATIVE_CUDA=ON -DCMAKE_CUDA_ARCHITECTURES=89` when configuring
(89 is the test machine, not every NVIDIA GPU), then use `--device cuda`.
VAD is enabled by default and requires `--vad-model PATH`; `--no-vad` disables it.
CTranslate2 model directories cannot be used: download GGML weights separately.
Models are not embedded in binaries or archives.

## Archive

```bash
docker build -f experiments/whisper_cpp/Dockerfile \
  --build-arg NATIVE_CUDA=OFF --target archive \
  --output type=local,dest=dist/native-cpu .
docker build -f experiments/whisper_cpp/Dockerfile \
  --build-arg NATIVE_CUDA=ON --target archive \
  --build-arg CUDA_ARCHITECTURES=89 \
  --output type=local,dest=dist/native-cuda .
```

The archive contains `bin/`, `lib/`, `sources/` and `licenses/`. Keep them together;
RPATH is relative. This is not a single static executable. The default build base
is pinned Ubuntu 22.04/CUDA 12.8.1; CPU archives need no CUDA driver. CUDA archives
require a compatible host NVIDIA driver. Neither glibc nor the driver is bundled.
CPU builds currently require AVX2/FMA/F16C-capable hardware. No claim is made about
all Linux distributions or untested GPU architectures. Change CUDA_ARCHITECTURES
for your target GPU (the Dockerfile default builds 75/80/86/89/90 and takes longer).

FFmpeg is a minimal shared LGPL build, without GPL/nonfree components or network
protocols. It supports common MOV/MP4, Matroska, WAV, MP3, Ogg, FLAC, AAC and AIFF
inputs, not every codec supported by the Python/PyAV distribution. FFmpeg source,
configuration and upstream notices accompany the artifact. CUDA and compiler
runtime libraries retain their own licenses; do not treat the entire archive as
MIT. Audit the final dependency list and notices before distributing binaries.
See [THIRD_PARTY.md](THIRD_PARTY.md) for the dependency checklist.

## Parity

| Capability | Python | Prototype |
| --- | --- | --- |
| Multiple files, model reused | yes | yes |
| TXT/SRT/JSON, common segment fields | yes | yes; unavailable metrics are null |
| CPU / explicit CUDA | yes | yes; no silent fallback |
| VAD | bundled Silero | explicit GGML Silero model |
| Atomic individual outputs | yes | yes; no overwrite |
| Directory discovery, jobs, numbered mapping | yes | no |
| Model download/cache management | yes | no |
| int8, batching, word timestamps, prompt | yes | no |
| Chunking / resume | no | no |

Unsupported switches fail explicitly. Signal cancellation is checked by inference
callbacks; it is not guaranteed to be immediate during model load or media decode.
Both implementations currently decode whole recordings into memory. Neither is a
solution for bounded-memory transcription yet. Multi-format output is not a
three-file transaction.

## Comparison

See [BENCHMARK.md](BENCHMARK.md) for measured results and caveats. The runner is
`tools/benchmark.py`: `prepare --inputs FILE1 FILE2 FILE3`, then `cpu`, `gpu`,
and `full`. It prepares four fixed 120-second clips and records wall time, child
peak RSS and sampled GPU process VRAM. CPU/GPU clip runs default to three repeats;
the full first lecture runs once per backend. Private media, paths and raw output
stay under ignored `benchmark-results/`; nothing is uploaded.

The comparison disables VAD, uses beam size 5 and four CPU threads, and loads the
model once per run. CPU uses small (CTranslate2 float32 vs unquantized GGML mixed
precision), GPU uses large-v3 (CTranslate2 float16 vs GGML). These are comparable
tasks, not bit-identical decoders. Without reference transcripts there is no WER
measurement and no justified claim of equal recognition quality.
