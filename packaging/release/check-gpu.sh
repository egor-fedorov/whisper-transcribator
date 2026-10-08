#!/usr/bin/env bash
# Verify an exact GPU export using a fresh container and narrowly scoped bind mounts.
set -euo pipefail
[[ $# = 2 && $1 =~ ^(cuda|vulkan)$ ]] || { echo 'Usage: check-gpu.sh cuda|vulkan EXPORT_DIRECTORY' >&2; exit 1; }
device=$1
root=$(realpath "$2")
repo=$(git rev-parse --show-toplevel)
revision=$(git rev-parse HEAD)
temporary=$(mktemp -d)
image="wt-$device-check:$(basename "$temporary" | tr '[:upper:]' '[:lower:]')"
container=${image/:/-}
cleanup() {
    docker container rm -f "$container" >/dev/null 2>&1 || true
    docker image rm "$image" >/dev/null 2>&1 || true
    mkdir -p "$root/diagnostics"
    cp -a "$temporary/results/." "$root/diagnostics/" 2>/dev/null || true
    rm -rf "$temporary"
}
trap cleanup EXIT
rm -f "$root/$device/$device-verification.json"
archives=("$root/$device"/whisper-transcribator-*-linux-x86_64-"$device".tar.gz)
[[ ${#archives[@]} = 1 && -s ${archives[0]} ]]
archive=${archives[0]}
digest=$(sha256sum <"$archive" | cut -d ' ' -f1)
printf '%s  %s\n' "$digest" "$(basename "$archive")" >"$temporary/checksum"
cmp "$temporary/checksum" "$root/$device/SHA256SUMS"
mkdir -p "$temporary"/{bundle,checks,fixtures,results}
tar -xzf "$archive" -C "$temporary/bundle"
metadata="$temporary/bundle/share/build-metadata.env"
grep -Fxq "WT_SOURCE_REVISION=$revision" "$metadata"
grep -Fxq 'WT_SOURCE_DIRTY=false' "$metadata"
grep -Fxq 'WT_TARGET_OS=linux' "$metadata"
grep -Fxq 'WT_TARGET_ARCH=x86_64' "$metadata"
grep -Fxq "WT_PACKAGE_FLAVOR=$device" "$metadata"
cp -a "$root/tools" "$temporary/tools"
chmod +x "$temporary/tools/wt-audio-fixture" "$temporary/tools/wt-process-runner"
cp "$repo"/tests/smoke/{prepare-smoke,smoke,streaming-smoke,gpu-release}.sh "$temporary/checks/"
docker build -f "$repo/tests/packaging/gpu.Dockerfile" -t "$image" "$temporary"
common=(--rm --name "$container" --user "$(id -u):$(id -g)" --cap-drop ALL --security-opt no-new-privileges
    --read-only --tmpfs '/tmp:rw,nosuid,nodev,size=256m')
# Only public fixture/model preparation can use the network; no GPU is needed here.
docker run "${common[@]}" -v "$temporary/fixtures:/fixtures" "$image" \
    bash /checks/prepare-smoke.sh /opt/whisper-transcribator/bin/whisper-transcribator /fixtures
capabilities=compute,utility
if [[ $device == vulkan ]]; then capabilities=graphics,utility; fi
docker run "${common[@]}" --gpus all --network none -e "NVIDIA_DRIVER_CAPABILITIES=$capabilities" \
    -v "$temporary/fixtures:/fixtures:ro" -v "$temporary/results:/results" "$image" \
    bash /checks/gpu-release.sh "$device"
jq -n --arg archive "$(basename "$archive")" --arg digest "$digest" --arg revision "$revision" \
    --arg device "$device" \
    --slurpfile doctor "$temporary/results/doctor.json" \
    '{schema_version: 1, archive: $archive, sha256: $digest, source_revision: $revision,
      device: $device, checks: {smoke: true, streaming_resume: true, offline: true}, doctor: $doctor[0]}' \
    >"$temporary/verification.json"
bash "$repo/packaging/release/verify-gpu.sh" "$device" "$archive" "$temporary/verification.json" "$revision"
mv "$temporary/verification.json" "$root/$device/$device-verification.json"
echo "Exact $device archive passed offline hardware inference and interrupted resume"
