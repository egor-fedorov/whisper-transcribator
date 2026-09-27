# Contributing

Keep the product a small local CLI with one whisper.cpp backend. Do not add a
second inference implementation or revive the historical Python pipeline.

## Build And Test

On Ubuntu 24.04 install `cmake ninja-build g++ git pkg-config libavformat-dev
libavcodec-dev libswresample-dev libcurl4-openssl-dev libssl-dev`.
Upstream sources are fetched at pinned revisions with SHA-256 checks.

```bash
cmake --preset cpu
cmake --build --preset cpu -j4
ctest --preset cpu
find src tests -type f \( -name '*.cpp' -o -name '*.hpp' \) -exec clang-format-18 --dry-run --Werror {} +
find tests packaging -type f -name '*.sh' -exec shellcheck {} +
bash tests/packaging/release.sh
cmake --preset asan
cmake --build --preset asan -j4
ctest --preset asan
```

On macOS install the Xcode Command Line Tools and `brew install cmake ninja pkgconf ffmpeg ffmpeg-full openssl@3 jq`, then use the same CTest commands. CMake finds Homebrew's keg-only OpenSSL itself, and tests generate media with `ffmpeg-full` because Homebrew's `ffmpeg` has no libvorbis encoder; the application links `ffmpeg`. The test scripts run under the system Bash 3.2: do not expand empty arrays under `set -u` or rely on GNU-only tools. `tests/smoke/model-hash.sh` needs `strace` and runs on Linux only.

Run a focused model-free group with CTest labels; the unfiltered command above still runs all required checks:

```bash
ctest --preset cpu -L unit
ctest --preset cpu -L integration
ctest --preset cpu -L resource
ctest --preset cpu -j4 --schedule-random
```

Integration checks use synthetic media, fake inference or a loopback TLS server, not downloaded models. Resource checks retain their existing timeouts; the RSS comparison is excluded from sanitizer builds. Each C++ scenario gets a unique temporary directory, removed on success and reported/preserved on failure. Shared helpers restore scoped environment, current-directory and stream changes.

Assertions report the calling file and line. Negative tests must name the expected error reason; use `rejects<Error>(action, reason)` when a specific exception type is part of the contract. Assertions inside an action are not accepted as ordinary runtime failures. Frozen synthetic checkpoint/output compatibility fixtures live in `tests/fixtures/`; do not regenerate expectations with the code under test.

ASan/UBSan instrument this project's code, not an audit of upstream dependencies.
LeakSanitizer cannot run under ptrace-based sandboxes; run these checks normally,
rather than disabling leak detection. GCC and Clang are checked in CI with
`-DWT_WERROR=ON` for project code and tests, not dependencies; GCC also runs natively
on Linux aarch64 and Apple Clang on macOS arm64. Keep code free of architecture-specific
assumptions and keep Linux- or macOS-only calls in small platform branches. FFmpeg 5.1 is
the minimum supported version and is checked separately. Install `ffmpeg` and `jq`
for synthetic media integration tests (skipped when the `ffmpeg` command's libavcodec differs
from the linked one, as in archive builds), including Vorbis/MP3 at 44.1/48 kHz,
MPEG-TS format transitions, clock drift and timestamp jumps, compared against independent
FFmpeg PCM references. The test executable needs `libvorbis` and `libmp3lame`
encoders and the `noise` bitstream filter. Damaged MP3/AAC checks include consecutive
bad packets, zeroed 4/16 KiB M4A blocks and one second of missing TS packets.
They compare PCM, exercise time-based budgets and strict/restart behavior, and verify
byte-identical resume using fake inference that hashes decoded windows. These are
test tools only, not runtime dependencies. `-DGGML_NATIVE=ON` is supported for local
builds; portable packaging explicitly keeps it off.

The optional inference check uses only a public 11-second JFK recording:

```bash
bash tests/smoke/prepare-smoke.sh .build/cpu/whisper-transcribator .build/fixtures
bash tests/smoke/smoke.sh .build/cpu/whisper-transcribator .build/fixtures/jfk.wav \
  .build/fixtures/ggml-tiny.bin .build/fixtures/ggml-silero-v6.2.0.bin .build/smoke
bash tests/smoke/streaming-smoke.sh .build/cpu/whisper-transcribator \
  .build/cpu/tests/wt-audio-fixture .build/fixtures/jfk.wav \
  .build/fixtures/ggml-tiny.bin .build/fixtures/ggml-silero-v6.2.0.bin .build/streaming-smoke
```

On Linux with `exfatprogs`, `exfat-fuse` and root or passwordless sudo, or on macOS without extra privileges through `hdiutil`, repeat publication, checkpoint and model-cache checks on exFAT disk images after preparing the fixtures above. The default `integration/permissionless` test simulates such a filesystem instead:

