# Build And Release

The 0.4 release line is a native CLI. Build artifacts live under `.build/`; nothing is
published automatically. Do not move or delete local `models/`, `transcripts/`
or `benchmark-results/` during release cleanup. The immutable `v0.2.0` tag is the
historical Python implementation, not another active release line.

## CPU

For a developer build with system FFmpeg (5.1+), libcurl and OpenSSL:

```bash
cmake --preset cpu
cmake --build --preset cpu -j4
ctest --preset cpu
.build/cpu/whisper-transcribator --help
```

Use the Ubuntu 22.04 recipe for portable archives. It includes its FFmpeg,
inference libraries, TLS trust store, notices and sources; no weights or Python.
The recipe enables `GGML_BACKEND_DL` and `GGML_CPU_ALL_VARIANTS` with
`GGML_NATIVE=OFF`. The loader selects a compatible installed CPU plugin, including
baseline x86_64 without AVX2; CUDA archives also retain this CPU fallback.
Packaging collects dependencies for every plugin, not only the executable.
`share/backends.txt` inventories the included modules.

Local source builds can opt into host-specific optimization with
`cmake --preset cpu -DGGML_NATIVE=ON`; do not redistribute that build as portable.
Host-native and dynamic portable configurations are mutually exclusive.

```bash
docker build -f packaging/Dockerfile --target archive \
  --build-arg WT_SOURCE_REVISION="$(git rev-parse HEAD)" \
  --build-arg WT_SOURCE_DIRTY="$(test -z "$(git status --porcelain --untracked-files=no)" && echo false || echo true)" \
  --output type=local,dest=.build/artifacts/cpu .
cd .build/artifacts/cpu
sha256sum -c SHA256SUMS
mkdir -p unpacked
tar -xzf whisper-transcribator-*-linux-x86_64-cpu.tar.gz -C unpacked
./unpacked/bin/whisper-transcribator doctor --device cpu --json
```

From the repository root, `--target runtime -t whisper-transcribator:cpu` builds
the optional image using the exact same bundle. Models use `/models` in Docker.
To wrap an already verified, unpacked archive without recompiling, use
`docker build -f packaging/Dockerfile --target runtime-bundle -t whisper-transcribator:cpu .build/artifacts/cpu/unpacked`.
This target's build context must be the unpacked archive, not the source checkout.

## ARM64

Linux aarch64 CPU archives use the same recipe on an arm64 host; CI builds them
on GitHub-hosted `ubuntu-24.04-arm` runners. The pinned Ubuntu 22.04 base images
are multi-architecture indexes, so these archives keep the glibc 2.35 baseline.
Ubuntu 22.04's GCC 11 cannot compile ggml's `armv9.2` (SME) CPU variants, so the
arm64 recipe uses the distribution's `clang-15` with `GGML_OPENMP=OFF`: Clang's
OpenMP runtime would add `libomp` and the whole LLVM source package to the bundle.
ggml's own thread pool was not slower than OpenMP in a local x86_64 comparison;
the C++ runtime remains GCC's. The archive contains CPU plugins from `armv8.0` to
`armv9.2` with SVE2/SME and selects one at startup.

```bash
docker build -f packaging/Dockerfile --target archive \
  --build-arg WT_SOURCE_REVISION="$(git rev-parse HEAD)" \
  --build-arg WT_SOURCE_DIRTY="$(test -z "$(git status --porcelain --untracked-files=no)" && echo false || echo true)" \
  --output type=local,dest=.build/artifacts/cpu-aarch64 .
bash tests/packaging/package.sh .build/artifacts/cpu-aarch64 # on the arm64 host
```

On an x86_64 host, `docker buildx build --platform linux/arm64` works through QEMU
emulation but is much slower. CUDA archives remain x86_64-only; the recipe rejects
`WT_CUDA=ON` on arm64. `share/build-metadata.env` records `WT_TARGET_ARCH`, and both
`tests/packaging/package.sh` and the release helper reject an archive whose name
and metadata disagree.

## macOS

The macOS archive `macos-arm64-metal` is built natively on an Apple silicon Mac; CI uses
GitHub's `macos-15` runners. `packaging/macos.sh` builds the same decoding-only FFmpeg as the
Linux recipe (`packaging/ffmpeg.sh`), then the application for macOS 14 and later with
dynamically loaded `apple_m1`, `apple_m2_m3` and `apple_m4` CPU variants, Metal and Accelerate,
and runs CTest. It copies every library the executable and plugins load, rewrites their
references to `@rpath` and signs each binary ad hoc. Only libraries that are part of macOS
(`/usr/lib` and `/System/Library`, such as libcurl, libc++, Metal and Accelerate) stay outside the
archive; the script and `tests/packaging/package.sh` reject anything else, such as Homebrew paths.

