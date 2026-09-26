# whisper-transcribator

A local C++17 CLI that turns an audio stream in a media file into TXT, SRT, WebVTT or JSON.
Inference uses whisper.cpp; decoding and resampling use FFmpeg libraries.
No Python runtime, external ffmpeg executable or web service is required.

This README describes `main`, including unreleased streaming/resume, paragraphs,
WebVTT, resumable downloads and portable CPU backends. The published 0.3.0 archives
do not include these additions; build from source to try them. See the
[changelog](CHANGELOG.md) for the boundary between released and unreleased features.

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
Archives target Linux x86_64 and Ubuntu 22.04+ (glibc 2.35+).
Published 0.3.0 archives require a Haswell-class AVX2/FMA/F16C/BMI2 CPU.
Archives built from `main` instead select a compatible CPU plugin, including a
baseline x86_64 implementation without AVX2. Keep all bundled backend plugins;
they are loaded from the installed library directory, not the working directory.
These are not universal static binaries.
Alternatively, [build an archive](docs/releasing.md) locally.

```bash
./bin/whisper-transcribator lecture.mp4 -o lecture.txt --model small --language ru
./bin/whisper-transcribator lecture.m4a --output-dir transcripts --format all
./bin/whisper-transcribator --input-dir lectures --output-dir transcripts \
  --naming numbered --skip-existing --model large-v3 --device cuda
```

Files are processed **sequentially with one loaded model**. Directory discovery
is non-recursive and sorted by filename bytes, independent of locale; explicit
file arguments retain their order. FFmpeg selects the best audio stream; use
`--audio-stream N` to select an absolute container stream index. A video track
is neither required nor decoded. Archive codecs/containers are listed in
[packaging](packaging/Dockerfile); source builds use the installed FFmpeg.

Defaults: `small`, Russian, TXT, device `auto`, beam size 5, VAD enabled,
`--cpu-threads 0` (automatic). On Linux, auto counts physical cores within process
affinity and caps that count by visible cgroup v1/v2 CPU quotas, including parent
quotas. Missing topology falls back to allowed logical CPUs; the result is at
least one. Fractional quotas round down. `doctor --json` reports the count.
Set `--cpu-threads 4` for an explicit override, which is not clamped automatically.
`--language auto` detects language separately in each recognition window.
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
`large-v3-turbo`, `large-v3-turbo-q5_0` (574 MB), and `large-v3-turbo-q8_0`
(874 MB); `large` and `turbo` retain their unquantized aliases. File sizes are
not total inference RAM/VRAM requirements. Silero VAD is a separate,
small model. **No weights are embedded in an image or archive.** Only the selected
model and, when enabled, VAD weights are downloaded on first use.
Catalog names take precedence over files or directories in the current directory.
Use an explicit path such as `./small` to select a local file with a catalog name.

Cache precedence: `--download-root`, `WHISPER_DOWNLOAD_ROOT`, then
`${XDG_CACHE_HOME:-$HOME/.cache}/whisper-transcribator/models`.
`WHISPER_MODEL` overrides the default model. Downloads use pinned revisions,
TLS verification, size/SHA-256 checks, an exclusive lock and atomic publication.
Corrupt cached files are reported, not silently replaced. Remove a corrupt file
explicitly and retry. Missing-model downloads wait for a busy cache lock and can
be cancelled; verified existing weights do not take the download lock. Each
prepared model is hashed once and its verified digest is reused for checkpoints.
`models list --json` verifies existing catalog weights without downloading.
Interrupted transfers retain a private, hash-specific `.part` file and resume
automatically on retry. Download progress includes existing bytes. A server that
ignores Range causes a safe restart, not an append of duplicate data. The final
file is published only after full size/SHA-256 verification; corrupt partial
downloads are removed, while corrupt published weights are preserved for explicit
repair. `models list` reports `partial` for an unfinished download; partial files
are not usable models in offline mode.

`--local-files-only` forbids downloads, including VAD. Local models must be
nonempty GGML files, not faster-whisper/CTranslate2 directories or GGUF files.
Their contents are not checked against the catalog. For a custom CA bundle set
`SSL_CERT_FILE`. Do not disable TLS verification.

## Outputs And Errors

Status and progress go to stderr; command JSON stays on stdout. Use `--quiet`
for warnings/errors only, or `--verbose` to include backend and checkpoint
diagnostics. Terminal progress is updated in place; redirected progress is
rate-limited. A first SIGINT/SIGTERM requests a graceful stop; a second exits
immediately, leaving the last committed checkpoint recoverable.

