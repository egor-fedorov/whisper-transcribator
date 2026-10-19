#!/usr/bin/env bash
# Model-free orchestration contract. Docker is a mock, not hardware verification.
set -euo pipefail
repo=$(git rev-parse --show-toplevel)
root=$(mktemp -d)
trap 'rm -rf "$root"' EXIT
export GPU_TEST_ROOT="$root" GPU_TEST_REVISION
GPU_TEST_REVISION=$(git rev-parse HEAD)
mkdir -p "$root"/{export/tools,bundle/share}
touch "$root/export/tools/wt-audio-fixture" "$root/export/tools/wt-process-runner"
docker() {
    printf '%q ' "$@" >>"$GPU_TEST_ROOT/docker.log"
    printf '\n' >>"$GPU_TEST_ROOT/docker.log"
    case "$1" in
        build)
            test -x "${!#}/tools/wt-audio-fixture"
            test -x "${!#}/tools/wt-process-runner"
            printf '%s' "${!#}" >"$GPU_TEST_ROOT/context" ;;
        run)
            if [[ " $* " == *' /checks/gpu-release.sh '* ]]; then
                [[ ${GPU_TEST_FAIL:-0} = 0 ]] || return 1
                local context
                context=$(<"$GPU_TEST_ROOT/context")
                jq -n --arg revision "$GPU_TEST_REVISION" --arg device "${!#}" \
                    '{device: $device, errors: [], source_revision: $revision, source_dirty: false,
                      selected_device: {description: "Synthetic GPU (test only)"}}' >"$context/results/doctor.json"
            fi ;;
        image | container) ;;
        *) echo "Unexpected Docker command: $*" >&2; return 1 ;;
    esac
}
export -f docker
for device in cuda vulkan; do
    mkdir -p "$root/export/$device"
    printf 'WT_SOURCE_REVISION=%s\nWT_SOURCE_DIRTY=false\nWT_TARGET_OS=linux\nWT_TARGET_ARCH=x86_64\nWT_PACKAGE_FLAVOR=%s\n' \
        "$GPU_TEST_REVISION" "$device" >"$root/bundle/share/build-metadata.env"
    archive="whisper-transcribator-0.5.0-linux-x86_64-$device.tar.gz"
    tar -C "$root/bundle" -czf "$root/export/$device/$archive" .
    (cd "$root/export/$device" && sha256sum "$archive" >SHA256SUMS)
    # Verification consumes a downloaded copy. Publication retains the original archive
    # and receives only the report, never a second upload of the archive or test helpers.
    downloaded="$root/downloaded-$device"
    mkdir -p "$downloaded" "$root/evidence-$device" "$root/draft/$device"
    cp -a "$root/export/$device" "$root/export/tools" "$downloaded/"
    chmod -x "$downloaded/tools/"*
    bash "$repo/packaging/release/check-gpu.sh" "$device" "$downloaded"
    test ! -x "$downloaded/tools/wt-audio-fixture"
    test ! -x "$downloaded/tools/wt-process-runner"
    test ! -e "$root/export/$device/$device-verification.json"
    cp "$downloaded/$device/$device-verification.json" "$root/evidence-$device/"
    cp "$root/export/$device/$archive" "$root/export/$device/SHA256SUMS" "$root/draft/$device/"
    cp "$root/evidence-$device/$device-verification.json" "$root/draft/$device/"
    cmp "$root/export/$device/$archive" "$root/draft/$device/$archive"
    bash "$repo/packaging/release/verify-gpu.sh" "$device" "$root/draft/$device/$archive" \
        "$root/draft/$device/$device-verification.json" "$GPU_TEST_REVISION"
    printf 'tampered archive' >>"$root/draft/$device/$archive"
    if bash "$repo/packaging/release/verify-gpu.sh" "$device" "$root/draft/$device/$archive" \
        "$root/draft/$device/$device-verification.json" "$GPU_TEST_REVISION" >"$root/rejected.log" 2>&1; then
        echo 'Separate report accepted for a changed archive' >&2; exit 1
    fi
    grep -Fq "Missing or incompatible $device hardware verification" "$root/rejected.log"
    bash "$repo/packaging/release/verify-gpu.sh" "$device" "$root/export/$device/$archive" \
        "$downloaded/$device/$device-verification.json" "$GPU_TEST_REVISION"
    grep -Eq "run .*--gpus all --network none .*fixtures:/fixtures:ro .* /checks/gpu-release.sh $device" "$root/docker.log"
    grep -Eq 'run .*--cap-drop ALL --security-opt no-new-privileges --read-only' "$root/docker.log"
    test -s "$downloaded/diagnostics/doctor.json"
    # A failed rerun must remove previously successful evidence, not leave it publishable.
    if GPU_TEST_FAIL=1 bash "$repo/packaging/release/check-gpu.sh" "$device" "$downloaded"; then
        echo 'Failed hardware check unexpectedly succeeded' >&2; exit 1
    fi
    test ! -e "$downloaded/$device/$device-verification.json"
    test ! -e "$(<"$root/context")"
done
grep -Fq 'NVIDIA_DRIVER_CAPABILITIES=graphics\,utility' "$root/docker.log"
grep -Fq 'NVIDIA_DRIVER_CAPABILITIES=compute\,utility' "$root/docker.log"
echo 'CUDA/Vulkan verification orchestration checks passed (mock Docker only)'
