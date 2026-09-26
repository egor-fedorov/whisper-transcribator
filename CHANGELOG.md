# Changelog

## 0.3.0 - Local Candidate

- Replace Python/faster-whisper with one C++17/whisper.cpp implementation.
- Preserve sorted sequential jobs, stable numbered maps and protected atomic outputs.
- Add pinned GGML/VAD catalog, verified downloads, local/offline cache and diagnostics.
- Deliver CPU/CUDA archives with shared libraries; Docker packages the same CLI.
- Add CTest, GCC/Clang, wrapper sanitizers and short offline inference checks.
- Remove Python tooling, prototype duplication and root shell launchers.

Python 0.2 is frozen in `v0.2.0`, not maintained in parallel. Jobs, batching,
compute-type, prompts and word timestamps are not in 0.3. Full-audio decoding
remains; chunking/resume are deferred. See [migration](docs/migration.md).
Local checks and remaining limits: [verification notes](docs/releases/0.3.0.md).

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
