#!/usr/bin/env bash
# Default: backend loading only. An optional fixture directory opts into slow inference.
set -euo pipefail
if [[ $# != 2 && $# != 3 ]]; then
    echo "Usage: $0 BUNDLE OUTPUT [FIXTURES (opts into slow inference)]" >&2
    exit 2
fi
bundle=$(realpath "$1")
mkdir -p "$2"
root=$(realpath "$2")
# Emulate the oldest CPU each archive supports: x86-64 without AVX2, or ARMv8.0.
case "$(uname -m)" in
    x86_64) emulator=(qemu-x86_64 -cpu qemu64) baseline=libggml-cpu-x64.so cpu='non-AVX2' ;;
    aarch64) emulator=(qemu-aarch64 -cpu cortex-a53) baseline=libggml-cpu-armv8.0_1.so cpu='ARMv8.0' ;;
    *) echo "Unsupported archive architecture: $(uname -m)" >&2; exit 1 ;;
esac
test -s "$bundle/share/backends.txt"
test -s "$bundle/lib/$baseline"
echo "Checking $cpu backend loading (no model or inference; 30s limit)"
timeout --kill-after=5 30 "${emulator[@]}" "$bundle/bin/whisper-transcribator" \
    doctor --device cpu --json >"$root/baseline-doctor.json"
jq -e --arg baseline "$baseline" '.device == "cpu" and .errors == [] and .cpu_backend == $baseline' \
    "$root/baseline-doctor.json"
mkdir -p "$root/missing/cwd"
cp -a "$bundle/bin" "$bundle/lib" "$root/missing/"
cp "$bundle/lib/$baseline" "$root/missing/cwd/"
find "$root/missing/lib" -maxdepth 1 -name 'libggml-cpu*' -delete
status=0
(cd "$root/missing/cwd" && ../bin/whisper-transcribator doctor --device cpu --json) \
    >"$root/missing.json" 2>"$root/missing.log" || status=$?
test "$status" = 1
jq -e 'any(.errors[]; contains("No compatible CPU backend"))' "$root/missing.json"
echo "$cpu backend loading and missing-plugin diagnostics passed"
if [[ $# == 3 ]]; then
    fixtures=$(realpath "$3")
    echo 'Opt-in inference under slow QEMU emulation; public 11-second fixture, 10m limit'
    timeout --kill-after=10 600 "${emulator[@]}" "$bundle/bin/whisper-transcribator" \
        "$fixtures/jfk.wav" --model "$fixtures/ggml-tiny.bin" --no-vad --language en \
        --cpu-threads 1 --beam-size 1 --device cpu --local-files-only --format json --output-dir "$root/baseline"
    jq -e '.run.device == "cpu" and (.text | ascii_downcase | contains("country"))' "$root/baseline/jfk.json"
    echo 'Opt-in portable CPU inference passed'
fi
