#!/usr/bin/env bash
# Model-free orchestration contract. Docker is a mock, not hardware verification.
set -euo pipefail
repo=$(git rev-parse --show-toplevel)
root=$(mktemp -d)
trap 'rm -rf "$root"' EXIT
export CUDA_TEST_ROOT="$root" CUDA_TEST_REVISION
CUDA_TEST_REVISION=$(git rev-parse HEAD)
mkdir -p "$root"/{export/cuda,export/tools,bundle/share}
printf 'WT_SOURCE_REVISION=%s\nWT_SOURCE_DIRTY=false\nWT_TARGET_OS=linux\nWT_TARGET_ARCH=x86_64\n' \
    "$CUDA_TEST_REVISION" >"$root/bundle/share/build-metadata.env"
archive=whisper-transcribator-0.5.0-linux-x86_64-cuda.tar.gz
tar -C "$root/bundle" -czf "$root/export/cuda/$archive" .
(cd "$root/export/cuda" && sha256sum "$archive" >SHA256SUMS)
docker() {
    printf '%q ' "$@" >>"$CUDA_TEST_ROOT/docker.log"
    printf '\n' >>"$CUDA_TEST_ROOT/docker.log"
    case "$1" in
        build) printf '%s' "${!#}" >"$CUDA_TEST_ROOT/context" ;;
        run)
            if [[ " $* " == *' /checks/cuda-release.sh '* ]]; then
                [[ ${CUDA_TEST_FAIL:-0} = 0 ]] || return 1
                local context
                context=$(<"$CUDA_TEST_ROOT/context")
                jq -n --arg revision "$CUDA_TEST_REVISION" \
                    '{device: "cuda", errors: [], source_revision: $revision, source_dirty: false,
                      selected_device: {description: "Synthetic GPU (test only)"}}' >"$context/results/doctor.json"
            fi ;;
        image | container) ;;
        *) echo "Unexpected Docker command: $*" >&2; return 1 ;;
    esac
}
export -f docker
bash "$repo/packaging/release/check-cuda.sh" "$root/export"
bash "$repo/packaging/release/verify-cuda.sh" "$root/export/cuda/$archive" \
    "$root/export/cuda/cuda-verification.json" "$CUDA_TEST_REVISION"
grep -E 'run .*--gpus all --network none .*fixtures:/fixtures:ro .* /checks/cuda-release.sh' "$root/docker.log"
grep -E 'run .*--cap-drop ALL --security-opt no-new-privileges --read-only' "$root/docker.log"
test -s "$root/export/diagnostics/doctor.json"
# A failed rerun must remove previously successful evidence, not leave it publishable.
if CUDA_TEST_FAIL=1 bash "$repo/packaging/release/check-cuda.sh" "$root/export"; then
    echo 'Failed hardware check unexpectedly succeeded' >&2; exit 1
fi
test ! -e "$root/export/cuda/cuda-verification.json"
test ! -e "$(<"$root/context")"
echo 'CUDA verification orchestration checks passed (mock Docker only)'
