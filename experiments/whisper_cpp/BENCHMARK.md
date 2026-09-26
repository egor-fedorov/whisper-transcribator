# Local Backend Comparison

Date: 2026-09-26. This is a small private-corpus experiment, not a general Whisper
leaderboard. An internal manual quality check is complete (see below).
Raw recordings and transcripts are not included in the repository.

## Setup

- Intel Core i5-12400F, 6 physical cores / 12 logical CPUs, approximately 32 GB RAM.
- NVIDIA RTX 4060 Ti 16 GB, driver 615.71.09; Linux x86_64 workstation.
- Python 3.13, faster-whisper 1.2.1, CTranslate2 4.6.0, PyAV 18.1.0; pinned Docker
  dependencies in requirements.lock, CUDA cuBLAS 12.8.4.1 and cuDNN 9.7.1.26.
- Native whisper.cpp commit `927cfce34f31707e17f2bff35c349632fb9e2c3a`
  (v1.9.4 tag, runtime reports 1.9.4-dev), FFmpeg 8.0.1, Release build,
  GGML_NATIVE=OFF. CPU archive built on Ubuntu 22.04; CUDA archive built on
  Ubuntu 22.04 / CUDA 12.8.1 with architecture 89 for this GPU.
- An earlier local CUDA 13.4 build was smoke-tested but is not used for comparison.

Four 120-second clips (480 seconds total) come from three lecture recordings.
Offsets are 600 and 9810 seconds in source 1, 5400 in source 2, 2100 in source 3.
Preparation decodes them to the same 16 kHz mono PCM WAV; hashes are kept in the
private corpus manifest. The full-file test uses source 1 (10768.64 seconds).

Both backends use Russian, beam size 5, four CPU threads, no VAD, one model load
for the input list and all three output formats. CPU: small, CTranslate2 float32
versus unquantized GGML with mixed-precision weights. GPU: large-v3,
CTranslate2 float16 versus unquantized GGML; native flash attention is enabled.
Backend defaults/decoding and numeric precision are **not identical**. In
particular this is not a benchmark of the production CPU int8 default or batching.
These numbers compare the current configurations, not language overhead or
matched decoders. See [backend selection](../../docs/backend-selection.md) for
the remaining feature and distribution considerations.

Models are cached before timing. CTranslate2 snapshots: small
`536b0662742c02347bc0e980a01041f333bce120`, large-v3
`edaa852ec7e145841d8ffdb056a99866b5f0a478`.
GGML SHA-256: small `1be3a9b2063867b937e64e2ec7483364a79917e157fa98c5d94b5c1fffea987b`,
large-v3 `64d182b440b98d5203c4f9bd541544d84c605196c4f7b845dfa11fb23594d1e2`.

## Measurement

`tools/benchmark.py` alternates Python/native, three short-corpus runs per backend,
and one full-file run per backend. Python is measured inside Docker, so RSS is
not the Docker client. Wall time includes process/model initialization, decoding,
inference and output. RSS is the largest child-process peak, not container/page
cache usage. GPU VRAM is sampled every 0.5 seconds for newly appearing compute
processes; short peaks can be missed. Do not run other GPU jobs during GPU tests.
CPU GPU-memory readings are not meaningful and are not reported.
These no-VAD memory figures must not be presented as limits for the production
VAD-enabled path, which can have additional full-recording allocations.

This is an interactive workstation, not an isolated benchmark host. Initial CPU
passes overlapped archive packaging and brief smoke checks; treat small timing
differences cautiously. Results below will distinguish CPU, GPU and full-file runs.

## Results

Short corpus results use median wall time (range) and maximum RSS across three runs.

| Device/model | Backend | Wall seconds | Peak RSS MiB | Peak VRAM MiB |
| --- | --- | ---: | ---: | ---: |
| CPU/small | faster-whisper float32 | 82.08 (81.58-82.58) | 1635.1 | n/a |
| CPU/small | whisper.cpp | 98.38 (98.17-98.71) | 872.4 | n/a |
| CUDA/large-v3 | faster-whisper float16 | 35.76 (34.62-36.64) | 3379.3 | 5292 |
| CUDA/large-v3 | whisper.cpp | 29.91 (29.90-30.96) | 663.8 | 4122 |

On these clips native GPU wall time is about 16% lower, with substantially lower
host RSS. Native CPU float32/mixed-precision wall time is about 20% higher. These
are backend/configuration results, not evidence that Python orchestration is slow.
Full-file results (one run each, 10768.64 seconds of audio):

| Backend | Wall seconds | Peak RSS MiB | Peak VRAM MiB |
| --- | ---: | ---: | ---: |
| faster-whisper float16 | 890.35 | 10399.1 | 5452 |
| whisper.cpp | 608.11 | 2211.0 | 4122 |

Native finished in 10:08 versus 14:50. Its host RSS peak was about 2.16 GiB versus
10.16 GiB. This supports further native investigation for memory-constrained
machines, but does not establish equivalent output quality or bounded memory.
See ignored `benchmark-results/comparison/` for per-run metrics and logs.
Full-file tests are optional memory/stability investigations, not a prerequisite
for routine speed comparisons. Use short clips by default and agree the time
budget before running a long recording.

## Quality Check

An internal manual comparison of faster-whisper and whisper.cpp using large-v3
on GPU was completed on three 30-second lecture excerpts. The project owner
reviewed both outputs and confirmed that all transcriptions were correct and
practically identical on these samples. Audio and transcripts are not included
in the repository.

## Decision

Keep Python/faster-whisper as the supported CLI and Docker distribution for now.
The prototype has a meaningful measured GPU/RAM advantage on this machine, but
does not yet have feature parity. Its CPU result also
does not justify a blanket rewrite. A Rust frontend would not by itself change
the inference engine or these memory characteristics.

The internal quality check is complete. Next: assess native feature parity and
distribution before selecting the supported backend. Chunking/resume remains a
separate project for either backend;
neither current implementation guarantees RAM independent of recording length.
Do not add automatic full-lecture benchmarks to CI or routine development.

## Packaging

Local archives, excluding model weights:

| Archive | Compressed bytes | SHA-256 |
| --- | ---: | --- |
| CPU | 24274910 | `2e81502f0edb70802523955b45d2ea4c3604a58c15bca8ab9e673a451106613f` |
| CUDA SM89 | 950238253 | `bc1624358971f70e40fe153c08ccd267dff0382738407444804f2d65dae6cff5` |

Both archives ran real tiny inference in clean Ubuntu 22.04 containers without
Python or system FFmpeg. The CUDA test used only host-driver injection, not an
installed toolkit. VAD was checked separately with ggml-silero-v6.2.0. Own code,
FFmpeg and whisper.cpp sources/notices, compiler runtime and CUDA/NCCL notices
are included. These checks do not establish portability to untested machines or
replace the distribution review in THIRD_PARTY.md.
