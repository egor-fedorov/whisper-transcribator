#!/usr/bin/env bash
# Run inside the clean verification image with --network none and explicit GPU selection.
set -euo pipefail
[[ $# = 1 && $1 =~ ^(cuda|vulkan)$ ]]
device=$1
binary=/opt/whisper-transcribator/bin/whisper-transcribator
"$binary" doctor --device "$device" --json >/results/doctor.json
jq -e --arg device "$device" '.device == $device and .errors == []' /results/doctor.json
bash /checks/smoke.sh "$binary" /fixtures/jfk.wav /fixtures/ggml-tiny.bin \
    /fixtures/ggml-silero-v6.2.0.bin /results/smoke "$device"
bash /checks/streaming-smoke.sh "$binary" /tools/wt-audio-fixture /fixtures/jfk.wav \
    /fixtures/ggml-tiny.bin /fixtures/ggml-silero-v6.2.0.bin /results/streaming "$device"
for result in /results/smoke/plain/jfk.json /results/smoke/vad/jfk.json \
    /results/streaming/plain/windows.json /results/streaming/vad/windows.json \
    /results/streaming/resume/windows.json; do
    jq -e --arg device "$device" '.run.device == $device' "$result"
done
