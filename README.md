# whisper-transcribator

A local C++17 CLI that turns an audio stream in a media file into TXT, SRT or JSON.
Inference uses whisper.cpp; decoding and resampling use FFmpeg libraries.
No Python runtime, external ffmpeg executable or web service is required.

[Migration from 0.2](docs/migration.md) ·
[Build and release](docs/releasing.md)

## Run

Version 0.3 uses one native backend. CPU and CUDA archives contain the executable
**and its shared libraries**: keep `bin/`, `lib/` and `share/` together.
Archives target Linux x86_64, Ubuntu 22.04+ (glibc 2.35+), and a Haswell-class
AVX2/FMA/F16C/BMI2 CPU. They are not universal static binaries.
No public release upload is assumed; [build an archive](docs/releasing.md) locally.

```bash
./bin/whisper-transcribator lecture.mp4 -o lecture.txt --model small --language ru
./bin/whisper-transcribator lecture.m4a --output-dir transcripts --format all
./bin/whisper-transcribator --input-dir lectures --output-dir transcripts \
  --naming numbered --skip-existing --model large-v3 --device cuda
```

Files are processed **sequentially with one loaded model**. Directory discovery
is non-recursive and sorted by filename bytes, independent of locale; explicit
file arguments retain their order. The first audio stream is used. A video track
is neither required nor decoded. Archive codecs/containers are listed in
[packaging](packaging/Dockerfile); source builds use the installed FFmpeg.

Defaults: `small`, Russian, TXT, device `auto`, beam size 5, VAD enabled,
`--cpu-threads 0` (whisper.cpp's default, not all cores). Set
`--cpu-threads 4` explicitly when needed. `--language auto` detects language.
`--device cuda` errors when CUDA is unavailable; `auto` reports its CPU fallback.

## Models

```bash
./bin/whisper-transcribator models list
./bin/whisper-transcribator models download large-v3
./bin/whisper-transcribator models download silero-v6.2.0
./bin/whisper-transcribator lecture.wav --model large-v3 --local-files-only
./bin/whisper-transcribator lecture.wav --model /weights/ggml-small.bin --no-vad
```

The catalog contains `tiny`, `base`, `small`, `medium`, `large-v3` and
`large-v3-turbo`; `large` and `turbo` are aliases. Silero VAD is a separate,
small model. **No weights are embedded in an image or archive.** Only the selected
model and, when enabled, VAD weights are downloaded on first use.

Cache precedence: `--download-root`, `WHISPER_DOWNLOAD_ROOT`, then
`${XDG_CACHE_HOME:-$HOME/.cache}/whisper-transcribator/models`.
`WHISPER_MODEL` overrides the default model. Downloads use pinned revisions,
TLS verification, size/SHA-256 checks, an exclusive lock and atomic publication.
Corrupt cached files are reported, not silently replaced. Remove a corrupt file
explicitly and retry; a busy cache lock means another downloader is still running.
`models list --json` verifies existing catalog weights without downloading.

`--local-files-only` forbids downloads, including VAD. Local models must be
nonempty GGML files, not faster-whisper/CTranslate2 directories or GGUF files.
Their contents are not checked against the catalog. For a custom CA bundle set
`SSL_CERT_FILE`. Do not disable TLS verification.

## Outputs And Errors

TXT joins recognition segments with spaces instead of adding arbitrary line breaks.
SRT preserves segment timestamps. JSON schema version 1 includes segment/run metadata;
backend-specific unavailable metrics are `null`, not fabricated values.
`--format all` requires `--output-dir` and uses a single inference pass.

Existing outputs cause an error unless `--overwrite` is set. `--skip-existing`
skips only when every requested output is a nonempty regular file; it is not a
content/model validation. Incomplete sets require an explicit `--overwrite`.
Inputs, including hardlink/symlink aliases, are protected from output collisions.
Each output is atomically published; the three-format set is not a transaction.
On interruption no partial file is published, but already completed files remain.

Numbered names are `result_001.txt`, etc. `result_files.json` records the source
mapping. Changed input lists or orphaned numbered outputs are rejected even with
`--overwrite`; use a fresh `--prefix` or output directory. Do not delete the mapping
to reuse its old results.

Exit codes: 0 success (including nothing to do), 1 runtime failure, 2 usage error,
130 SIGINT, 143 SIGTERM. `--continue-on-error` attempts later files but still exits
1 if any failed. Logs and progress go to stderr. `doctor --device cuda --json`
checks backend availability without downloading weights or running inference.

## Optional Docker

Docker packages the **same native CLI**, not a Python backend.

```bash
docker build -f packaging/Dockerfile --target runtime -t whisper-transcribator:cpu .
mkdir -p transcripts models
docker run --rm --user "$(id -u):$(id -g)" \
  -v "$PWD/lectures:/input:ro" -v "$PWD/transcripts:/output" \
  -v "$PWD/models:/models" whisper-transcribator:cpu \
  --input-dir /input --output-dir /output --naming numbered --model small
```

After downloading weights, add `--network none` to Docker and
`--local-files-only` to the CLI; mount models read-only. CUDA needs a CUDA build,
an NVIDIA driver and NVIDIA Container Toolkit when using Docker.
See [CUDA build and verification](docs/releasing.md#cuda).
Do not run untrusted media as root or mount unrelated directories.

## Scope

There is no server, LLM summarizer or second backend. Transcripts are not lecture
summaries. Version 0.3 intentionally does not support jobs, batching, compute-type,
prompts, word timestamps, chunking or resume; unknown options are errors.
Python 0.2 is frozen in `v0.2.0`, not maintained alongside C++.

Audio still decodes in full; VAD can allocate additional buffers. RAM is **not**
bounded independently of recording length. Choose a smaller model if necessary.
Bounded-memory decoding and resumable jobs are the next separate design task.

[Backend decision](docs/backend-selection.md) · [Contributing](CONTRIBUTING.md) ·
[Security](SECURITY.md) · [Dependency notices](docs/third-party.md)
