#!/usr/bin/env bash
# Fresh Ubuntu runtime, no SDK, GPU, models or inference. Check the exact archive.
set -euo pipefail
root=$(realpath "$1")
bash "$(dirname "$0")/package.sh" "$root" vulkan
binary="$root/unpacked/bin/whisper-transcribator"
export VK_DRIVER_FILES="$root/missing-icd.json" VK_ICD_FILENAMES="$root/missing-icd.json"
for device in cpu auto; do
    "$binary" doctor --device "$device" --json >"$root/$device.json"
    jq -e '.device == "cpu" and .errors == []' "$root/$device.json"
done
status=0
"$binary" doctor --device vulkan --json >"$root/vulkan.json" || status=$?
test "$status" = 1
jq -e '.errors | any(contains("Vulkan requested but unavailable"))' "$root/vulkan.json"
echo 'Exact Vulkan archive remains usable on CPU and rejects unavailable Vulkan'
