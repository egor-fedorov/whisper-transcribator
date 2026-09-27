# whisper-transcribator
A local C++17 CLI that turns the audio stream of a media file into TXT, SRT, WebVTT or JSON. One whisper.cpp backend, FFmpeg libraries for decoding, no Python runtime or external ffmpeg executable required.

`main` is **0.4.0-dev**. Windowing, resume, paragraphs, WebVTT and portable CPU plugins are not in the published **0.3.0** archives. See [Unreleased changes](CHANGELOG.md); build from source to use the examples with new features below.

## Quick Start
With a [source build](docs/releasing.md#cpu), transcribe one file or a directory:

```bash
cmake --preset cpu
cmake --build --preset cpu -j4
.build/cpu/whisper-transcribator lecture.mp4 -o lecture.txt --model small --language ru
.build/cpu/whisper-transcribator --input-dir lectures --output-dir transcripts \
  --naming numbered --skip-existing --model large-v3 --device cuda
```

Files run sequentially with one loaded model. A video track is neither required nor decoded. Directory discovery is non-recursive, case-insensitive by extension and sorted by filename bytes; explicit file arguments retain their order. Supported extensions include MP4/M4A, MKV/MKA, WAV/AIFF/AIF, MP3, FLAC, OGG/Opus, TS/MTS/M2TS, 3GP and ASF. Container support does not guarantee every possible embedded codec; packaged codecs are listed in [the recipe](packaging/Dockerfile). Explicit paths are not extension-filtered.

Defaults: `small`, Russian, TXT paragraphs, device `auto`, beam size 5, VAD enabled and 120-second windows. `--cpu-threads 0` selects physical cores within Linux process affinity and visible cgroup CPU quotas; explicit positive counts override it. `--language auto` detects language per window. Explicit `--device cuda` fails if unavailable; `auto` reports CPU fallback.

FFmpeg chooses the best audio stream. `--audio-stream N` selects an absolute container stream index, not an audio ordinal. Explicit selections are checked before model preparation. Captions follow the container timeline; `--timestamp-gaps auto` corrects large transport discontinuities, while `preserve` retains forward gaps. See [timeline rules](docs/resume.md#container-timeline).

## Install A Published Archive
Download from [Releases](https://github.com/egor-fedorov/whisper-transcribator/releases). Choose `cpu` or `cuda` for NVIDIA; CUDA runtime libraries are bundled, but a compatible driver is required.

```bash
version=0.3.0
flavor=cpu
archive="whisper-transcribator-${version}-linux-x86_64-${flavor}.tar.gz"
url="https://github.com/egor-fedorov/whisper-transcribator/releases/download/v${version}"
curl -fLO "$url/$archive"
curl -fLO "$url/SHA256SUMS"
sha256sum --check --ignore-missing SHA256SUMS
mkdir -p whisper-transcribator
tar -xzf "$archive" -C whisper-transcribator
./whisper-transcribator/bin/whisper-transcribator doctor --device "$flavor" --json
```

Keep `bin/`, `lib/` and `share/` together: these are not universal static binaries. Archives target Linux x86_64, Ubuntu 22.04+ / glibc 2.35+. Published 0.3.0 requires a Haswell-class AVX2/FMA/F16C/BMI2 CPU; archives built from `main` select a compatible installed CPU plugin, including baseline x86_64. Keep all bundled plugins. See [build and release instructions](docs/releasing.md).

## Models And Offline Use
```bash
./bin/whisper-transcribator models list
./bin/whisper-transcribator models download large-v3
./bin/whisper-transcribator models download silero-v6.2.0
./bin/whisper-transcribator lecture.wav --model large-v3 --local-files-only
./bin/whisper-transcribator lecture.wav --model /weights/ggml-small.bin --no-vad
```

The catalog provides `tiny`, `base`, `small`, `medium`, `large-v3`, `large-v3-turbo`, and turbo Q5_0 (574 MB) / Q8_0 (874 MB). Quantized names are `large-v3-turbo-q5_0` and `large-v3-turbo-q8_0`; `large` and `turbo` alias the unquantized models. File size is not total RAM/VRAM usage. Silero VAD is separate. **No weights are embedded in archives or Docker images**; only selected weights download on first use.

Catalog names take precedence over local names. Use `./small` for a local file named `small`. Local weights must be nonempty GGML files, not GGUF or faster-whisper/CTranslate2 directories. They are hashed but not checked against catalog contents.

Cache precedence: `--download-root`, `WHISPER_DOWNLOAD_ROOT`, then `${XDG_CACHE_HOME:-$HOME/.cache}/whisper-transcribator/models`. `WHISPER_MODEL` overrides the default model. Downloads use pinned revisions, TLS, size/SHA-256 checks, cancellable locks and atomic publication. Each prepared model is fully hashed once per invocation; `models list --json` also verifies cached weights without downloading. Corrupt published files are reported and preserved for explicit repair.

Interrupted downloads retain private hash-specific `.part` files and resume automatically. A server ignoring Range causes a safe restart. Corrupt partial downloads are removed; partial files are not usable weights. `--local-files-only` forbids downloads, including VAD. For a custom CA bundle use `SSL_CERT_FILE`; do not disable TLS verification.

## Outputs And Resume
```bash
./bin/whisper-transcribator lecture.m4a --output-dir transcripts --format all
# After interruption, repeat the same options and add --resume:
./bin/whisper-transcribator lecture.m4a --output-dir transcripts --format all --resume
```

`all` writes TXT, SRT, VTT and JSON from the same recognition. TXT/JSON text uses paragraph heuristics based on pauses, language changes and sentence endings, not an LLM rewrite. Lines are not wrapped to a fixed width. `--text-layout single-line` retains the old layout; `--paragraph-pause-ms` changes the 2000 ms pause threshold. Captions normalize whitespace; JSON segments retain raw recognized text. JSON schema 2 records per-segment languages and a multilingual summary; unavailable metrics are `null`.

Audio buffers cover only the current window; outputs stream from saved segments. Smaller `--chunk-seconds` values can reduce buffers but increase repeated recognition of window tails. They do not impose a hard model memory cap. Resume verifies input/model hashes and significant settings; thread counts and equivalent model spellings may change. Checkpoint schema 3, timeline version 2 and chunking version 4 do not migrate older progress.

See [long recordings and resume](docs/resume.md) for compatibility, timeline semantics, crash recovery, overwrite/skip behavior and numbered mappings. Existing outputs are never silently replaced. Successful publication removes journal payloads; lock files remain intentionally.

## Diagnostics
Status goes to stderr; command JSON stays on stdout. `--quiet` retains warnings/errors, while `--verbose` adds backend and checkpoint diagnostics without FFmpeg DEBUG/TRACE packet dumps. Terminal progress updates in place; redirected progress is rate-limited. The first SIGINT/SIGTERM stops gracefully; a second exits immediately with committed progress recoverable.

`doctor --device cpu --json` reports effective CPU count, backend availability, FFmpeg library versions, the loaded CPU library, system features and source revision without downloading models or recognizing speech. Development `--version` includes the source commit; builds without known provenance say `unknown`. Published run metadata remains that of the original job after resume.

Exit codes: 0 success/nothing to do, 1 runtime failure, 2 usage error, 130 SIGINT, 143 SIGTERM. `--continue-on-error` attempts subsequent files after runtime failures but still returns 1; it does not hide invalid explicit audio-stream selections.

## Optional Docker
Docker wraps the same native CLI. The runtime defaults to UID/GID 10001; for host bind mounts, create writable directories and use your own identity:

```bash
docker build -f packaging/Dockerfile --target runtime -t whisper-transcribator:cpu .
mkdir -p transcripts models
docker run --rm --user "$(id -u):$(id -g)" \
  -v "$PWD/lectures:/input:ro" -v "$PWD/transcripts:/output" \
  -v "$PWD/models:/models" whisper-transcribator:cpu \
  --input-dir /input --output-dir /output --naming numbered --model small
```

After downloading weights, add `--network none` to Docker and `--local-files-only` to the CLI; models may be mounted read-only. CUDA requires a CUDA build, an NVIDIA driver and NVIDIA Container Toolkit; see [CUDA verification](docs/releasing.md#cuda). Old root-owned volumes need explicit ownership repair on the host. Do not run untrusted media as root or mount unrelated directories.

## Scope
No server, LLM summarizer, second backend, parallel file jobs, batching, compute-type, prompts or word timestamps. Unknown flags are errors. Python 0.2 is frozen in `v0.2.0`, not maintained alongside C++. No full PCM cache, checkpoint migration between backend versions or absolute memory-budget flag is provided.

[Migration](docs/migration.md) | [Backend decision](docs/backend-selection.md) | [Contributing](CONTRIBUTING.md) | [Security](SECURITY.md) | [Dependency notices](docs/third-party.md)
