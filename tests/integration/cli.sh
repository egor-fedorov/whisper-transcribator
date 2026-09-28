#!/usr/bin/env bash
set -euo pipefail
binary=$1
# shellcheck disable=SC1090
source "$3"
version=$WT_PACKAGE_VERSION
revision=$4
mkdir -p "$2"
root=$(mktemp -d "$2/run-XXXXXX")
cleanup() {
    local status=$?
    if (( status == 0 )); then
        rm -rf "$root"
    else
        printf 'CLI fixtures: %s\n' "$root" >&2
    fi
}
trap cleanup EXIT
export WHISPER_DOWNLOAD_ROOT="$root/models"
unset WHISPER_MODEL
expect() {
    local expected=$1 status=0
    shift
    "$binary" "$@" >"$root/stdout" 2>"$root/stderr" || status=$?
    if [[ $status != "$expected" ]]; then
        cat "$root/stdout" "$root/stderr"
        echo "Expected $expected, got $status: $*" >&2
        exit 1
    fi
}
expect 0 --help
expect 0 --version
test "$(<"$root/stdout")" = "$version"
expect 2
expect 2 --jobs 2
expect 2 --compute-type int8
expect 2 --cpu-threads -1
expect 2 --beam-size 0
expect 2 --chunk-seconds 0
expect 2 --chunk-seconds 29
expect 2 --chunk-seconds 601
expect 2 --chunk-min-silence-ms -1
expect 2 --audio-stream -1
expect 2 --decode-errors invalid
expect 2 --decode-error-limit-seconds -1
expect 2 --decode-error-limit-seconds 0.5
expect 2 --text-layout invalid
expect 2 --paragraph-pause-ms -1
expect 2 --device magic
expect 2 --quiet --verbose
expect 2 models download
expect 2 models list tiny
expect 2 models download unknown --local-files-only
expect 1 models download tiny --local-files-only
expect 0 models list --json
grep -q 'large-v3-turbo' "$root/stdout"
test ! -e "$root/models"
expect 0 doctor --device cpu --json
grep -q 'whisper.cpp' "$root/stdout"
grep -q "$revision" "$root/stdout"
grep -Eq '"cpu_threads": [1-9][0-9]*' "$root/stdout"
grep -q '"ffmpeg"' "$root/stdout"
grep -q '"system_info"' "$root/stdout"
grep -q '"cpu_backend"' "$root/stdout"
grep -q '"available_devices"' "$root/stdout"
grep -q '"selected_device"' "$root/stdout"
grep -q '"gpu_index": -1' "$root/stdout"
grep -Fq "$WT_SOURCE_REVISION" "$root/stdout"
expect 0 doctor --device cpu --cpu-threads 3 --json
grep -q '"cpu_threads": 3' "$root/stdout"
expect 2 --timestamp-gaps invalid
grep -q 'timestamp-gaps' "$root/stderr"
expect 0 --help
grep -q 'vulkan' "$root/stdout"
grep -q -- '--timestamp-gaps' "$root/stdout"
grep -q -- '--decode-errors' "$root/stdout"
grep -q -- '--decode-error-limit-seconds' "$root/stdout"
expect 1 "$root/missing.mp4" --local-files-only
mkdir -p "$root/empty"
expect 0 --input-dir "$root/empty" --local-files-only
expect 0 --input-dir "$root/empty" --local-files-only --decode-errors tolerant --decode-error-limit-seconds 0
expect 0 --input-dir "$root/empty" --local-files-only --decode-errors strict
expect 0 --input-dir "$root/empty" --local-files-only --quiet
test ! -s "$root/stderr"
printf media >"$root/input.mp4"
printf completed >"$root/input.txt"
expect 0 "$root/input.mp4" --skip-existing --local-files-only
expect 1 "$root/input.mp4" --local-files-only
printf 'CLI tests passed\n'
