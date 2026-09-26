# Build And Release

Version 0.3 is a native CLI. Build artifacts live under `.build/`; nothing is
published automatically. Do not move or delete local `models/`, `transcripts/`
or `benchmark-results/` during release cleanup. The immutable `v0.2.0` tag is the
historical Python implementation, not another active release line.

## CPU

For a developer build with system FFmpeg (5.0+), libcurl and OpenSSL:

```bash
cmake --preset cpu
cmake --build --preset cpu -j4
ctest --preset cpu
.build/cpu/whisper-transcribator --help
```

Use the Ubuntu 22.04 recipe for portable archives. It includes its FFmpeg,
inference libraries, TLS trust store, notices and sources; no weights or Python.

```bash
docker build -f packaging/Dockerfile --target archive \
  --output type=local,dest=.build/artifacts/cpu .
cd .build/artifacts/cpu
sha256sum -c SHA256SUMS
mkdir -p unpacked
tar -xzf whisper-transcribator-0.3.0-linux-x86_64-cpu.tar.gz -C unpacked
./unpacked/bin/whisper-transcribator doctor --device cpu --json
```

From the repository root, `--target runtime -t whisper-transcribator:cpu` builds
the optional image using the exact same bundle. Models use `/models` in Docker.

## CUDA

Use the pinned CUDA 12.8 builder; only the runtime and driver are needed to run
the resulting archive, not an installed toolkit. A source build with
`cmake --preset cuda` requires a locally installed CUDA toolkit and compatible
host compiler.

```bash
CUDA_IMAGE=nvidia/cuda:12.8.1-devel-ubuntu22.04@sha256:a99a1860ba8e2916e5c3e73b72ec4c4301653a84586e05bfc9a2aa2d58027e97
docker build -f packaging/Dockerfile --target archive \
  --build-arg BUILD_IMAGE="$CUDA_IMAGE" --build-arg WT_CUDA=ON \
  --output type=local,dest=.build/artifacts/cuda .
docker build -f packaging/Dockerfile --target runtime \
  --build-arg BUILD_IMAGE="$CUDA_IMAGE" --build-arg WT_CUDA=ON \
  -t whisper-transcribator:cuda .
```

Default compiled architectures: SM75/80/86/89/90. Actual hardware validation is
limited to RTX 4060 Ti (SM89); compilation is not a test of the other GPUs.
For local development only, `--build-arg CUDA_ARCHITECTURES=89` reduces build time.
Do not label that restricted artifact as the full release matrix.

```bash
docker run --rm --gpus all whisper-transcribator:cuda doctor --device cuda --json
```

On hosts whose Container Toolkit does not expose UVM nodes, also pass
`--device /dev/nvidia-uvm --device /dev/nvidia-uvm-tools`. Ensure these nodes exist
and that the chosen container UID has access. Do not silently switch to CPU or
use `--privileged` to hide runtime configuration errors. Prefer non-root execution
with narrowly scoped mounts once device permissions are configured.

## Gates

1. Run GCC/Clang CTest, clang-format, ShellCheck and wrapper ASan/UBSan checks.
2. Build CPU and CUDA archives. Inspect bundled dependencies, source packages,
   vendor notices and SHA256SUMS. No glibc or host driver may be bundled.
3. Run `tests/prepare-smoke.sh` once, then `tests/smoke.sh` with networking disabled
   and a fresh output directory. Use the public 11-second fixture, not lectures.
4. Check the CPU archive in clean Ubuntu 22.04 without Python/system FFmpeg, and
   the CPU runtime image. Confirm nonempty TXT/SRT/JSON and nonzero failure exits.
5. On a trusted GPU machine, repeat the offline smoke with the CUDA archive and
   `cuda` as the last script argument; test the CUDA image with driver injection.
6. Scan the entire Git history for secrets. Review the diff and ensure private
   data/weights and generated artifacts remain ignored. Update release notes.
7. Only after approval, tag and publish the verified artifacts and checksums.

CI runs CPU/compiler/sanitizer/package checks; optional manual dispatch builds
the CUDA archive but does not certify GPU inference. It does not publish assets
or expose a personal GPU runner to pull requests. Configuring a workflow is not
the same as having a successful remote Actions run.

Dependencies downloaded by CMake can be supplied offline via
`FETCHCONTENT_SOURCE_DIR_WHISPER` and `FETCHCONTENT_SOURCE_DIR_CLI11`; those local
overrides are trusted and bypass archive hash verification. App build versions,
FFmpeg pin and source package versions are recorded in the bundle. Apt security
updates mean rebuilds need not have identical bytes.