```bash
brew install ninja pkgconf ffmpeg-full openssl@3 jq # build and test tools, not bundled
WT_SOURCE_REVISION="$(git rev-parse HEAD)" \
WT_SOURCE_DIRTY="$(test -z "$(git status --porcelain --untracked-files=no)" && echo false || echo true)" \
  bash packaging/macos.sh .build/artifacts/macos-arm64
bash tests/packaging/package.sh .build/artifacts/macos-arm64
```

The archive has no Apple Developer ID signature and is not notarized; the README explains
quarantined browser downloads. HTTPS uses the system libcurl and trust store, so the archive has
no `cacert.pem`, and SHA-256 uses CommonCrypto, so it contains no OpenSSL.

## Windows

Native Windows CPU builds use MSVC on x64 and ClangCL on ARM64, the same pinned minimal FFmpeg recipe and vcpkg's pinned libcurl with Schannel. Follow [the contributor build instructions](../CONTRIBUTING.md#windows). CNG supplies SHA-256; OpenSSL is only a test-server dependency. The DLL-filled development/test directory is not a distribution archive: it also contains test executables and test-only libraries.

`test (windows-2025, x64)` builds and runs model-free CTest, including private ACLs, long and Unicode paths, junction rejection, locking, interruption and crash recovery. An additional standard-user check catches assumptions hidden by elevated CI.
`test (windows-11-arm, arm64)` repeats these checks natively on Windows ARM64 with
the `arm64-windows` vcpkg triplet and baseline ARMv8-A/NEON inference. It does not
inherit Linux's ARM multi-variant plugins. See [ARM64 toolchain limitations](windows.md#arm64).

After a successful source build, run in the same x64 developer PowerShell:

```powershell
./packaging/windows-bundle.ps1
./tests/packaging/windows.ps1 -Artifacts .build/artifacts/windows-x86_64
```

The bundle script reuses the build instead of compiling again. It traverses PE imports for the CLI
and every CPU plugin; only an explicit Windows-system DLL allowlist may remain external.
Required MSVC runtime files come exclusively from `VCToolsRedistDir/<x64|arm64>/Microsoft.VC*.CRT`, never
the build tree, System32 or debug directories. Every bundled CRT DLL must match its provenance
inventory. The unsigned ZIP includes dependency sources/notices, build metadata, vcpkg
SPDX records, DLL inventories and per-file checksums. The archive checksum is in `SHA256SUMS`.
No model or inference is needed to package it. The app-local runtime must be updated with the
application; an installed system-wide redistributable does not service those copies.

`package (windows-x86_64)` verifies the exact ZIP on a fresh Windows runner, including imports,
checksums, source blobs against Git HEAD and baseline/missing-plugin diagnostics with a restricted
runtime PATH. It runs verification twice in private temporary directories to check repeatability.
`package (windows-servercore)` checks the exact ZIP without an installed VC++ runtime, plus a
negative control with an app-local CRT DLL removed. Native artifacts expire after seven days.
The separate
`smoke (windows-x86_64)` downloads the same ZIP, prepares the public short fixture, blocks the
application's network access and checks CPU inference, Unicode paths and interrupted resume.
Test helpers are downloaded separately and never included in the ZIP. Hosted Windows CI checks
the baseline plugin explicitly; it does not emulate physical hardware without AVX2. None of
these jobs publishes a release.

The ARM64 build creates `.build/artifacts/windows-arm64/` automatically. Its
`package (windows-arm64)` and `smoke (windows-arm64)` jobs verify the matching ZIP
on fresh `windows-11-arm` runners. Pass `-Architecture arm64` to
`tests/packaging/windows.ps1`; mixing x64 and ARM64 PE images is an error.
The Server Core clean-runtime check remains x64-only, not an ARM64 certification.

The manual `run_windows_benchmark` input compares MSVC with ClangCL on the same runner. It is
not a packaging gate and does not change the production compiler. See [Windows validation and
signing status](windows.md); unsigned releases can be blocked by Windows 11 Smart App Control.

## CUDA

