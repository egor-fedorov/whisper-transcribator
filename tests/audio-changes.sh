#!/usr/bin/env bash
set -euo pipefail
ulimit -c 0
binary=$1
ffmpeg=$2
root=$(mktemp -d)
trap 'rm -rf "$root"' EXIT
"$ffmpeg" -hide_banner -loglevel error -f lavfi \
    -i 'sine=frequency=440:sample_rate=48000:duration=2' -ac 6 -c:a aac -f mpegts "$root/surround.ts"
"$ffmpeg" -hide_banner -loglevel error -f lavfi \
    -i 'sine=frequency=660:sample_rate=16000:duration=2' -ac 1 -c:a aac -f mpegts "$root/mono.ts"
cat "$root/surround.ts" "$root/mono.ts" >"$root/forward.ts"
cat "$root/mono.ts" "$root/surround.ts" >"$root/reverse.ts"
"$binary" --compare "$root/forward.ts" "$root/surround.ts" "$root/mono.ts"
"$binary" --compare "$root/reverse.ts" "$root/mono.ts" "$root/surround.ts"
