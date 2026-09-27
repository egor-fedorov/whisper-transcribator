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

1. Run GCC/Clang CTest, clang-format, ShellCheck and wrapper ASan/UBSan checks.
2. Build CPU and CUDA archives. Inspect bundled dependencies, source packages,
   vendor notices and SHA256SUMS. No glibc or host driver may be bundled.
3. Run `tests/smoke/prepare-smoke.sh` once, then `tests/smoke/smoke.sh` with networking disabled
   and a fresh output directory. Use the public 11-second fixture, not lectures.
4. Check the CPU archive in clean Ubuntu 22.04 without Python/system FFmpeg, and
   the CPU runtime image. Confirm nonempty TXT/SRT/VTT/JSON and nonzero failure exits.
   Before release, opt into `tests/packaging/portable.sh BUNDLE OUTPUT FIXTURES` for inference
   with QEMU's non-AVX2 `qemu64` CPU. Omitting `FIXTURES` only checks backend loading
   and missing-plugin diagnostics, without downloading weights or running inference.
5. On a trusted GPU machine, repeat the offline smoke with the CUDA archive and
   `cuda` as the last script argument; test the CUDA image with driver injection.
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

The `package` job builds and verifies the archive, dependencies and `doctor`,
including a model-free non-AVX2 loader check with a 30-second timeout. It never
downloads models or performs speech recognition. The dependent `smoke` job
downloads that exact archive, checks short native inference in clean Ubuntu, and
wraps the same bundle in the non-root Docker runtime without recompiling.
Slow QEMU inference is only in the opt-in `portable-inference` job: start it before
release with `gh workflow run ci.yml -f run_portable_inference=true`. It is not
part of pull-request or push CI. The separate compiler jobs retain their short
source-build integration and sanitizer checks.

Compiler caches are keyed by toolchain, sanitizer configuration and dependency pins; tests always run. The extra clean rebuild/cache-hit check requires the manual `verify_ccache` input. Buildx additionally caches unchanged packaging layers in separate CPU/CUDA GHA v2 scopes. Source-layer changes still rebuild their dependents. Cache export has a two-minute limit and is nonessential; a missing cache never skips checks or changes correctness requirements.
CTest exercises interrupted HTTPS downloads using a loopback TLS server and a
temporary trusted certificate; it does not download large model weights.
Streaming tests repeat the public fixture into a short recording. Long-duration
memory tests use synthetic audio and fake inference, not full lecture recognition.

## Prepare And Publish

The 0.4.0 release commit has an empty `WT_VERSION_SUFFIX` and reports `0.4.0`. Development versions use a `-dev+g<revision>` suffix, plus `.dirty` when applicable; source provenance also remains available in release metadata. Git-less builds without explicit provenance record `unknown`. Docker receives revision/dirty state through build arguments because `.git` is deliberately excluded. CMake refreshes metadata during every build, rewriting the generated files only when it changes. For each release, update the project version and clear the suffix in the reviewed release commit, then build from that clean commit. Do not override the suffix just to rename a development archive: the release helper rejects mismatched versions, source revisions and dirty/unknown provenance. Publishing remains a separate approved operation.

Keep the two archives and their original `SHA256SUMS` under
`.build/artifacts/cpu/` and `.build/artifacts/cuda/`. Prepare from a clean checkout
of the release commit after its `main` CI has passed. The helper verifies both
checksums and every packaged project source against Git, then checks local and
remote tag targets and CI. It never publishes automatically or overwrites assets.

```bash
version=0.4.0 # only after the release commit and all gates above
bash packaging/release.sh "$version" .build/artifacts
git tag -a "v$version" -m "Release $version"
git push origin "v$version"
bash packaging/release.sh "$version" .build/artifacts --draft
gh release view "v$version"
```

The draft contains CPU/CUDA archives, combined checksums, release notes and the
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
overrides are trusted and bypass archive hash verification. App build versions,
FFmpeg pin and source package versions are recorded in the bundle. Apt security
updates mean rebuilds need not have identical bytes.
