#!/usr/bin/env bash
# Build the relocatable macOS arm64 archive (CPU and Metal) on an Apple silicon Mac:
#   bash packaging/macos.sh OUTPUT_DIRECTORY
# Needs the Xcode Command Line Tools, CMake, Ninja, pkg-config, network access for the pinned
# sources and the test tools from CONTRIBUTING.md. Pass WT_SOURCE_REVISION and WT_SOURCE_DIRTY
# like the Docker recipe. Binaries get ad-hoc signatures, not a Developer ID.
set -euo pipefail
fail() {
    echo "$*" >&2
    exit 1
}
[[ $# = 1 ]] || fail 'Usage: macos.sh OUTPUT_DIRECTORY'
[[ $(uname -s) = Darwin && $(uname -m) = arm64 ]] || fail 'Build on an Apple silicon Mac'
repo=$(cd "$(dirname "$0")/.." && pwd)
mkdir -p "$1"
output=$(cd "$1" && pwd)
work="$repo/.build/macos-release"
build="$work/build"
bundle="$work/bundle"
# The oldest macOS the archive supports; CI runs it there.
export MACOSX_DEPLOYMENT_TARGET=14.0
# Keep macOS metadata (AppleDouble entries) out of tar archives.
export COPYFILE_DISABLE=1
rm -rf "$work"
# Room for the install names and rpaths rewritten below.
bash "$repo/packaging/ffmpeg.sh" "$work" "$work/media" --install-name-dir=@rpath \
    --extra-ldflags=-Wl,-headerpad_max_install_names
# Only the FFmpeg built above; libcurl then comes from the macOS SDK.
export PKG_CONFIG_LIBDIR="$work/media/lib/pkgconfig"
unset PKG_CONFIG_PATH
cmake -S "$repo" -B "$build" -G Ninja -DCMAKE_BUILD_TYPE=Release -DWT_WERROR=ON \
    -DGGML_NATIVE=OFF -DGGML_BACKEND_DL=ON -DGGML_CPU_ALL_VARIANTS=ON \
    -DWT_SOURCE_REVISION="${WT_SOURCE_REVISION:-}" -DWT_SOURCE_DIRTY="${WT_SOURCE_DIRTY:-}"
cmake --build "$build" -j"$(getconf _NPROCESSORS_ONLN)"
ctest --test-dir "$build" --output-on-failure

# shellcheck source=packaging/macos/runtime.sh
source "$repo/packaging/macos/runtime.sh"
wt_macos_stage_runtime "$work" "$build" "$bundle"

ffmpeg=$(find "$work" -maxdepth 1 -type d -name 'ffmpeg-[0-9]*' | head -n 1)
cp "$work/ffmpeg.tar.xz" "$bundle/sources/$(basename "$ffmpeg").tar.xz"
cp "$ffmpeg/COPYING.LGPLv2.1" "$bundle/licenses/FFmpeg-LGPL-2.1.txt"
cp "$ffmpeg/ffbuild/config.log" "$bundle/sources/ffmpeg-config.log"
# shellcheck disable=SC1091
source "$bundle/share/build-metadata.env"
cmake -DWT_SOURCE_DIR="$repo" -DWT_BUILD_DIR="$build" -DWT_BUNDLE_DIR="$bundle" \
    -P "$repo/packaging/cmake/sources.cmake"
"$bundle/bin/whisper-transcribator" --version
name="whisper-transcribator-$WT_PACKAGE_VERSION-$WT_TARGET_OS-$WT_TARGET_ARCH-metal.tar.gz"
rm -f "$output/$name" "$output/SHA256SUMS"
tar -C "$bundle" -czf "$output/$name" .
(cd "$output" && shasum -a 256 "$name" >SHA256SUMS)
echo "Archive: $output/$name"
