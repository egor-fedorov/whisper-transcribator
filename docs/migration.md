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
- Published 0.3.0 JSON keeps schema version 1; unavailable probability/decoder statistics are
  `null`. Metadata identifies `whisper.cpp`.
- All-format output is atomic per file, not an all-or-nothing transaction.

## Removed In 0.3

`--jobs`, batching, `--compute-type`, prompts and word timestamps are not
implemented. Published 0.3.0 has neither chunking nor resume. Unsupported flags produce a
usage error instead of being ignored. No compatibility wrapper silently translates
int8 into another precision. Use a local quantized GGML file if you intentionally
choose that model; the CLI does not convert weights.

## Unreleased Main

The native backend adds `--chunk-seconds` and `--resume`, not Python checkpoint
imports or recovery of interrupted 0.3.0 inference. Checkpoint schema 3 / chunking
version 4 / audio timeline version 3 reject earlier unfinished progress without migration: finish with the
old binary or explicitly restart. See [long recordings and resume](resume.md).

Container-relative timestamps preserve delayed audio and ordinary packet gaps, tolerate codec timestamp jitter, and correct large transport discontinuities by default. Use `--timestamp-gaps preserve` to retain intentional large forward gaps. Entirely zero-valued windows skip inference without losing checkpoint progress. Schema-2 checkpoints and schema-3 checkpoints using earlier timeline/chunking versions cannot be resumed under these rules. CPU counts and equivalent model spellings may change when resuming compatible schema-3 jobs; original output metadata remains frozen for reproducible publication.

TXT and JSON aggregate text now use paragraphs by default; choose
`--text-layout single-line` to retain the old layout. JSON schema 2 records
per-segment languages and a language list; its top-level language is `null` for
mixed-language or empty transcripts. `--format all` now means TXT/SRT/VTT/JSON.
An old three-file set is not considered complete, and old three-format checkpoints
must be finished with the old binary or moved aside explicitly before starting
over. Completed single-format transcripts are unaffected.

`--cpu-threads 0` now follows physical cores, process affinity and visible CPU
quotas instead of whisper.cpp's four-thread default. An explicit positive value
still overrides auto selection. Automatic language detection runs per window,
and FFmpeg selects the best audio stream unless `--audio-stream N` is supplied.

Exit codes remain meaningful: 1 runtime failure, 2 invalid CLI usage, 130/143
interrupt/termination. A process killed externally (for example by the OOM killer)
cannot print a final report; inspect the caller's exit status and system logs.
