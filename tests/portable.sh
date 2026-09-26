#!/usr/bin/env bash
# Run against an unpacked CPU archive, using only the public short fixture.
set -euo pipefail
bundle=$(realpath "$1")
fixtures=$(realpath "$2")
mkdir -p "$3"
root=$(realpath "$3")
test -s "$bundle/share/backends.txt"
test -s "$bundle/lib/libggml-cpu-x64.so"
timeout --kill-after=10 600 qemu-x86_64 -cpu qemu64 "$bundle/bin/whisper-transcribator" \
    "$fixtures/jfk.wav" --model "$fixtures/ggml-tiny.bin" --no-vad --language en \
    --cpu-threads 1 --beam-size 1 --device cpu --local-files-only --format json --output-dir "$root/no-avx2"
jq -e '.run.device == "cpu" and (.text | ascii_downcase | contains("country"))' "$root/no-avx2/jfk.json"
mkdir -p "$root/missing/cwd"
cp -a "$bundle/bin" "$bundle/lib" "$root/missing/"
cp "$bundle/lib/libggml-cpu-x64.so" "$root/missing/cwd/"
find "$root/missing/lib" -maxdepth 1 -name 'libggml-cpu*' -delete
status=0
(cd "$root/missing/cwd" && ../bin/whisper-transcribator doctor --device cpu --json) \
    >"$root/missing.json" 2>"$root/missing.log" || status=$?
test "$status" = 1
jq -e 'any(.errors[]; contains("No compatible CPU backend"))' "$root/missing.json"
echo 'Portable CPU inference without AVX2 and missing-plugin diagnostics passed'
