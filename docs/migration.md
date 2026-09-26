# Migrating From Python 0.2 To Native 0.3

There is one supported backend: C++ + whisper.cpp. The historical `v0.2.0` tag
is untouched and can reproduce the old Python package; it does not receive
parallel feature development or CI. No Python code is shipped in the current
CLI or Docker runtime. WIP Python chunk/resume work is not part of this release.

## Keep

`transcribe` (or flat input syntax), `models list/download`, `doctor`,
`--version`, explicit files or `--input-dir`, sorted directory discovery,
`--output-dir`, source/numbered names, `--prefix`, JSON mapping,
`--skip-existing`, `--overwrite`, `--continue-on-error`, TXT/SRT/JSON,
language selection, CPU threads and optional VAD.

Use a fresh output directory when comparing backends. Skipping an old nonempty
result does not establish which model/backend created it. Existing numbered JSON
mappings are understood when the absolute source list and requested formats match.
Old TSV mappings are not supported.

## Change

- Run `bin/whisper-transcribator` or the native Docker image, not `python -m ...`.
- Process files sequentially, reusing one model. Root shell launchers are removed.
- Download GGML weights separately; CTranslate2/Hugging Face cache directories
  cannot be reused as native models. Existing weights/results are not deleted.
- Cache defaults to the XDG cache directory. Docker explicitly uses `/models`.
- `--device auto` chooses CUDA when available, otherwise reports CPU fallback.
- JSON keeps schema version 1; unavailable probability/decoder statistics are
  `null`. Metadata identifies `whisper.cpp`.
- All-format output is atomic per file, not an all-or-nothing transaction.

## Removed In 0.3

`--jobs`, batching, `--compute-type`, prompts and word timestamps are not
implemented. Neither chunking nor resume is available. These flags produce a
usage error instead of being ignored. No compatibility wrapper silently translates
int8 into another precision. Use a local quantized GGML file if you intentionally
choose that model; the CLI does not convert weights.

Exit codes remain meaningful: 1 runtime failure, 2 invalid CLI usage, 130/143
interrupt/termination. A process killed externally (for example by the OOM killer)
cannot print a final report; inspect the caller's exit status and system logs.
