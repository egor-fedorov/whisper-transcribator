#!/usr/bin/env bash
set -euo pipefail
if [[ ${1:-} == --help || ${1:-} == -h ]]; then
    echo 'Usage: transcribe_dir.sh DIRECTORY [transcribe options]'
    echo 'WHISPER_IMAGE, WHISPER_DEVICE, WHISPER_MODELS_VOLUME configure Docker.'
    exit 0
fi
[[ $# -gt 0 ]] || { echo 'A directory is required.' >&2; exit 2; }
directory=$(cd -- "$1" && pwd)
shift
device=${WHISPER_DEVICE:-cpu}
previous=''
for arg in "$@"; do
    [[ "$previous" != --device ]] || device=$arg
    case "$arg" in --device=*) device=${arg#*=} ;; esac
    previous=$arg
done
docker_args=()
case "$device" in
    cuda|auto)
        docker_args+=(--gpus all)
        for node in /dev/nvidia-uvm /dev/nvidia-uvm-tools; do
            [[ ! -c "$node" ]] || docker_args+=(--device "$node")
        done ;;
    cpu) ;;
    *) echo 'Device must be cpu, cuda or auto.' >&2; exit 2 ;;
esac
extra=()
case ${WHISPER_OVERWRITE:-0} in
    1) extra+=(--overwrite) ;; 0) ;; *) echo 'WHISPER_OVERWRITE must be 0 or 1.' >&2; exit 2 ;;
esac
exec "${DOCKER_BIN:-docker}" run --rm --init "${docker_args[@]}" \
    -v "$directory:/work" -v "${WHISPER_MODELS_VOLUME:-whisper-models}:/models" \
    "${WHISPER_IMAGE:-whisper-transcribator}" transcribe \
    --input-dir /work --output-dir /work --skip-existing \
    --model "${WHISPER_MODEL:-small}" --language "${WHISPER_LANGUAGE:-ru}" \
    --format "${WHISPER_FORMAT:-text}" --device "$device" \
    --compute-type "${WHISPER_COMPUTE_TYPE:-auto}" \
    --cpu-threads "${WHISPER_CPU_THREADS:-0}" --batch-size "${WHISPER_BATCH_SIZE:-1}" \
    "${extra[@]}" "$@"
