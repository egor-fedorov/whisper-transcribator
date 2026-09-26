# Backend Decision

Decision: 2026-09-26. Starting with 0.3, the only supported implementation is
C++17 + whisper.cpp. Python/faster-whisper 0.2 is retained in the immutable
`v0.2.0` tag as a rollback reference, not a maintained branch or second backend.
Docker remains optional packaging of the same CLI; archives are the primary
delivery. A Rust wrapper would not by itself change the inference engine.

The local comparison used four 120-second clips, three runs per configuration,
on an i5-12400F / RTX 4060 Ti 16 GB. Median end-to-end seconds and maximum process
RSS are shown below; these are configuration-specific, not universal speed claims.

| Configuration | faster-whisper | whisper.cpp |
| --- | ---: | ---: |
| CPU / small, seconds | 82.08 | 98.38 |
| CPU / small, RSS MiB | 1635 | 872 |
| CUDA / large-v3, seconds | 35.76 | 29.91 |
| CUDA / large-v3, RSS MiB | 3379 | 664 |

Versions: faster-whisper 1.2.1 / CTranslate2 4.6.0; whisper.cpp
`927cfce34f31707e17f2bff35c349632fb9e2c3a`. Both used Russian, beam 5,
four CPU threads and VAD off. CT2 used float32 on CPU and float16 on GPU;
GGML used unquantized weights with flash attention. Precision/decoder defaults
were not identical. These figures do not characterize the old CPU int8 default,
VAD memory peaks or arbitrary GPUs. Raw results remain private.

An internal manual check of short large-v3 transcripts from both backends was
completed: the owner found them correct and practically identical. Audio and
transcripts are not included in the repository; no further quality framework is
needed for this decision.

The selection favors the measured GPU/RAM behavior and native distribution,
not a claim that Python arithmetic is slow: faster-whisper already uses native
inference. Core file safety and model management are retained; the narrower
0.3 feature scope is explicit in the [migration guide](migration.md).
Bounded-memory decoding and resume are future work, not implied by this rewrite.
