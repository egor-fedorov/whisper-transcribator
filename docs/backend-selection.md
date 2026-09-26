# Backend Selection: Pending

Audit date: 2026-09-26. Stable baseline: `v0.2.0`. No backend migration is approved
yet. New product features are paused on both implementations; unfinished Python
chunk/resume work is isolated on `wip/python-chunk-resume`. That branch is not
release-ready: its real-inference smoke found different output on checkpoint
re-render. Do not merge it merely to finish work already started.

## What We Are Comparing

This is **faster-whisper/CTranslate2 versus whisper.cpp/GGML**, not interpreted
Python arithmetic versus C++. CTranslate2 is itself a C++ inference engine with a
Python API. Merely rewriting a CLI around the same engine would not imply different
recognition quality or eliminate the engine's allocations.

Both candidates use converted large-v3 checkpoints, not separately trained models.
However, equal model names do not establish identical numeric computation or text
decoding. Floating-point differences can change a close token choice; subsequent
tokens depend on the preceding text. More importantly, our current pipelines leave
different decoding policies enabled. A short migration regression check is warranted,
not a new broad evaluation of large-v3 and not a requirement for byte-identical text.

## Verified Configuration Differences

Reviewed faster-whisper 1.2.1 (`WhisperModel`, not the batched pipeline) and
whisper.cpp commit `927cfce34f31707e17f2bff35c349632fb9e2c3a`, as used in the builds.
The wrappers set language, beam size, threads and VAD but leave the following
settings at library defaults:

| Aspect | Python baseline | Current native prototype |
| --- | --- | --- |
| GPU computation | CTranslate2 float16 | unquantized GGML, flash attention |
| Temperature fallback | 0, 0.2, ..., 1.0 | 0, 0.2, ..., 1.0 |
| Candidates at positive temperature | `best_of=5` | `greedy.best_of=-1`, clamped to one decoder |
| Repetition failure criterion | gzip compression ratio > 2.4 | token-sequence entropy < 2.4 when sequence length > 32 |
| Non-speech token suppression | default non-speech token set (`suppress_tokens=[-1]`) | `suppress_nst=false` |
| Beam scoring / patience | CTranslate2 scoring, patience 1 | different scoring implementation; patience marked unimplemented upstream |
| Audio track | first audio stream | FFmpeg best audio stream |
| Decoded PCM | resample to s16, then float32 | resample directly to float32 |

The last two differences are controlled by using identical single-track 16 kHz
PCM16 WAV excerpts. They still matter for future multi-track media support.
Matching names or numeric thresholds does not make the fallback algorithms equal.
Previous timing results are therefore configuration comparisons; a decoder change
would require new **short** timings, not reusing those numbers as a matched result.

Important nuance: native `no_context=true` clears history at the start of a call.
It does **not** disable history between audio windows within that call; the source
still appends previous tokens at low temperatures. Do not equate it with
faster-whisper's `condition_on_previous_text=False` or flip it to false and risk
carrying one file's text into another.

Evidence: [faster-whisper source](https://github.com/SYSTRAN/faster-whisper/blob/v1.2.1/faster_whisper/transcribe.py),
[whisper.cpp implementation](https://github.com/ggml-org/whisper.cpp/blob/927cfce34f31707e17f2bff35c349632fb9e2c3a/src/whisper.cpp),
[whisper.cpp parameter declarations](https://github.com/ggml-org/whisper.cpp/blob/927cfce34f31707e17f2bff35c349632fb9e2c3a/include/whisper.h),
and [CTranslate2 overview](https://opennmt.net/CTranslate2/).
Local wrapper decisions are in `whisper_transcribator/engine.py`,
`experiments/whisper_cpp/main.cpp` and `experiments/whisper_cpp/audio.cpp`.

## Native Adoption Work

The prototype already provides explicit file lists with model reuse, CPU/CUDA,
TXT/SRT/JSON, no-clobber atomic individual files, explicit VAD weights and an archive
that ran on clean Ubuntu 22.04 without Python/system FFmpeg. This is useful evidence,
not complete compatibility. Existing [parity and packaging notes](../experiments/whisper_cpp/README.md)
and [measured performance](../experiments/whisper_cpp/BENCHMARK.md) remain applicable.

Before replacing the current CLI, retain the established user workflow: sorted
directory inputs, stable numbered mappings, skip/overwrite/error semantics, model
selection/cache/download and actionable device errors. The prototype lacks these
directory/model-management conveniences. Decide separately whether CPU jobs, int8,
batching, prompts and word timestamps must be preserved; do not silently drop them.
Neither current production implementation provides bounded audio memory or resume.

The native archive is a binary plus shared libraries, not a universal static file.
Current CPU builds require AVX2-class hardware; the tested CUDA archive targets
SM89 and is about 950 MB compressed without weights. Keep dependency notices,
codec coverage, target architectures and driver requirements in the adoption cost.
Docker may build/test/package either implementation but need not be the user's
runtime. Switching to Rust is not a third inference engine or a necessary step.

## Next Gate

1. Human-review the three prepared 30-second excerpts without candidate text.
2. Run each existing backend once on those exact WAVs. Keep current configurations
   explicit; score WER/CER and inspect omissions, invented text and technical terms.
   If a discrepancy matters, isolate its decoder setting on that clip only.
3. Combine that result with existing short speed/memory measurements and the native
   adoption work above. Three clips are a sanity check, not statistical equivalence.
4. Choose the supported backend and then resume product feature development.

No human references exist yet, so neither backend has won a quality comparison.
No new inference is needed until those references are ready. The review set and
raw outputs stay in ignored `benchmark-results/`; see [quality.md](quality.md).
