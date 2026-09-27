#!/usr/bin/env bash
set -euo pipefail
binary=$1
recovery=$2
ffmpeg=$3
root=$(mktemp -d)
trap 'rm -rf "$root"' EXIT
"$ffmpeg" -hide_banner -loglevel error -f lavfi \
    -i 'sine=frequency=440:sample_rate=44100:duration=2' -c:a libmp3lame "$root/part.mp3"
cat "$root/part.mp3" "$root/part.mp3" >"$root/joined.mp3"
"$ffmpeg" -hide_banner -loglevel error -f lavfi \
    -i 'sine=frequency=880:sample_rate=48000:duration=12' -c:a aac "$root/clean.mkv"
# Mutate compressed payload only, leaving the container and timestamps valid.
"$ffmpeg" -hide_banner -loglevel error -i "$root/clean.mkv" -c:a copy \
    -bsf:a 'noise=amount=if(eq(n\,200)\,1\,0)' "$root/damaged.mkv"
for source in "$root/joined.mp3" "$root/damaged.mkv"; do
    "$binary" --summary "$source" auto >"$root/summary.json" 2>"$root/recovery.log"
    jq -e '.samples > 4 * 16000' "$root/summary.json"
    test "$(grep -c '^Warning: Skipping invalid audio data' "$root/recovery.log")" = 1
    grep -q 'Audio decoding completed with' "$root/recovery.log"
    "$ffmpeg" -hide_banner -loglevel error -i "$source" \
        -af 'aresample=16000:async=1:first_pts=0' -ac 1 -c:a pcm_f32le "$root/reference.wav" -y
    "$binary" --pcm "$source" "$root/reference.wav"
done
# Interruption before and after corruption; fake inference hashes actual decoded PCM.
"$recovery" --resume "$root/joined.mp3" 1
"$recovery" --resume "$root/joined.mp3" 3
"$recovery" --resume "$root/damaged.mkv" 2
"$recovery" --resume "$root/damaged.mkv" 6
"$ffmpeg" -hide_banner -loglevel error -i "$root/clean.mkv" -c:a copy \
    -bsf:a 'noise=amount=if(between(n\,200\,250)\,1\,0)' "$root/severe.mkv"
"$recovery" --severe "$root/severe.mkv"
