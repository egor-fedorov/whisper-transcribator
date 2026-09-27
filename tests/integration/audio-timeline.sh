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

# Transients force Vorbis block-size changes; a constant sine misses its PTS jitter.
for rate in 44100 48000; do
    for codec in libvorbis libmp3lame; do
        suffix=ogg
        if [[ $codec == libmp3lame ]]; then suffix=mp3; fi
        source="$root/$codec-$rate.$suffix"
        "$ffmpeg" -hide_banner -loglevel error -f lavfi \
            -i "aevalsrc=0.25*sin(2*PI*(440*t+30*t*t))*lt(mod(t\,0.37)\,0.2):s=$rate:d=8" \
            -c:a "$codec" "$source"
        "$ffmpeg" -hide_banner -loglevel error -i "$source" \
            -af 'aresample=16000:async=1:first_pts=0' -ac 1 -c:a pcm_f32le "$root/reference.wav" -y
        "$binary" --timeline "$source" "$root/reference.wav"
    done
done

"$ffmpeg" -hide_banner -loglevel error -f lavfi \
    -i 'sine=frequency=440:sample_rate=48000:duration=2' \
    -c:a aac -muxdelay 0 -f mpegts "$root/first.ts"
"$binary" --summary "$root/first.ts" auto >"$root/summary.json" 2>"$root/ts.log"
jq -e '.estimate_before > 0 and .estimate_after == .estimate_before' "$root/summary.json"
if grep -Eq 'start time for stream|duration not set' "$root/ts.log"; then exit 1; fi
for offset in 8 10800; do
    "$ffmpeg" -hide_banner -loglevel error -f lavfi \
        -i 'sine=frequency=660:sample_rate=48000:duration=2' \
        -c:a aac -output_ts_offset "$offset" -muxdelay 0 -f mpegts "$root/second.ts" -y
    cat "$root/first.ts" "$root/second.ts" >"$root/jump.ts"
    if [[ $offset == 8 ]]; then
        "$ffmpeg" -hide_banner -loglevel error -i "$root/jump.ts" \
            -af 'aresample=16000:async=1:first_pts=0' -ac 1 -c:a pcm_f32le "$root/reference.wav" -y
        "$binary" --pcm "$root/jump.ts" "$root/reference.wav"
    else
        "$binary" --compare "$root/jump.ts" "$root/first.ts" "$root/second.ts"
        "$binary" --summary "$root/jump.ts" auto >"$root/summary.json" 2>"$root/jump.log"
        jq -e '.samples < 5 * 16000 and .estimate_after == 0' "$root/summary.json"
        test "$(grep -c 'Warning: Audio timestamp discontinuity corrected' "$root/jump.log")" = 1
        "$binary" --summary "$root/jump.ts" preserve >"$root/summary.json" 2>"$root/gap.log"
        jq -e '.samples > 10800 * 16000 and .samples < 10803 * 16000' "$root/summary.json"
        test "$(grep -c 'Warning: Preserving audio timestamp gap' "$root/gap.log")" = 1
    fi
done
