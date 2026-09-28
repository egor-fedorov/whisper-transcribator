# Changelog

## Unreleased
- Dispatch Windows ARM64 CPU inference to baseline, DOTPROD, FP16 or I8MM plugins using ggml's existing Windows feature probes. Keep a baseline fallback, add the missing Windows build variants without updating whisper.cpp, and verify CPU selection from the packaged ZIP.
- Smoke-test the pinned 11-second JFK MP3 as well as WAV on Windows x64/ARM64, offline with Unicode paths, expected speech and duration checks.
- Add native Windows ARM64 CPU builds with ClangCL and a portable ARMv8-A/NEON plugin. Build, verify and smoke-test a separate `windows-arm64-cpu.zip` on `windows-11-arm`; validate every PE dependency's architecture, preserve app-local runtime/source inventories and require the sixth archive when preparing a release. Windows x64 remains on MSVC; ARM64 GPU/NPU acceleration is not included.
- Add experimental, source-build-only Vulkan on Linux/Windows through `WT_VULKAN` and explicit `--device vulkan`; leave automatic selection and release archives unchanged. Select the correct GPU ordinal in mixed-backend builds, report GPU diagnostics, and reject silent CPU fallback on GPU initialization failure. Add model-free Vulkan CI and short public-fixture validation/benchmark tooling.
- Identify post-0.4.0 builds as 0.5.0-dev with source provenance. Document Windows Smart App Control restrictions and the pending signing setup.
- Harden Windows packaging with retried source downloads, exclusive redist CRT lookup and inventory checks, normalized SDK metadata, repeatable temporary verification, and a Git source-blob comparison. Check the ZIP in Server Core without a preinstalled VC++ runtime and keep native artifacts for seven days.
- Add an opt-in same-Windows-runner MSVC/ClangCL comparison using small and a 41-second public fixture; keep MSVC as the production default and record actual CPU plugin coverage.
- Add a model-free Windows x64 CPU ZIP with the runtime dependency closure, app-local MSVC runtime, licenses, sources and checksums. Verify the exact archive on fresh native runners; keep packaging separate from short offline inference/resume checks. Require the ZIP as the fifth artifact in future release preparation. Existing 0.4.0 assets are unchanged.
- Enable native Windows x64 CPU builds with MSVC, a UTF-8/long-path manifest, Schannel downloads, CNG hashing and private checkpoint ACLs. Add native Windows CTest and separate short offline inference/resume CI; port process, TLS and memory tests without dropping recovery coverage. Use thread-safe cancellation, refuse failed private ACL initialization, and request write-through output replacement.
- Document running the Linux archives on Windows under WSL 2, including CUDA and `/mnt/c` limitations. Move operating-system calls (files, locks, no-replace renames, signals, library paths and CPU limits) into `src/platform/` with a POSIX implementation for Linux and macOS, as preparation for a native Windows port; behavior is unchanged, and a CTest check keeps system headers out of the rest of the sources.
- Publish a relocatable macOS archive (`macos-arm64-metal`, Apple silicon, macOS 14+) with Metal and dynamically selected M1, M2/M3 and M4 CPU variants. Binaries are signed ad hoc, without an Apple Developer ID or notarization; downloads with curl run directly, browser downloads need their quarantine attribute removed. CI checks the archive's dependencies, signatures and offline CPU/Metal inference on clean macOS 14 and 15 runners, and the release helper requires it as a fourth asset. Linux and macOS archives share one FFmpeg recipe, build metadata records `WT_TARGET_OS`, and macOS computes SHA-256 with CommonCrypto instead of OpenSSL.
- Run whisper.cpp's built-in VAD with the selected CPU thread count instead of a fixed four. Without OpenMP, which macOS builds and Linux aarch64 archives do not use, VAD was up to 80 times slower with fewer than four available cores, for example in 2-vCPU virtual machines or CPU-limited containers. The pinned whisper.cpp source is patched at build time.
- Build and test on macOS (Apple silicon) with Homebrew FFmpeg and OpenSSL, and add `--device metal`; `--device auto` now prefers CUDA, then Metal, then CPU. Automatic CPU threads use performance cores on macOS, no-clobber publication uses `renamex_np` with `RENAME_EXCL`, and file syncs use `F_FULLFSYNC`. A job whose output already is a file published earlier in the same run, as happens for names differing only in letter case on APFS, exFAT or FAT, now fails before its inference instead of replacing that file. Checkpoints on macOS volumes that ignore ownership, such as FAT and exFAT drives, get the checkpoint privacy warning. CI runs CTest, the exFAT check and offline smoke tests on a macos-15 arm64 runner.
- Build and verify a Linux aarch64 CPU archive (`linux-aarch64-cpu`, glibc 2.35+, CPU plugins from ARMv8.0 to ARMv9.2 with SVE2/SME) natively on GitHub arm64 runners, using Ubuntu 22.04's clang-15 without OpenMP. Record the target architecture in build metadata and require matching x86_64 CPU/CUDA and aarch64 CPU archives in release checks. CUDA remains x86_64-only; existing x86_64 check and archive names are unchanged.
- Support outputs, checkpoints and the model cache on FAT32, exFAT and other filesystems without POSIX permissions or hard links, such as USB drives, SD cards and many FUSE/SMB mounts. No-clobber publication uses an atomic no-replace rename (`renameat2` with `RENAME_NOREPLACE`), falling back to hard links and finally to a locked existence check with a weaker guarantee. Checkpoints stay under the output directory with a one-time warning that their privacy depends on mount options; strict owner, mode, symlink and hard-link checks remain on filesystems that store permissions. CI repeats checkpoint, publication and model-cache checks on an exFAT image mounted through exfat-fuse.

