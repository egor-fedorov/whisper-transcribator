# whisper-transcribator

A local C++17 CLI that turns an audio stream in a media file into TXT, SRT or JSON.
Inference uses whisper.cpp; decoding and resampling use FFmpeg libraries.
No Python runtime, external ffmpeg executable or web service is required.

Streaming transcription and `--resume` on `main` are unreleased. The published
0.3.0 archives do not include these features yet; build from source to try them.

[Migration from 0.2](docs/migration.md) ·
[Build and release](docs/releasing.md)

## Install

Download a Linux x86_64 archive from [Releases](https://github.com/egor-fedorov/whisper-transcribator/releases).
Choose `cpu`, or `cuda` for an NVIDIA GPU with a compatible driver. CUDA runtime
libraries are included; an installed CUDA toolkit is not required.

```bash
version=0.3.0
flavor=cpu # or cuda
archive="whisper-transcribator-${version}-linux-x86_64-${flavor}.tar.gz"
url="https://github.com/egor-fedorov/whisper-transcribator/releases/download/v${version}"
curl -fLO "$url/$archive"
curl -fLO "$url/SHA256SUMS"
sha256sum --check --ignore-missing SHA256SUMS
mkdir -p whisper-transcribator
tar -xzf "$archive" -C whisper-transcribator
./whisper-transcribator/bin/whisper-transcribator doctor --device "$flavor" --json
```

## Run

Version 0.3 uses one native backend. CPU and CUDA archives contain the executable
**and its shared libraries**: keep `bin/`, `lib/` and `share/` together.
Archives target Linux x86_64, Ubuntu 22.04+ (glibc 2.35+), and a Haswell-class
AVX2/FMA/F16C/BMI2 CPU. They are not universal static binaries.
Alternatively, [build an archive](docs/releasing.md) locally.

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
content/model validation. Incomplete sets require an explicit `--overwrite`, or
`--resume` with a checkpoint that verifies the already published files.
Inputs, including hardlink/symlink aliases, are protected from output collisions.
Each output is atomically published; the three-format set is not a transaction.
On interruption no partial file is published, but already completed files remain.

## Long Recordings And Resume (Unreleased)

Audio is decoded incrementally and recognized in windows of at most 120 seconds.
Use `--chunk-seconds 30` to reduce the window, or another integer between 30 and
600. With VAD enabled, the last suitable pause in the final quarter of a window
is preferred as its boundary; `--chunk-min-silence-ms` controls the minimum pause
(200 ms by default). `--vad-min-silence-ms` independently controls inference VAD
(2000 ms by default).
Without such a pause, or with `--no-vad`, the maximum window is used. Windows
do not overlap. Recognition near a cut can differ from whole-file inference.
Language detection is locked after the first nonempty window for `--language auto`.

PCM, VAD and recognition buffers cover only the current window. TXT/SRT/JSON
are assembled incrementally from saved segments, not accumulated in RAM. Model
weights, backend workspaces and container metadata still consume memory: this is
not a hard RAM/VRAM cap, and a smaller window cannot make every model fit.

```bash
./bin/whisper-transcribator lecture.mp4 --output-dir transcripts --format all \
  --model large-v3 --device cuda --chunk-seconds 120
# After interruption, repeat the same options and add --resume:
./bin/whisper-transcribator lecture.mp4 --output-dir transcripts --format all \
  --model large-v3 --device cuda --chunk-seconds 120 --resume
```

Every finished window, including silence, is automatically checkpointed under
`transcripts/.whisper-transcribator/`. This private directory contains transcript
fragments and source paths, not audio or model weights. Keep it on persistent
storage; mounting `/output` in Docker also preserves checkpoints. Payloads are
removed after successful publication; small lock files remain to prevent races.
Allow disk space for the journal and staged output files. Disk errors fail the
job rather than reporting a completed transcript.

`--resume` validates source/model hashes, requested outputs, inference options
and backend versions. It then re-decodes and discards the completed audio prefix
without running inference on it. There is no full PCM cache or approximate seek.
Source hashing and prefix decoding can take time before recognition resumes.
At most the uncommitted window must be recognized again after a crash or kill.
Progress reports use absolute audio seconds; inference percentages are per window.

Saved progress is never silently reused. Without `--resume`, its presence is an
error unless `--overwrite` explicitly starts over. `--resume` starts a new job
when there is no checkpoint; incompatible or damaged checkpoints are errors.
`--resume --overwrite` permits replacing existing outputs but does not bypass
checkpoint validation. Plain `--overwrite` discards this job's saved progress.
If publication of multiple formats was interrupted, verified completed formats
are retained and missing ones are reconstructed without inference. Changed output
files require explicit overwrite permission. Existing 0.3.0 partial runs have no
compatible checkpoints. `--skip-existing` retains its completeness-only semantics.

Chunking version 2 also fixes mid-stream changes in decoded audio parameters.
Earlier unreleased checkpoints cannot be resumed: finish them with the previous
binary or explicitly restart with `--overwrite` without `--resume`. Completed
transcripts are unaffected.

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
prompts or word timestamps; unknown options are errors. Chunking and resume are
available on `main`, not in the published 0.3.0 archives.
Python 0.2 is frozen in `v0.2.0`, not maintained alongside C++.

No parallel file jobs, full audio cache, checkpoint migration between backend
versions, or absolute memory-budget flag is provided.

[Backend decision](docs/backend-selection.md) · [Contributing](CONTRIBUTING.md) ·
[Security](SECURITY.md) · [Dependency notices](docs/third-party.md)
