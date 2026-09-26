#!/usr/bin/env bash
# Only the public 11-second JFK fixture, never a lecture or a quality benchmark.
set -euo pipefail
binary=$(realpath "$1")
sample=$(realpath "$2")
model=$(realpath "$3")
vad=$(realpath "$4")
root=$5
device=${6:-cpu}
mkdir -p "$root"
root=$(realpath "$root")
common=(--model "$model" --vad-model "$vad" --language en --device "$device" --cpu-threads 2 --local-files-only)
"$binary" "$sample" --output-dir "$root/plain" --format all --no-vad "${common[@]}"
jq -e '.schema_version == 2 and .language == "en" and .languages == ["en"] and .duration > 10 and .duration < 12
    and (.segments | length > 0) and (.text | ascii_downcase | contains("country"))' "$root/plain/jfk.json"
test -s "$root/plain/jfk.txt"
grep -q -- '-->' "$root/plain/jfk.srt"
test "$(head -n 1 "$root/plain/jfk.vtt")" = WEBVTT
"$binary" "$sample" --output-dir "$root/vad" --format all "${common[@]}"
jq -e '.run.vad == true and (.segments | length > 0)' "$root/vad/jfk.json"
# A failed first file must not claim success or block the next with --continue-on-error.
printf 'not audio' >"$root/broken.wav"
status=0
"$binary" "$root/broken.wav" "$sample" --continue-on-error --output-dir "$root/batch" "${common[@]}" || status=$?
test "$status" = 1
test ! -e "$root/batch/broken.txt"
test -s "$root/batch/jfk.txt"
before=$(sha256sum "$root/batch/jfk.txt")
cp "$sample" "$root/broken.wav"
"$binary" "$root/broken.wav" "$sample" --continue-on-error --skip-existing \
    --output-dir "$root/batch" "${common[@]}" >"$root/retry.log" 2>&1
test -s "$root/batch/broken.txt"
test "$before" = "$(sha256sum "$root/batch/jfk.txt")"
grep -q '^Skipping complete result:' "$root/retry.log"
mkdir -p "$root/sequential"
cp "$sample" "$root/sequential/01.wav"
cp "$sample" "$root/sequential/02.wav"
"$binary" --input-dir "$root/sequential" --output-dir "$root/numbered" --naming numbered "${common[@]}"
test -s "$root/numbered/result_001.txt"
test -s "$root/numbered/result_002.txt"
jq -e 'length == 2 and .[0].outputs.text == "result_001.txt"' "$root/numbered/result_files.json"
"$binary" "$sample" --output-dir "$root/plain" --format all --skip-existing --model /missing --local-files-only
# SIGTERM must not publish a partial transcript or exit successfully.
"$binary" "$sample" -o "$root/interrupted.txt" "${common[@]}" >"$root/interrupt.log" 2>&1 &
pid=$!
sleep 0.05
kill -TERM "$pid"
status=0
wait "$pid" || status=$?
test "$status" = 143
test ! -e "$root/interrupted.txt"
echo "Offline $device smoke passed"
