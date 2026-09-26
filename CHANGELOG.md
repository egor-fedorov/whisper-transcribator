# Changelog

## Unreleased
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

These changes are available on `main`, not in the published 0.3.0 archives. Windowing limits recording-length-dependent buffers, not total model RAM or VRAM.

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