## 0.4.0 - 2026-09-27
- Recover damaged audio with `--decode-errors tolerant|strict` (tolerant by default), a configurable 30-second input-duration budget and an independent no-progress guard instead of packet-count limits. Retain checkpoints on failure and explain explicit restart/repair options. Record audio decoding policy version 1 and its settings in resume identity without migrating older checkpoints.
- Recover accumulated negative clock drift and backward timestamp jumps in all containers without dropping decoded samples; warn once and invalidate stale duration estimates. Audio timeline version 3 supersedes version 2 without checkpoint migration.
- Skip Whisper and VAD for entirely zero-valued windows while retaining their timeline and resumable progress; chunking version 4 does not migrate previous checkpoints.
- Tolerate codec timestamp jitter without resampler resets, restore MP3 gapless trimming and MPEG-TS duration estimates, and add `--timestamp-gaps auto|preserve` for large transport gaps. Timeline version 2 intentionally rejects older saved progress without migration.
- Identify development builds as 0.4.0-dev with source revision/dirty metadata, including Docker provenance; report FFmpeg versions, CPU plugin and whisper system features in doctor.
- Align audio to the container timeline, preserving delayed starts and packet gaps with bounded silence buffers; handle preroll, overlaps and transport timestamp resets without stale sample-rate extrapolation.
- Use checkpoint schema 3 with separate compatibility and immutable publication metadata; allow CPU-count and equivalent model-spelling changes on resume, report incompatible fields and preserve older journals without migration.
- Recover private temporary files left by a kill during the first manifest/chunk write while rejecting unsafe or unknown contents; add frozen schema-3, timestamp and crash regressions.
- Discover TS/MTS/M2TS/MKA/AIF/3GP/ASF files, validate explicit audio streams before model preparation, normalize SRT whitespace and reduce FFmpeg logging noise.
- Shorten the README, document timeline/resume contracts and window-tail overhead separately, and cache CPU/CUDA packaging layers independently without adding inference to package checks.
- Simplify checkpoint recovery and descriptor ownership, isolate streaming formatters and read-only job planning, and strengthen failure/compatibility tests; run CI lint checks once and make the ccache clean-rebuild check opt-in.
- Organize source code by responsibility and tests by execution scope, isolate the whisper session and checkpoint rendering, and check standalone headers; preserve CLI behavior, output formats and checkpoint compatibility.
- Separate model-free archive packaging from native smoke tests using the same artifact; restrict slow non-AVX2 QEMU inference to an explicit manual workflow and keep only a quick loader check on pull requests.
- Select CPU threads from physical cores, process affinity and visible hierarchical cgroup quotas; report the effective count in diagnostics and retain explicit overrides.
- Build portable archives with dynamically selected CPU variants, including baseline x86_64 without AVX2; package plugin dependencies and test missing-plugin diagnostics and non-AVX2 inference.
- Resume interrupted HTTPS model downloads with byte progress, validated ranges, private hash-specific partial files and final SHA-256 checks; add pinned turbo Q5_0/Q8_0 model entries without changing defaults.
- Default TXT/JSON text to pause- and sentence-based paragraphs, with `--text-layout single-line` compatibility and a configurable paragraph pause; preserve raw recognition segments.
- Add escaped WebVTT captions and include VTT in `--format all`, complete-set checks, mappings and interrupted publication recovery; detect old three-format saved progress rather than silently starting again.
- Retain uncommitted audio tails across bounded windows, detect automatic language per window, select the best audio stream or an explicit `--audio-stream`, and show file-level progress/ETA.
- Use checkpoint schema 2 and chunking version 3; previous unreleased progress requires the previous binary or an explicit restart. JSON schema 2 records per-segment languages and a multilingual language summary.
- Add quiet/verbose reporting, rate-limited terminal/log progress and immediate termination on a repeated interrupt; cache CI compilation separately for each toolchain and sanitizer configuration.
- Require FFmpeg 5.1+, derive build metadata and artifact names from CMake, pin JSON independently, and enforce project warnings in CI while allowing explicit local CPU optimizations.
- Default Docker runtime execution to UID/GID 10001; host bind mounts should still use the host user's explicit `--user`.
- Prefer model catalog names over same-named local directories, reuse verified model hashes and wait cancellably for concurrent downloads without locking ready weights.
- Avoid saved-progress errors after early failures; safely restart verified zero-progress journals while preserving committed windows and unknown contents.
- Index paths and file identities instead of comparing every output against every input; consistently reject output and mapping symlinks.
- Handle mid-stream audio rate, format and channel layout changes without losing resampler tails or corrupting PCM.
- Separate the 200 ms window-boundary pause threshold (`--chunk-min-silence-ms`) from inference VAD.
- Decode audio incrementally and run whisper.cpp in bounded windows with `--chunk-seconds` (120 seconds by default, range 30-600), preferring VAD pauses near window boundaries.
- Automatically checkpoint completed windows and add explicit `--resume` with source/model/settings validation; re-decode the completed prefix without repeating inference.
- Stream TXT/SRT/JSON from saved fragments instead of accumulating the full transcript in RAM, and recover interrupted multi-format publication using verified output hashes.
- Include window size and chunking version in run metadata.
- Add bounded-memory, checkpoint integrity, signal recovery and write-failure regressions, plus short offline windowed-inference and resume checks.