Use the pinned CUDA 12.8 builder; only the runtime and driver are needed to run
the resulting archive, not an installed toolkit. A source build with
`cmake --preset cuda` requires a locally installed CUDA toolkit and compatible
host compiler.

```bash
CUDA_IMAGE=nvidia/cuda:12.8.1-devel-ubuntu22.04@sha256:a99a1860ba8e2916e5c3e73b72ec4c4301653a84586e05bfc9a2aa2d58027e97
docker build -f packaging/Dockerfile --target archive \
  --build-arg BUILD_IMAGE="$CUDA_IMAGE" --build-arg WT_CUDA=ON \
  --build-arg WT_SOURCE_REVISION="$(git rev-parse HEAD)" \
  --build-arg WT_SOURCE_DIRTY="$(test -z "$(git status --porcelain --untracked-files=no)" && echo false || echo true)" \
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

1. Run GCC/Clang CTest, clang-format, ShellCheck and wrapper ASan/UBSan checks, and CTest on macOS arm64.
2. Build Linux x86_64 CPU/CUDA, Linux aarch64 CPU, macOS arm64 and Windows x64/ARM64 CPU archives. Inspect bundled
   dependencies, source packages, vendor notices and SHA256SUMS. No glibc, host driver or
   macOS or Windows system library may be bundled.
3. Run `tests/smoke/prepare-smoke.sh` once, then `tests/smoke/smoke.sh` with networking disabled
   and a fresh output directory. Use the public 11-second fixture, not lectures.
4. Check the CPU archive in clean Ubuntu 22.04 without Python/system FFmpeg, and
   the CPU runtime image. Confirm nonempty TXT/SRT/VTT/JSON and nonzero failure exits.
   Before release, opt into `tests/packaging/portable.sh BUNDLE OUTPUT FIXTURES` for inference
   with QEMU's non-AVX2 `qemu64` CPU (x86_64) or `cortex-a53` (ARMv8.0, aarch64). Omitting `FIXTURES` only checks backend loading
   and missing-plugin diagnostics, without downloading weights or running inference.
   Check the macOS archive offline on clean macOS 14 and 15 runners with CPU and Metal.
   Check the Windows ZIP on a fresh native runner with restricted PATH, its app-local runtime,
   Unicode paths, baseline/missing-plugin diagnostics and offline interrupted resume.
5. On a trusted GPU machine, repeat the offline smoke with the CUDA archive and
   `cuda` as the last script argument; test the CUDA image with driver injection.
   On an Apple silicon Mac, repeat it with the macOS archive and `metal`.
6. Scan the entire Git history for secrets. Review the diff and ensure private
   data/weights and generated artifacts remain ignored. Update release notes.
7. Only after approval, tag and publish the verified artifacts and checksums.

CI runs on pushes to `main` and pull requests, without duplicate push runs for
PR branches. The protected `main` requires the GCC, Clang/sanitizer, package, smoke and
secret checks; no second reviewer is required for this single-maintainer project.
CI runs CPU/compiler/sanitizer/package checks; optional manual dispatch builds
the CUDA archive but does not certify GPU inference. It does not publish assets
or expose a personal GPU runner to pull requests. The first remote checks passed;
every release still needs a successful run for its exact source commit.
The aarch64 legs run natively on `ubuntu-24.04-arm` as `test (gcc, g++, OFF, aarch64)`,
`package (aarch64)` and `smoke (aarch64)`; existing x86_64 check names are unchanged.
The aarch64 checks are required by branch protection, alongside native Windows test and smoke checks.
`test (macos-15, arm64)` builds from source with Homebrew dependencies and runs CTest, the
`hdiutil` exFAT check and offline CPU smoke tests, with network access denied by
`sandbox-exec`. It repeats the smoke with Metal only where the hosted runner exposes Metal.
`package (macos-arm64)` builds, tests and verifies the macOS archive. `smoke (macos-arm64)` and
`smoke (macos-arm64, macOS 14)` run it on clean runners: only archive and macOS libraries may
load, CPU and Metal inference run offline, and a quarantined copy shows how Gatekeeper treats a
browser download. Hosted runners have a virtual GPU, so Metal on real hardware remains a manual
gate. These macOS checks are required by branch protection. Add `package (windows-x86_64)`
after its first successful run without removing existing required checks.

The `package` job builds and verifies the archive, dependencies and `doctor`,
including a model-free baseline-CPU loader check (non-AVX2 x86_64 or ARMv8.0) with a 30-second timeout. It never
downloads models or performs speech recognition. The dependent `smoke` job
downloads that exact archive, checks short native inference in clean Ubuntu, and
wraps the same bundle in the non-root Docker runtime without recompiling.
Slow QEMU inference is only in the opt-in `portable-inference` job: start it before
release with `gh workflow run ci.yml -f run_portable_inference=true`. It is not
part of pull-request or push CI. The separate compiler jobs retain their short
source-build integration and sanitizer checks.

Compiler caches are keyed by toolchain, sanitizer configuration and dependency pins; tests always run. The extra clean rebuild/cache-hit check requires the manual `verify_ccache` input. Buildx additionally caches unchanged packaging layers in separate x86_64 CPU, aarch64 CPU and CUDA GHA v2 scopes. Source-layer changes still rebuild their dependents. Cache export has a two-minute limit and is nonessential; a missing cache never skips checks or changes correctness requirements.
CTest exercises interrupted HTTPS downloads using a loopback TLS server and a
temporary trusted certificate; it does not download large model weights.
Streaming tests repeat the public fixture into a short recording. Long-duration
memory tests use synthetic audio and fake inference, not full lecture recognition.

## Prepare And Publish

The 0.4.0 release commit has an empty `WT_VERSION_SUFFIX` and reports `0.4.0`. Current development targets `0.5.0-dev`. Development versions use a `-dev+g<revision>` suffix, plus `.dirty` when applicable; source provenance also remains available in release metadata. Git-less builds without explicit provenance record `unknown`. Docker receives revision/dirty state through build arguments because `.git` is deliberately excluded. CMake refreshes metadata during every build, rewriting the generated files only when it changes. For each release, update the project version and clear the suffix in the reviewed release commit, then build from that clean commit. Do not override the suffix just to rename a development archive: the release helper rejects mismatched versions, source revisions and dirty/unknown provenance. Publishing remains a separate approved operation.

Keep the six archives and their original `SHA256SUMS` under
`.build/artifacts/cpu/`, `.build/artifacts/cuda/`, `.build/artifacts/cpu-aarch64/` and
`.build/artifacts/macos-arm64/`, plus `.build/artifacts/windows-x86_64/` and
`.build/artifacts/windows-arm64/` for the ZIPs
(the `native-cpu-aarch64`, `native-macos-arm64`, `native-windows-x86_64` and `native-windows-arm64` CI artifacts
of the release commit). Prepare from a clean checkout
of the release commit after its `main` CI has passed. The helper verifies every
checksum, target system and architecture and packaged project source against Git (using `unzip`
for the Windows ZIP), then checks local and
remote tag targets and CI. It never publishes automatically or overwrites assets.

```bash
version=0.4.0 # only after the release commit and all gates above
bash packaging/release.sh "$version" .build/artifacts
git tag -a "v$version" -m "Release $version"
git push origin "v$version"
bash packaging/release.sh "$version" .build/artifacts --draft
gh release view "v$version"
```

The draft contains the six archives, combined checksums, release notes and the
source commit/CI link. This validates artifact integrity and source correspondence,
not that CUDA inference ran: the short hardware smoke gate above remains mandatory
for the exact archive to be published. After reviewing the draft and the GPU check:

```bash
gh release edit "v$version" --draft=false --latest
```

If an upload fails, inspect the draft before retrying; the helper deliberately
does not delete releases or use `--clobber`. GitHub Actions artifacts expire and
are not a substitute for release assets. Dependency updates arrive through
Dependabot for Actions and Docker base images. FetchContent/FFmpeg updates remain manual and must update verified SHA-256 pins together with versions; automatic pin maintenance is a separate follow-up. Merging workflow changes with `gh` requires the `workflow` token scope.

Dependencies downloaded by CMake can be supplied offline via
`FETCHCONTENT_SOURCE_DIR_WHISPER`, `FETCHCONTENT_SOURCE_DIR_CLI11` and
`FETCHCONTENT_SOURCE_DIR_NLOHMANN_JSON`; those local
overrides are trusted and bypass archive hash verification. They are also used unpatched:
apply `cmake -DWT_WHISPER_SOURCE=DIR -P cmake/patch-whisper.cmake` to a whisper.cpp override. App build versions,
FFmpeg pin and source package versions are recorded in the bundle. Apt security
updates mean rebuilds need not have identical bytes.
