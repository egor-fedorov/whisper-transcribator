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
# macOS Bash 3.2 rejects "${extra[@]}" for an empty array under set -u.
for mode in plain vad; do
    extra=()
    if [[ $mode == plain ]]; then extra=(--no-vad); fi
    "$binary" "${common[@]}" ${extra[@]+"${extra[@]}"} --output-dir "$root/$mode" >"$root/$mode.log" 2>&1
    jq -e '.duration == 41 and .run.chunk_seconds == 30 and .run.chunking_version == 4
        and (.segments | length > 1)
        and all(.segments[]; .start >= 0 and .end >= .start and .end <= 41)
        and any(.segments[]; .start >= 30)' "$root/$mode/windows.json"
    test "$(grep -c '^Checkpoint:' "$root/$mode.log")" -ge 2
done
# Both modes retain unfinished audio, rather than cutting the first window at 30s.
for mode in plain vad; do
    awk '/^Checkpoint:/ { if ($2 + 0 <= 0 || $2 + 0 >= 30) exit 1; found=1; exit } END { if (!found) exit 1 }' "$root/$mode.log"
done
if [[ -n ${WT_PROCESS_RUNNER:-} ]]; then
    status=0
    expected=143
    if command -v cygpath >/dev/null; then expected=130; fi
    "$WT_PROCESS_RUNNER" --interrupt-after "$root/interrupted.log" 'Checkpoint:' "$binary" \
        "${common[@]}" --no-vad --output-dir "$root/resume" || status=$?
    test "$status" = "$expected"
else
"$binary" "${common[@]}" --no-vad --output-dir "$root/resume" >"$root/interrupted.log" 2>&1 &
pid=$!
trap 'kill -TERM "$pid" 2>/dev/null || true; wait "$pid" 2>/dev/null || true' EXIT
found=false
for ((i = 0; i < 12000; ++i)); do
    if grep -q '^Checkpoint:' "$root/interrupted.log"; then
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
fi
test ! -e "$root/resume/windows.txt"
manifest=$(find "$root/resume/.whisper-transcribator" -name manifest.json -type f)
boundary=$(jq -r '.samples / 16000' "$manifest")
"$binary" "${common[@]}" --no-vad --resume --output-dir "$root/resume" >"$root/resumed.log" 2>&1
grep -q '^Resume: decoding prefix without inference to ' "$root/resumed.log"
if grep -q '^Recognizing 0\.0-' "$root/resumed.log"; then exit 1; fi
jq -e '.duration == 41 and (.segments | length > 1)' "$root/resume/windows.json"
# Already committed segments must be retained exactly, not regenerated.
diff <(jq -c --argjson boundary "$boundary" '[.segments[] | select(.end <= $boundary)]' "$root/plain/windows.json") \
     <(jq -c --argjson boundary "$boundary" '[.segments[] | select(.end <= $boundary)]' "$root/resume/windows.json")
"$helper" --silence "$sample" "$root/silence.wav"
for mode in plain vad; do
    extra=()
    if [[ $mode == plain ]]; then extra=(--no-vad); fi
    status=0
    "$binary" "$root/silence.wav" "${common[@]:1}" ${extra[@]+"${extra[@]}"} \
        --output-dir "$root/silent-$mode" >"$root/silent-$mode.log" 2>&1 || status=$?
    test "$status" = 1
    grep -q 'No transcript produced' "$root/silent-$mode.log"
    test ! -e "$root/silent-$mode/silence.txt"
    test ! -e "$root/silent-$mode/silence.json"
    manifest=$(find "$root/silent-$mode/.whisper-transcribator" -name manifest.json -type f)
    jq -e '.samples == 41 * 16000 and .finished and .languages == []' "$manifest"
    test "$(grep -c '^Skipping digital silence:' "$root/silent-$mode.log")" = 2
    if grep -Eq '^(Loading model:|Recognizing )' "$root/silent-$mode.log"; then exit 1; fi
done
echo "Windowed $device inference and resume smoke passed"