Windowing limits recording-length-dependent buffers, not total model RAM or VRAM. JSON uses schema 2; checkpoints use schema 3 with decoding policy 1, timeline 3 and chunking 4. Earlier development checkpoints are not migrated. See the [release notes](docs/releases/0.4.0.md) for upgrade guidance and known limitations.

## 0.3.0 - 2026-09-26

- Replace Python/faster-whisper with one C++17/whisper.cpp implementation.
- Preserve sorted sequential jobs, stable numbered maps and protected atomic outputs.
- Add pinned GGML/VAD catalog, verified downloads, local/offline cache and diagnostics.
- Deliver CPU/CUDA archives with shared libraries; Docker packages the same CLI.
- Add CTest, GCC/Clang, wrapper sanitizers and short offline inference checks.
- Remove Python tooling, prototype duplication and root shell launchers.

Python 0.2 is frozen in `v0.2.0`, not maintained in parallel. Jobs, batching,
compute-type, prompts and word timestamps are not in 0.3. Full-audio decoding
remains; chunking/resume are deferred. See [migration](docs/migration.md).
Checks and remaining limits: [release notes](docs/releases/0.3.0.md).

## 0.2.0 - 2026-09-26

- Unified `transcribe`, `models list/download`, `doctor` and version commands.
- Directory sorting, numbered mappings, skip policy and CPU workers in the CLI.
- Process-death detection, SIGINT/SIGTERM cleanup and permission-aware outputs.
- Versioned JSON with run metadata; existing transcript fields retained.
- Thin Docker launchers, package tests, offline inference smoke test and CI.
- Experimental C++/whisper.cpp CLI with CPU/CUDA build recipes.
- MIT license, English/Russian documentation and contribution/security policy.

Migration: numbered mappings now use JSON rather than TSV. Existing numbered
files without their new mapping are not trusted; choose a new prefix/output
directory. `MAP_FILE` is no longer accepted. A directory means all recognized
media extensions, not only MP4. Native archives are experimental and have a
smaller CLI than the Python implementation.
