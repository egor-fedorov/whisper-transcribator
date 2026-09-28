#!/usr/bin/env bash
# Run in MSYS2 with an inherited x64 MSVC developer environment.
set -euo pipefail
repo=$(cd "$(dirname "$0")/.." && pwd)
work="$repo/.build/windows-ffmpeg"
compiler_dir=$(dirname "$(command -v cl.exe)")
export PATH="$compiler_dir:$PATH"
if [[ ! -f $work/prefix/lib/pkgconfig/libavcodec.pc ]]; then
    bash "$repo/packaging/ffmpeg.sh" "$work" "$(cygpath -m "$work/prefix")" \
        --toolchain=msvc --arch=x86_64 --extra-cflags=-MD
fi
