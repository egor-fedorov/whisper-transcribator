#!/usr/bin/env bash
set -euo pipefail
binary=$1
root=$2
mkdir -p "$root"
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
grep -qx '0.3.0' "$root/stdout"
expect 2
expect 2 --jobs 2
expect 2 --compute-type int8
expect 2 --cpu-threads -1
expect 2 --beam-size 0
expect 2 --chunk-seconds 0
expect 2 --chunk-seconds 29
expect 2 --chunk-seconds 601
expect 2 --device magic
expect 2 models download
expect 2 models list tiny
expect 2 models download unknown --local-files-only
expect 1 models download tiny --local-files-only
expect 0 models list --json
grep -q 'large-v3-turbo' "$root/stdout"
test ! -e "$root/models"
expect 0 doctor --device cpu --json
grep -q 'whisper.cpp' "$root/stdout"
expect 1 "$root/missing.mp4" --local-files-only
mkdir -p "$root/empty"
expect 0 --input-dir "$root/empty" --local-files-only
printf media >"$root/input.mp4"
printf completed >"$root/input.txt"
expect 0 "$root/input.mp4" --skip-existing --local-files-only
expect 1 "$root/input.mp4" --local-files-only
printf 'CLI tests passed\n'