```bash
bash tests/integration/exfat.sh .build/cpu .build/fixtures .build/exfat
```

Use a fresh smoke output directory for each run. Fixtures/weights are downloaded
only during preparation. CI then disables networking for actual inference.
Do not add private recordings, transcripts or model weights. Never run full
lectures for routine checks or benchmarks.

The streaming smoke generates a 41-second fixture by repeating the public sample
and inserting silence. It checks VAD boundaries, actual SIGTERM/resume and
inference bypass on fully zero-valued windows with and without VAD. Pass
`cuda` as the final argument to either smoke script for a trusted GPU check.
Model-free pipeline tests cover SIGINT/SIGTERM/SIGKILL, checkpoint corruption,
incompatible settings, disk-write failures and interrupted multi-format output.
The non-sanitized memory test decodes synthetic 120/3600-second WAV files with
fake inference and large fake text, checking that peak RSS grows by no more than
32 MiB. It does not transcribe hour-long recordings or measure model quality.

## Layout

- `src/app/`: CLI parsing, diagnostics and sequential job orchestration.
- `src/audio/`: FFmpeg decoding, audio-stream selection, resampling and bounded container-timeline alignment. Timestamp placement and decode-error budgets are independently testable policies; public headers do not expose FFmpeg headers.
- `src/inference/`: whisper.cpp device discovery, logging and a lazily initialized RAII session shared across files.
- `src/models/`: pinned catalog, model cache, locking and HTTPS transfers.
- `src/transcript/`: value types, job planning, windowing, metadata, checkpoint integrity and output rendering.
- `src/support/`: plain options, filesystem/hash operations, cancellation, CPU limits and reporting.
- `cmake/`: pinned dependencies and generated metadata.
- `tests/unit/` and `tests/integration/`: focused component checks and cross-component recovery, audio, CLI and process scenarios.
- `tests/resource/`: synthetic bounded-memory and planning-scale checks.
- `tests/fixtures/`: reviewed synthetic checkpoint and output snapshots for cross-version compatibility.
- `tests/smoke/`: public fixture preparation and short real-inference checks, separate from default CTest.
- `tests/packaging/`: archive, release-helper and portable-loader checks; QEMU inference remains explicitly opt-in.
- `tests/support/`: assertions, scoped fixtures, fake audio/inference and the separate `wt-audio-fixture` generator.
- `packaging/`: archive/container recipes and dependency collection.
- `docs/`: usage, migration, distribution and design decisions.
- `.build/`: all generated build, test and release artifacts.

Keep includes specific to their owner; do not restore an umbrella application header. Application code coordinates the modules. Audio and inference adapters contain upstream API calls; model-free transcript logic and support code must not include whisper, FFmpeg or CLI11 headers. The journal owns durable publication and receives a renderer callback. `transcript/publication.cpp` adapts the journal to a repeatable streaming segment source; format serializers and paragraph layout have no filesystem or journal dependency. Compatibility metadata excludes execution-only fields; original output metadata is frozen in the journal for byte-stable resumed publication. Changes in timeline, chunking or serialization behavior require an explicit algorithm-version and fixture review, not merely a new application version.

Job planning validates inputs, output collisions and numbered mappings before preparing directories or publishing a new mapping. Journal recovery validates records against explicit candidate states without mutating a live journal. `support/fd.hpp` owns descriptor lifetimes only; checkpoint locks still fail immediately, while model-download locks wait cancellably.

Production build targets remain `wt_core`, `wt_engine` and the CLI. Source lists are explicit; register new headers in the standalone header compilation list in `tests/CMakeLists.txt`. Keep model-free checks in CTest and real inference in smoke scripts. Avoid adding a library or interface for every directory.

Add regressions for output safety, error codes and interrupted operations.
Keep manual GPU tests on a trusted machine; never execute untrusted PR code on
a personal GPU runner. Logical commits should separate behavior, tests,
packaging and documentation. Do not publish releases automatically.

Open a PR against `main`; required CI checks must pass on an up-to-date branch.
Force pushes and deletion of `main` are disabled. Use squash merges for focused
changes. Release preparation is documented in [releasing](docs/releasing.md).

Normal CI builds each compiler configuration once and runs formatting, ShellCheck and release-helper checks only in the GCC job. Ccache statistics remain visible; use the manual workflow's `verify_ccache` input to exercise the additional clean rebuild/cache-hit check. Required test, packaging and smoke jobs are unchanged.

Write each release-note paragraph or list item on a single physical line. Do not manually wrap prose, indent continuation lines, or add blank lines after headings; let the renderer wrap text to the reader's window.
