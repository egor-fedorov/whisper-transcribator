# Contributing

Keep the product a small local CLI with one whisper.cpp backend. Do not add a
second inference implementation or revive the historical Python pipeline.

## Build And Test

On Ubuntu 24.04 install `cmake ninja-build g++ pkg-config libavformat-dev
libavcodec-dev libswresample-dev libcurl4-openssl-dev libssl-dev`.
Upstream sources are fetched at pinned revisions with SHA-256 checks.

```bash
cmake --preset cpu
cmake --build --preset cpu -j4
ctest --preset cpu
clang-format-18 --dry-run --Werror src/*.cpp src/*.hpp tests/*.cpp
shellcheck tests/*.sh packaging/*.sh
bash tests/release.sh
cmake --preset asan
cmake --build --preset asan -j4
ctest --preset asan
```

ASan/UBSan instrument this project's code, not an audit of upstream dependencies.
LeakSanitizer cannot run under ptrace-based sandboxes; run these checks normally,
rather than disabling leak detection. GCC and Clang are checked in CI.

The optional inference check uses only a public 11-second JFK recording:

```bash
bash tests/prepare-smoke.sh .build/cpu/whisper-transcribator .build/fixtures
bash tests/smoke.sh .build/cpu/whisper-transcribator .build/fixtures/jfk.wav \
  .build/fixtures/ggml-tiny.bin .build/fixtures/ggml-silero-v6.2.0.bin .build/smoke
```

Use a fresh smoke output directory for each run. Fixtures/weights are downloaded
only during preparation. CI then disables networking for actual inference.
Do not add private recordings, transcripts or model weights. Never run full
lectures for routine checks or benchmarks.

## Layout

- `src/`: CLI, job planning, outputs, cache/downloads, decoding and inference.
- `cmake/`: pinned dependencies and generated metadata.
- `tests/`: model-free checks and short optional integration checks.
- `packaging/`: archive/container recipes and dependency collection.
- `docs/`: usage, migration, distribution and design decisions.
- `.build/`: all generated build, test and release artifacts.

Add regressions for output safety, error codes and interrupted operations.
Keep manual GPU tests on a trusted machine; never execute untrusted PR code on
a personal GPU runner. Logical commits should separate behavior, tests,
packaging and documentation. Do not publish releases automatically.

Open a PR against `main`; required CI checks must pass on an up-to-date branch.
Force pushes and deletion of `main` are disabled. Use squash merges for focused
changes. Release preparation is documented in [releasing](docs/releasing.md).
