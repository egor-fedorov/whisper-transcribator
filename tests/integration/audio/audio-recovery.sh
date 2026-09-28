#!/usr/bin/env bash
set -euo pipefail
binary=$1
recovery=$2
ffmpeg=$3
root=$(mktemp -d)
cleanup() {
    if (( $? == 0 )); then
        rm -rf "$root"
    else
        printf 'Audio recovery fixtures: %s\n' "$root" >&2
    fi
}
trap cleanup EXIT
check_recovery() {
    local source=$1
    "$binary" --summary "$source" auto >"$root/summary.json" 2>"$root/recovery.log"
    jq -e '.samples > 11 * 16000 and .samples < 13 * 16000' "$root/summary.json"
    test "$(grep -c '^Warning: Skipping invalid audio data' "$root/recovery.log")" = 1
    grep -q 'Audio decoding completed with' "$root/recovery.log"
    "$ffmpeg" -hide_banner -loglevel error -i "$source" \
        -af 'aresample=16000:async=1:first_pts=0' -ac 1 -c:a pcm_f32le "$root/reference.wav" -y
    "$binary" --recovered "$source" "$root/reference.wav"
}
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
"$recovery" --failure "$root/severe.mkv" 1
"$recovery" --failure "$root/damaged.mkv" strict
for count in 8 16 47; do
    "$ffmpeg" -hide_banner -loglevel error -i "$root/clean.mkv" -c:a copy \
        -bsf:a "noise=amount=if(between(n\\,200\\,$((199 + count)))\\,1\\,0)" "$root/damaged-$count.mkv"
    check_recovery "$root/damaged-$count.mkv"
done
"$ffmpeg" -hide_banner -loglevel error -f lavfi \
    -i 'sine=frequency=880:sample_rate=48000:duration=12' -c:a aac -b:a 128k -f mp4 "$root/clean.m4a"
for bytes in 4096 16384; do
    "$recovery" --zero-m4a "$root/clean.m4a" "$root/zero-$bytes.m4a" "$bytes"
    check_recovery "$root/zero-$bytes.m4a"
    "$recovery" --resume "$root/zero-$bytes.m4a" 2
    "$recovery" --resume "$root/zero-$bytes.m4a" 9
done
"$ffmpeg" -hide_banner -loglevel error -i "$root/clean.m4a" -c:a copy \
    -muxrate 188000 "$root/clean.ts"
"$recovery" --drop-ts "$root/clean.ts" "$root/loss.ts" 125
check_recovery "$root/loss.ts"
"$recovery" --resume "$root/loss.ts" 2
"$recovery" --resume "$root/loss.ts" 9
# Unlike random noise, a zero block cannot accidentally form a valid AAC frame.
# Exercise the default 30-second limit with generated audio, never inference.
"$ffmpeg" -hide_banner -loglevel error -f lavfi \
    -i 'sine=frequency=880:sample_rate=48000:duration=80' -c:a aac -b:a 128k -f mp4 "$root/long.m4a"
"$recovery" --zero-m4a "$root/long.m4a" "$root/long-damaged.m4a" 524288
"$recovery" --failure "$root/long-damaged.m4a" 30
