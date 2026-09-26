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
test -s "$bundle/share/backends.txt"
test -s "$bundle/lib/libggml-cpu-x64.so"
echo 'Checking non-AVX2 backend loading (no model or inference; 30s limit)'
timeout --kill-after=5 30 qemu-x86_64 -cpu qemu64 "$bundle/bin/whisper-transcribator" \
    doctor --device cpu --json >"$root/no-avx2-doctor.json"
jq -e '.device == "cpu" and .errors == []' "$root/no-avx2-doctor.json"
mkdir -p "$root/missing/cwd"
cp -a "$bundle/bin" "$bundle/lib" "$root/missing/"
cp "$bundle/lib/libggml-cpu-x64.so" "$root/missing/cwd/"
find "$root/missing/lib" -maxdepth 1 -name 'libggml-cpu*' -delete
status=0
(cd "$root/missing/cwd" && ../bin/whisper-transcribator doctor --device cpu --json) \
    >"$root/missing.json" 2>"$root/missing.log" || status=$?
test "$status" = 1
jq -e 'any(.errors[]; contains("No compatible CPU backend"))' "$root/missing.json"
echo 'Non-AVX2 backend loading and missing-plugin diagnostics passed'
if [[ $# == 3 ]]; then
    fixtures=$(realpath "$3")
    echo 'Opt-in inference under slow QEMU emulation; public 11-second fixture, 10m limit'
    timeout --kill-after=10 600 qemu-x86_64 -cpu qemu64 "$bundle/bin/whisper-transcribator" \
        "$fixtures/jfk.wav" --model "$fixtures/ggml-tiny.bin" --no-vad --language en \
        --cpu-threads 1 --beam-size 1 --device cpu --local-files-only --format json --output-dir "$root/no-avx2"
    jq -e '.run.device == "cpu" and (.text | ascii_downcase | contains("country"))' "$root/no-avx2/jfk.json"
    echo 'Opt-in portable CPU inference passed'
fi
