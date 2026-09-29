#!/usr/bin/env bash
# Run inside the clean verification image with --network none and explicit CUDA selection.
set -euo pipefail
binary=/opt/whisper-transcribator/bin/whisper-transcribator
"$binary" doctor --device cuda --json >/results/doctor.json
jq -e '.device == "cuda" and .errors == []' /results/doctor.json
bash /checks/smoke.sh "$binary" /fixtures/jfk.wav /fixtures/ggml-tiny.bin \
    /fixtures/ggml-silero-v6.2.0.bin /results/smoke cuda
bash /checks/streaming-smoke.sh "$binary" /tools/wt-audio-fixture /fixtures/jfk.wav \
    /fixtures/ggml-tiny.bin /fixtures/ggml-silero-v6.2.0.bin /results/streaming cuda
for result in /results/smoke/plain/jfk.json /results/smoke/vad/jfk.json \
    /results/streaming/plain/windows.json /results/streaming/vad/windows.json \
    /results/streaming/resume/windows.json; do
    jq -e '.run.device == "cuda"' "$result"
done
