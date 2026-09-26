# Backend Selection

Status: 2026-09-26. The supported CLI remains Python/faster-whisper (`v0.2.0`);
whisper.cpp is a separate native prototype. New product features are paused until
the backend is selected. Unfinished Python chunk/resume work is preserved on
`wip/python-chunk-resume`, not included in main or the release.

## Findings

The [local comparison](../experiments/whisper_cpp/BENCHMARK.md) found a GPU speed
and host-memory advantage for the native prototype on the tested machine. Its
CPU result did not show the same speed advantage. The internal manual quality
check is complete: both implementations produced correct, practically identical
transcriptions on the reviewed short samples. Media and transcripts remain private.

This compares CTranslate2 with whisper.cpp/GGML, not Python arithmetic with C++:
faster-whisper already uses a native inference engine. A Rust frontend alone
would not change that engine or its memory characteristics.

## Remaining Decision

Choose the backend based on the measured resource use and the cost of preserving
the existing CLI workflow, not another transcription quality exercise:

- The native prototype still lacks sorted directory processing, numbered mappings,
  skip/overwrite controls and model download/cache management.
- Decide which additional features to retain: CPU jobs, int8, batching, prompts
  and word timestamps. Current capability differences are listed in the
  [prototype documentation](../experiments/whisper_cpp/README.md#parity).
- Native delivery is an archive with shared libraries, not a universal static
  executable. Target CPU/GPU architectures, codecs and dependency notices remain
  part of the distribution work. Neither current implementation has bounded-memory
  audio processing or resume.

Docker stays optional for either backend. Select the supported implementation
before resuming chunk/resume development.
