#!/usr/bin/env bash
# Only repetitions of the public 11-second JFK fixture, never private recordings.
set -euo pipefail
binary=$(realpath "$1")
helper=$(realpath "$2")
sample=$(realpath "$3")
model=$(realpath "$4")
vad=$(realpath "$5")
mkdir -p "$6"
root=$(realpath "$6")
device=${7:-cpu}
"$helper" --repeat "$sample" "$root/windows.wav"
common=("$root/windows.wav" --model "$model" --vad-model "$vad" --language en
    --device "$device" --cpu-threads 2 --local-files-only --chunk-seconds 30 --format all --verbose)
for mode in plain vad; do
    extra=()
    if [[ $mode == plain ]]; then extra=(--no-vad); fi
    "$binary" "${common[@]}" "${extra[@]}" --output-dir "$root/$mode" >"$root/$mode.log" 2>&1
    jq -e '.duration == 41 and .run.chunk_seconds == 30 and .run.chunking_version == 2
        and (.segments | length > 1)
        and all(.segments[]; .start >= 0 and .end >= .start and .end <= 41)
        and any(.segments[]; .start >= 30)' "$root/$mode/windows.json"
    test "$(grep -c '^Checkpoint:' "$root/$mode.log")" -ge 2
done
# The trailing pause moves the first VAD boundary before the 30-second hard limit.
grep -q '^Checkpoint: 30\.000000s$' "$root/plain.log"
if grep -q '^Checkpoint: 30\.000000s$' "$root/vad.log"; then
    echo 'VAD did not choose the trailing pause' >&2
    exit 1
fi
"$binary" "${common[@]}" --no-vad --output-dir "$root/resume" >"$root/interrupted.log" 2>&1 &
pid=$!
trap 'kill -TERM "$pid" 2>/dev/null || true; wait "$pid" 2>/dev/null || true' EXIT
found=false
for ((i = 0; i < 12000; ++i)); do
    if grep -q '^Checkpoint: 30\.000000s$' "$root/interrupted.log"; then
        found=true
        break
    fi
    if ! kill -0 "$pid" 2>/dev/null; then break; fi
    sleep 0.01
done
if [[ $found != true ]]; then
    cat "$root/interrupted.log"
    exit 1
fi
kill -TERM "$pid"
status=0
wait "$pid" || status=$?
trap - EXIT
test "$status" = 143
test ! -e "$root/resume/windows.txt"
"$binary" "${common[@]}" --no-vad --resume --output-dir "$root/resume" >"$root/resumed.log" 2>&1
grep -q '^Resume: decoding prefix without inference to 30\.000000s$' "$root/resumed.log"
if grep -q '^Recognizing 0\.000000-' "$root/resumed.log"; then exit 1; fi
jq -e '.duration == 41 and (.segments | length > 1)' "$root/resume/windows.json"
# Already committed segments must be retained exactly, not regenerated.
diff <(jq -c '[.segments[] | select(.end <= 30)]' "$root/plain/windows.json") \
     <(jq -c '[.segments[] | select(.end <= 30)]' "$root/resume/windows.json")
echo "Windowed $device inference and resume smoke passed"
