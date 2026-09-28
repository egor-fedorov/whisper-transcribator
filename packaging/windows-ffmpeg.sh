#!/usr/bin/env bash
# Run in MSYS2 with a matching inherited MSVC developer environment.
set -euo pipefail
repo=$(cd "$(dirname "$0")/.." && pwd)
work="$repo/.build/windows-ffmpeg"
architecture=${1:-x64}
[[ ${VSCMD_ARG_TGT_ARCH:-} == "$architecture" ]] || { echo "Use a $architecture developer shell" >&2; exit 1; }
case "$architecture" in
    x64) options=(--arch=x86_64) ;;
    # Decoding-only C implementation avoids an extra gas-preprocessor toolchain.
    # Inference still uses ggml's ClangCL NEON implementation.
    arm64) options=(--arch=aarch64 --disable-asm) ;;
    *) echo "Unsupported Windows architecture: $architecture" >&2; exit 1 ;;
esac
compiler_dir=$(dirname "$(command -v cl.exe)")
export PATH="$compiler_dir:$PATH"
if [[ ! -f $work/prefix/lib/pkgconfig/libavcodec.pc ]]; then
    bash "$repo/packaging/ffmpeg.sh" "$work" "$(cygpath -m "$work/prefix")" \
        --toolchain=msvc --extra-cflags=-MD "${options[@]}"
fi
