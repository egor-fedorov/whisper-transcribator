#!/usr/bin/env bash
# Model-free trace: invalid media must be rejected after exactly one model hash.
set -euo pipefail
binary=$(realpath "$1")
cache=$(realpath "$2")
root=$3
mkdir -p "$root"
printf 'invalid audio' >"$root/invalid.wav"
status=0
strace -f -e trace=openat -o "$root/opens.log" "$binary" "$root/invalid.wav" \
    --output-dir "$root/output" --model tiny --download-root "$cache" \
    --device cpu --local-files-only --no-vad >"$root/stdout" 2>"$root/stderr" || status=$?
test "$status" = 1
grep -q 'open media:' "$root/stderr"
test "$(grep -F 'ggml-tiny.bin"' "$root/opens.log" | grep -c 'O_RDONLY')" = 1
echo 'One model hash per preparation passed'
