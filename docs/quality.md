# Short Human-Reference Evaluation

The native prototype is not promoted based on speed or agreement with another
machine transcript. A human must listen and transcribe the exact same audio first.
Do not use a faster-whisper result as the reference for whisper.cpp (or vice versa).

## Prepare

Use short mono 16-bit 16 kHz WAV clips that you are allowed to process:

```bash
python tools/quality.py prepare --root benchmark-results/quality --seconds 30 \
  short_source_1.wav short_source_2.wav short_source_3.wav
```

This reads only the first 30 seconds of each WAV, creates numbered audio clips,
empty `clip_001.reference.txt` files and a private `manifest.json`. It refuses to
overwrite an existing set. It does not run any recognition or download anything.

Listen to each generated WAV. Write a verbatim reference in its `.reference.txt`:
keep repetitions, false starts and spoken filler words; do not summarize or repair
grammar. Use consistent numeral/abbreviation spelling. Avoid seeing candidate
transcripts before writing the reference. If audio is genuinely unintelligible,
choose a different excerpt rather than inventing words. Mark that clip's
`human_reviewed` as `true` in the manifest after checking the text against audio.

## Recognize And Score

Run both backends on these **exact generated WAVs**, in separate directories,
with the same multilingual model family, language, VAD choice and beam settings.
Keep commands, model revisions/checksums and runtime versions with the results.
Keep whole-file and chunked recognition experiments separate: chunk boundaries
are another variable. This tool scores text; it does not validate inference settings.

```bash
whisper-transcribator benchmark-results/quality/clip_*.wav \
  --output-dir benchmark-results/quality/python --model /path/to/ct2-large-v3 \
  --device cuda --compute-type float16 --language ru --beam-size 5 --no-vad
/path/to/whisper-transcribator-native benchmark-results/quality/clip_*.wav \
  --output-dir benchmark-results/quality/native --model /path/to/ggml-large-v3.bin \
  --device cuda --language ru --beam-size 5 --no-vad
python tools/quality.py score --root benchmark-results/quality \
  --hypotheses benchmark-results/quality/python benchmark-results/quality/native
```

Scoring refuses unchecked/empty references, changed audio or missing hypotheses.
Normalization is Unicode NFKC, case folding, Russian yo-to-e conversion, punctuation
removal and single-space token joining. Numbers are not semantically normalized.
WER is word-level Levenshtein distance divided by reference words; CER uses normalized
characters including spaces. Totals sum errors and denominators across clips rather
than averaging percentages. Rates can exceed 1 due to insertions. Report raw counts,
text hashes and inspect important technical terms manually as well.

Three short clips are a preliminary check, not a representative quality benchmark.
Keep recordings/references/results private unless you have redistribution permission.
The local September 2026 review set is ignored by Git. No human-reference quality
verdict is available yet; the main supported backend remains faster-whisper.
