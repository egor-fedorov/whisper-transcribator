#!/usr/bin/env bash
# Verify a CUDA export using a fresh container and narrowly scoped bind mounts.
set -euo pipefail
[[ $# = 1 ]] || { echo 'Usage: check-cuda.sh CUDA_EXPORT_DIRECTORY' >&2; exit 1; }
root=$(realpath "$1")
repo=$(git rev-parse --show-toplevel)
revision=$(git rev-parse HEAD)
temporary=$(mktemp -d)
image="wt-cuda-check:$(basename "$temporary" | tr '[:upper:]' '[:lower:]')"
container=${image/:/-}
cleanup() {
    docker container rm -f "$container" >/dev/null 2>&1 || true
    docker image rm "$image" >/dev/null 2>&1 || true
    mkdir -p "$root/diagnostics"
    cp -a "$temporary/results/." "$root/diagnostics/" 2>/dev/null || true
    rm -rf "$temporary"
}
trap cleanup EXIT
rm -f "$root/cuda/cuda-verification.json"
archives=("$root"/cuda/whisper-transcribator-*-linux-x86_64-cuda.tar.gz)
[[ ${#archives[@]} = 1 && -s ${archives[0]} ]]
archive=${archives[0]}
digest=$(sha256sum <"$archive" | cut -d ' ' -f1)
printf '%s  %s\n' "$digest" "$(basename "$archive")" >"$temporary/checksum"
cmp "$temporary/checksum" "$root/cuda/SHA256SUMS"
mkdir -p "$temporary"/{bundle,checks,fixtures,results}
tar -xzf "$archive" -C "$temporary/bundle"
metadata="$temporary/bundle/share/build-metadata.env"
grep -Fxq "WT_SOURCE_REVISION=$revision" "$metadata"
grep -Fxq 'WT_SOURCE_DIRTY=false' "$metadata"
grep -Fxq 'WT_TARGET_OS=linux' "$metadata"
grep -Fxq 'WT_TARGET_ARCH=x86_64' "$metadata"
cp -a "$root/tools" "$temporary/tools"
cp "$repo"/tests/smoke/{prepare-smoke,smoke,streaming-smoke,cuda-release}.sh "$temporary/checks/"
docker build -f "$repo/tests/packaging/cuda.Dockerfile" -t "$image" "$temporary"
common=(--rm --name "$container" --user "$(id -u):$(id -g)" --cap-drop ALL --security-opt no-new-privileges
    --read-only --tmpfs '/tmp:rw,nosuid,nodev,size=256m')
# Only public fixture/model preparation can use the network; no GPU is needed here.
docker run "${common[@]}" -v "$temporary/fixtures:/fixtures" "$image" \
    bash /checks/prepare-smoke.sh /opt/whisper-transcribator/bin/whisper-transcribator /fixtures
docker run "${common[@]}" --gpus all --network none \
    -v "$temporary/fixtures:/fixtures:ro" -v "$temporary/results:/results" "$image" \
    bash /checks/cuda-release.sh
jq -n --arg archive "$(basename "$archive")" --arg digest "$digest" --arg revision "$revision" \
    --slurpfile doctor "$temporary/results/doctor.json" \
    '{schema_version: 1, archive: $archive, sha256: $digest, source_revision: $revision,
      device: "cuda", checks: {smoke: true, streaming_resume: true, offline: true}, doctor: $doctor[0]}' \
    >"$temporary/cuda-verification.json"
bash "$repo/packaging/release/verify-cuda.sh" "$archive" "$temporary/cuda-verification.json" "$revision"
mv "$temporary/cuda-verification.json" "$root/cuda/cuda-verification.json"
echo 'Exact CUDA archive passed offline hardware inference and interrupted resume'
