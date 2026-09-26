# whisper-transcribator

[Русская документация](README.ru.md)

A local, Docker-friendly transcription CLI built on **faster-whisper**.
Transcribe audio or video to continuous TXT, timestamped SRT and JSON, including
all three formats in one inference pass. No cloud transcription service.

Supported target: Linux x86_64, CPU and NVIDIA CUDA. Python 3.10/3.13 are the CI
targets; the Docker runtime uses pinned Python 3.13 dependencies. This is speech
recognition, not automatic lecture summarization.

## Quick Start

```bash
docker build -t whisper-transcribator .
docker run --rm -v "$PWD:/work" -v whisper-models:/models \
  whisper-transcribator transcribe /work/lecture.mp4 \
  --output-dir /work/results --format all --model small --language ru
```

Without Docker: `pip install .`, then use `whisper-transcribator` directly.
The old `whisper-transcribator FILE ...` syntax still means `transcribe`.
Use `transcribe -- ./doctor` for a file whose name matches a subcommand.

```bash
whisper-transcribator transcribe --input-dir ./lectures --output-dir ./results \
  --format all --skip-existing --model small --device cpu --language ru
whisper-transcribator models download large-v3
whisper-transcribator models list
whisper-transcribator doctor --device cuda --json
whisper-transcribator --version
whisper-transcribator transcribe --help
```

## Processing Rules

- Explicit inputs retain their order. Directory discovery is non-recursive and
  sorts filenames deterministically; supported media extensions are in `jobs.py`.
- Default output names use the source stem. `--naming numbered --prefix result`
  requires `--output-dir` and maintains `result_files.json`. A changed input list
  requires a new prefix; existing numbered outputs without their map are rejected.
- `--jobs 1` loads the model once for all inputs. CPU `--jobs N` uses independent
  workers, each holding a model; `--cpu-threads` applies to each worker.
  CUDA requires `--jobs 1`. `--batch-size` is intra-file model batching, not jobs.
- `--skip-existing` skips only complete nonempty output sets. It does **not**
  verify that source contents or model settings are unchanged. Partial or empty
  outputs require explicit `--overwrite` before replacement.
- Input/output collisions are rejected before loading a model. Individual files
  are published atomically without clobbering existing files by default.
  `--format all` is **not** an atomic transaction across three output files.
- Failures stop processing by default. `--continue-on-error` processes remaining
  files and still returns a failure status. SIGINT/SIGTERM stop CPU workers.
- Exit codes: 0 success, 1 processing failure, 2 invalid arguments, 130 interrupted,
  143 terminated. A partial transcript is never presented as a completed file.
- JSON schema version 1 retains source/model/language/text/segments and adds run
  parameters and dependency versions. It may contain private paths or prompts.

## Models And Offline Use

Models are not bundled in images. `--model` accepts a supported model name, a
Hugging Face repository ID or a local CTranslate2 model directory. Downloads use
the Hugging Face snapshot cache; complete legacy model directories are reused.
Local models require nonempty weights, configuration and tokenizer files.

Use `--download-root` or `WHISPER_DOWNLOAD_ROOT` to choose the cache. Docker uses
`/models`. Download first, then combine `--local-files-only` with Docker
`--network none` for enforced offline processing. Set `HF_HUB_DISABLE_XET=1` during
download if Xet is slow in your network. Keep tokens out of images and Git.

## GPU

```bash
docker build --target cuda -t whisper-transcribator:cuda .
docker run --rm --gpus all whisper-transcribator:cuda doctor --device cuda --json
docker run --rm --gpus all \
  -v "$PWD/lectures:/input:ro" -v "$PWD/results:/output" -v whisper-models:/models \
  whisper-transcribator:cuda transcribe --input-dir /input --output-dir /output \
  --format all --model large-v3 --device cuda --compute-type float16 --language ru
```

Create the output directory before mounting it. NVIDIA drivers and Container
Toolkit are required on the host. The image includes CUDA 12 cuBLAS and cuDNN 9.
Explicit CUDA requests fail rather than silently running on CPU. If `nvidia-smi`
works but CUDA initialization fails, some hosts need the additional Docker flags
`--device /dev/nvidia-uvm --device /dev/nvidia-uvm-tools`. Docker launchers add
existing UVM nodes for GPU runs. A successful `doctor` is a runtime check, not a
full model inference test. Containers run as root by default; for non-root usage
prepare writable cache/output directories and verify GPU access separately.
The Docker launchers treat `auto` as GPU-capable and request the NVIDIA runtime;
use `--device cpu` on hosts without it. Direct Python `auto` can fall back to CPU.

## Limitations And Migration

Long recordings are decoded in full and may consume substantial RAM even on GPU.
There is no chunked processing or resume after interruption. Transcripts can
contain hallucinations, repetitions and terminology errors; review important text.
Default language remains `ru`; use `--language auto` for detection.

The two shell scripts are now thin Docker launchers. Their processing logic lives
in the CLI. `run_whisper.sh` retains JOBS, THREADS, MODEL, DEVICE, PREFIX and OVERWRITE;
`MAP_FILE` is removed. Numbered mappings are JSON rather than TSV: use a new output
directory or prefix when migrating old results. Directory mode includes audio and
video, no longer only MP4. Launchers require Bash and Docker, not host Python.

## Development

See [CONTRIBUTING](CONTRIBUTING.md), [security guidance](SECURITY.md) and
[changelog](CHANGELOG.md). CI checks unit tests, package installation, CPU Docker,
offline inference, native sanitizers and Git secret scanning. GPU checks are
manual on trusted hardware, never on a personal runner exposed to public PRs.

[C++/whisper.cpp experiment](experiments/whisper_cpp/README.md): a separate native
prototype and benchmark, not a second supported production backend. Docker remains
useful for packaging; a decision to replace faster-whisper depends on measurements.

New product features, including chunking/resume, are paused pending
[backend selection](docs/backend-selection.md). The unfinished Python experiment
is preserved on `wip/python-chunk-resume`, not included in main or `v0.2.0`.
Next: [three short human-reviewed excerpts](docs/quality.md), not full-lecture runs.
Docker remains optional: direct Python installation is supported, and the native
prototype can run from its archive without Docker.

Own code: [MIT](LICENSE). Dependencies and model weights retain their own licenses.
