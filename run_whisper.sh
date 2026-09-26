#!/usr/bin/env bash
set -euo pipefail
# Compatibility launcher; discovery, numbering and workers belong to the CLI.
export WHISPER_IMAGE="${CONTAINER:-whisper-transcribator}"
export WHISPER_MODEL="${MODEL:-large-v3-turbo}"
export WHISPER_CPU_THREADS="${THREADS:-1}"
export WHISPER_DEVICE="${DEVICE:-cpu}"
export WHISPER_COMPUTE_TYPE="${COMPUTE_TYPE:-auto}"
export WHISPER_OVERWRITE="${OVERWRITE:-0}"
if [[ -n "${MAP_FILE:-}" ]]; then
    echo 'MAP_FILE is no longer supported; use --output-dir and --prefix.' >&2
    exit 2
fi
exec "$(dirname "$0")/transcribe_dir.sh" "$PWD" \
    --jobs "${JOBS:-1}" --naming numbered --prefix "${PREFIX:-result}" \
    --continue-on-error "$@"
