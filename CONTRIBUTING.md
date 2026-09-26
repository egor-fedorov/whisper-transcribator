# Contributing

The supported application is a local Python CLI on Linux CPU/NVIDIA CUDA.
The C++ implementation is an experiment, not a replacement or a stable API.

Use Python 3.10 or 3.13. Install `pip install -e '.[dev]'`, then run:

```bash
ruff check .
ruff format --check whisper_transcribator tests tools
coverage run -m unittest discover -s tests
coverage report
python -m build
bash -n run_whisper.sh transcribe_dir.sh
shellcheck run_whisper.sh transcribe_dir.sh experiments/whisper_cpp/package.sh
```

Tests must not download models. Put real inference checks in the explicit smoke
workflow. To reproduce that workflow, run `python tools/smoke.py --prepare` once,
then `python tools/smoke.py`. It uses a pinned public sample and model snapshot.
CUDA inference must be checked on real hardware; compiling CUDA is not enough.

Keep model/data downloads out of the repository. Never add lecture recordings,
transcripts, tokens or private paths to an issue or a fixture. Include hardware,
versions, anonymized arguments and a minimal reproducible example in bug reports.

Make focused commits. Preserve output no-clobber behavior and input protection.
Do not add a server, GUI or a second production backend as part of a cleanup.
The CLI is the public interface; internal Python functions are not a stable API.
Publication gates are in [RELEASING.md](RELEASING.md).

`requirements.lock` is the Python 3.13 Linux Docker runtime snapshot, not a
cross-platform lock. Update it deliberately, run `pip check`, build both images,
and run real CPU and GPU smoke tests before accepting dependency upgrades.
