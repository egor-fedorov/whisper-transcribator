#!/usr/bin/env bash
# Linux/NVIDIA, one mixed CUDA+Vulkan binary, only the pinned public 41-second fixture.
set -euo pipefail
binary=$(realpath "$1")
helper=$(realpath "$2")
sample=$(realpath "$3")
model=$(realpath "$4")
mkdir -p "$5"
root=$(realpath "$5")
repo=$(cd "$(dirname "$0")/../.." && pwd)
test "$(sha256sum "$sample" | cut -d' ' -f1)" = 59dfb9a4acb36fe2a2affc14bacbee2920ff435cb13cc314a08c13f66ba7860e
test "$(sha256sum "$model" | cut -d' ' -f1)" = "$(jq -r '.[] | select(.name == "small") | .sha256' "$repo/src/models/catalog.json")"
test ! -e "$root/metrics.tsv" # Use a fresh directory; never mix measurements from two builds.
"$helper" --repeat "$sample" "$root/sample.wav"
gpu=${WT_BENCH_GPU:-0}
nvidia-smi --id="$gpu" --query-gpu=name,driver_version,memory.total --format=csv >"$root/gpu.csv"
lscpu >"$root/cpu.txt"
uname -a >"$root/os.txt"
sampler=
trap 'if [[ -n $sampler ]]; then kill "$sampler" 2>/dev/null || true; wait "$sampler" 2>/dev/null || true; fi' EXIT
devices=(cpu cuda vulkan)
for device in "${devices[@]}"; do
    "$binary" doctor --device "$device" --json >"$root/doctor-$device.json"
done
printf 'device\tround\tseconds\trss_kib\tvram_baseline_mib\tvram_peak_mib\n' >"$root/metrics.tsv"
measure() {
    local device=$1 round=$2 prefix baseline peak
    prefix="$root/$device-$round"
    baseline=$(nvidia-smi --id="$gpu" --query-gpu=memory.used --format=csv,noheader,nounits)
    nvidia-smi --id="$gpu" --query-gpu=memory.used --format=csv,noheader,nounits --loop-ms=100 >"$prefix.vram.log" &
    sampler=$!
    /usr/bin/time -f '%e\t%M' -o "$prefix.time" "$binary" "$root/sample.wav" \
        --model "$model" --device "$device" --local-files-only --language en --beam-size 1 \
        --cpu-threads 2 --no-vad --chunk-seconds 120 --format json -o "$prefix.json" \
        --verbose >"$prefix.stdout.log" 2>"$prefix.stderr.log"
    kill "$sampler"
    wait "$sampler" 2>/dev/null || true
    sampler=
    jq -e --arg device "$device" '.run.device == $device and .duration == 41
        and (.segments | length > 0) and (.text | ascii_downcase | contains("country"))
        and all(.segments[]; .start >= 0 and .end >= .start and .end <= 41)' "$prefix.json" >/dev/null
    peak=$(awk 'BEGIN {max=0} /^[0-9]+$/ {if ($1 > max) max=$1; found=1} END {if (!found) exit 1; print max}' "$prefix.vram.log")
    printf '%s\t%s\t%s\t%s\t%s\n' "$device" "$round" "$(<"$prefix.time")" "$baseline" "$peak" | tee -a "$root/metrics.tsv"
}
# Round zero warms model/file/shader caches and is reported separately, never in medians.
for device in "${devices[@]}"; do measure "$device" 0; done
for ((round=1; round<=3; ++round)); do
    for ((offset=0; offset<3; ++offset)); do
        measure "${devices[$(((round-1+offset)%3))]}" "$round"
    done
done
jq -Rn '[inputs | split("\t") | select(.[0] != "device") |
    {device: .[0], round: (.[1]|tonumber), seconds: (.[2]|tonumber),
     rss_kib: (.[3]|tonumber), vram_baseline_mib: (.[4]|tonumber), vram_peak_mib: (.[5]|tonumber)}] |
    {measurements: ., summary: ([.[] | select(.round > 0)] | group_by(.device) | map({
        device: .[0].device, median_seconds: (map(.seconds)|sort|.[1]),
        rtf: ((map(.seconds)|sort|.[1])/41), peak_rss_kib: (map(.rss_kib)|max),
        max_vram_increase_mib: (map(.vram_peak_mib - .vram_baseline_mib)|max)})),
     scope: "Warmed end-to-end CLI (hash/load included), small, 41 seconds, beam 1, two CPU threads, no VAD",
     vram: "Whole NVIDIA device sampled every 100 ms, includes desktop/other processes; not exact process peak"}' \
    <"$root/metrics.tsv" >"$root/comparison.json"
jq '.summary' "$root/comparison.json"