TXT defaults to readable paragraphs: a pause of at least 2000 ms, a language
change, or a sentence-ending segment after about 600 Unicode characters starts
a new paragraph. This is a formatting heuristic, not an LLM rewrite or summary.
Segments otherwise join with spaces, including across recognition windows; lines
are never wrapped to a fixed width. Use `--text-layout single-line` for the old
layout or `--paragraph-pause-ms N` to change the pause threshold. JSON `text`
uses the same layout; individual segment text/timestamps remain unchanged.
SRT preserves segment timestamps. JSON schema version 2 includes segment/run metadata;
backend-specific unavailable metrics are `null`, not fabricated values.
Each segment records its window's language; `languages` lists observed languages
in encounter order and top-level `language` is `null` for multilingual recordings.
`--format vtt` produces WebVTT captions. `--format all` requires `--output-dir`
and writes TXT, SRT, VTT and JSON from the same recognition results.

Existing outputs cause an error unless `--overwrite` is set. `--skip-existing`
skips only when every requested output is a nonempty regular file; it is not a
content/model validation. Incomplete sets require an explicit `--overwrite`, or
`--resume` with a checkpoint that verifies the already published files.
Inputs, including hardlink/symlink aliases, are protected from output collisions.
Output files and numbered mappings must not be symlinks, including with
`--overwrite` or `--skip-existing`; symlinks to directories remain supported.
Each output is atomically published; the four-format set is not a transaction.
On interruption no partial file is published, but already completed files remain.

## Long Recordings And Resume (Unreleased)

Audio is decoded incrementally and recognized in windows of at most 120 seconds.
Use `--chunk-seconds 30` to reduce the window, or another integer between 30 and
600. With VAD enabled, the last suitable pause in the final quarter of a window
is preferred as its boundary; `--chunk-min-silence-ms` controls the minimum pause
(200 ms by default). `--vad-min-silence-ms` independently controls inference VAD
(2000 ms by default).
The full bounded window is recognized, but only complete segments before the
chosen boundary and a two-second end guard are committed. The uncommitted audio
tail is retained for the next window, including with `--no-vad`. At EOF the
remaining segments are committed. If no positive non-overlapping segment boundary
exists, a full window is committed with a warning; timestamp estimates cannot
guarantee perfect boundary words. Silence still advances the checkpoint.
`--language auto` detects language independently in every window, not every word.
An explicit language remains fixed. Audio defaults to FFmpeg's best audio stream;
`--audio-stream N` selects an absolute container stream index, not an audio ordinal.

PCM, VAD and recognition buffers cover only the current window. TXT/SRT/VTT/JSON
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
Progress reports use file-level audio seconds and an estimated remaining time
after a window commits. Unknown or exceeded duration estimates suppress the ETA.
Reading streams, hashing, model loading, prefix decoding and publication have
separate status messages; only successful publication reports completion.

Failed attempts before the first committed window do not count as saved progress.
Validated empty checkpoints are safely restarted, including after replacing a
bad input. Unknown or damaged checkpoint contents are never silently removed.
Saved progress is never silently reused. Without `--resume`, its presence is an
error unless `--overwrite` explicitly starts over. `--resume` starts a new job
when there is no checkpoint; incompatible or damaged checkpoints are errors.
`--resume --overwrite` permits replacing existing outputs but does not bypass
checkpoint validation. Plain `--overwrite` discards this job's saved progress.
Checkpoint schema 2 / chunking version 3 do not migrate earlier unreleased
checkpoints. Finish those jobs with their original binary, or explicitly restart
with `--overwrite` without `--resume`; incompatible progress is never silently reused.
An old three-format `all` checkpoint is also detected: finish it with the old
binary, choose a new output directory, or explicitly move its checkpoint aside.
Existing three-format outputs are no longer a complete `all` set; select a
single format to retain its skip behavior, or explicitly overwrite the full set.
If publication of multiple formats was interrupted, verified completed formats
are retained and missing ones are reconstructed without inference. Changed output
files require explicit overwrite permission. Existing 0.3.0 partial runs have no
compatible checkpoints. `--skip-existing` retains its completeness-only semantics.

Mid-stream changes in sample rate, channel layout and sample format are supported.
Completed transcripts are unaffected by checkpoint schema changes.

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
The runtime image defaults to UID/GID 10001. For host bind mounts, use your own
UID/GID as below and create writable directories first. Existing root-owned
checkpoints or model volumes are not automatically re-owned; explicitly repair
their ownership on the host before running as your user.

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
