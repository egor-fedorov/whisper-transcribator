#!/usr/bin/env bash
set -euo pipefail
binary=$1
ffmpeg=$2
cli=$3
root=$(mktemp -d)
trap 'rm -rf "$root"' EXIT
"$ffmpeg" -hide_banner -loglevel error -f lavfi -i 'color=s=16x16:r=1:d=7' \
    -itsoffset 5 -f lavfi -i 'sine=frequency=440:sample_rate=48000:duration=2' \
    -map 0:v -map 1:a -c:v mpeg4 -c:a aac "$root/delayed.mp4"
"$ffmpeg" -hide_banner -loglevel error -f lavfi \
    -i 'sine=frequency=440:sample_rate=16000:duration=8' \
    -af "asetnsamples=n=160:p=0,aselect='lt(t,1)+gte(t,7)'" -c:a pcm_f32le "$root/gap.mkv"
"$ffmpeg" -hide_banner -loglevel error -f lavfi \
    -i 'sine=frequency=880:sample_rate=44100:duration=16' -ac 2 -c:a pcm_f32le "$root/fractional.mkv"
for source in "$root/delayed.mp4" "$root/gap.mkv" "$root/fractional.mkv"; do
    "$ffmpeg" -hide_banner -loglevel error -i "$source" -map 0:a:0 \
        -af 'aresample=16000:async=1:first_pts=0' -ac 1 -c:a pcm_f32le "$root/reference.wav" -y
    "$binary" --timeline "$source" "$root/reference.wav"
done
"$ffmpeg" -hide_banner -loglevel error -f lavfi \
    -i 'sine=frequency=440:sample_rate=16000:duration=2' \
    -af 'asetnsamples=n=160:p=0,asetpts=PTS+gte(N\,16000)*86400/TB' \
    -c:a pcm_f32le "$root/huge-gap.mkv"
# The enormous timestamp gap must remain a counter, not a PCM allocation.
# Sanitizer virtual address reservations prohibit ulimit -v; RSS is checked separately.
"$binary" --gap "$root/huge-gap.mkv"
status=0
"$cli" "$root/delayed.mp4" --audio-stream 0 --local-files-only \
    --download-root "$root/no-models" >"$root/stdout" 2>"$root/stderr" || status=$?
test "$status" = 2
grep -q -- '--audio-stream' "$root/stderr"
test ! -e "$root/no-models"
test ! -e "$root/.whisper-transcribator"
