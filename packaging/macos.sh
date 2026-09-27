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

# Libraries a Mach-O file loads, and its rpaths.
load_commands() {
    otool -l "$1" | awk '/cmd LC_(LOAD|LOAD_WEAK|REEXPORT|LOAD_UPWARD)_DYLIB$/ { load = 1 }
        load && $1 == "name" { print $2; load = 0 }'
}
rpaths() {
    otool -l "$1" | awk '/cmd LC_RPATH$/ { rpath = 1 } rpath && $1 == "path" { print $2; rpath = 0 }'
}
# Prints the build output a dependency refers to; returns 1 for macOS libraries, 2 on errors.
resolve() {
    local name=$1 directory
    case $name in
        /usr/lib/* | /System/Library/*) return 1 ;;
        @rpath/*) name=${name#@rpath/} ;;
        "$work"/*) name=$(basename "$name") ;;
        *)
            echo "Only macOS system libraries may be linked outside the archive: $1" >&2
            return 2
            ;;
    esac
    for directory in "$build/bin" "$work/media/lib"; do
        if [[ -e $directory/$name ]]; then
            printf '%s\n' "$directory/$name"
            return 0
        fi
    done
    echo "Cannot resolve dependency: $1" >&2
    return 2
}
mkdir -p "$bundle"/{bin,lib,licenses,sources,share}
cp "$build/whisper-transcribator" "$bundle/bin/"
cp "$build/generated/package.env" "$bundle/share/build-metadata.env"
while IFS= read -r module; do
    cp "$module" "$bundle/lib/"
    basename "$module" >>"$bundle/share/backends.txt"
done < <(find "$build/bin" -maxdepth 1 -type f -name 'libggml-*.so' | sort)
test -s "$bundle/share/backends.txt"
# Copy dependencies until the set is closed; references keep the file names they use.
changed=true
while $changed; do
    changed=false
    for file in "$bundle"/bin/whisper-transcribator "$bundle"/lib/*; do
        while IFS= read -r dependency; do
            status=0
            origin=$(resolve "$dependency") || status=$?
            case $status in
                0) ;;
                1) continue ;;
                *) exit 1 ;;
            esac
            target="$bundle/lib/$(basename "$dependency")"
            if [[ ! -e $target ]]; then
                cp -L "$origin" "$target"
                chmod u+w "$target"
                changed=true
            fi
        done < <(load_commands "$file")
    done
done
# One copy per library: a second one would load a separate instance.
duplicates=$(cd "$bundle/lib" && shasum -a 256 -- * | awk '{ print $1 }' | sort | uniq -d)
[[ -z $duplicates ]] || fail "Library bundled under several names: $duplicates"
for file in "$bundle"/bin/whisper-transcribator "$bundle"/lib/*; do
    changes=()
    while IFS= read -r dependency; do
        case $dependency in /usr/lib/* | /System/Library/*) continue ;; esac
        changes+=(-change "$dependency" "@rpath/$(basename "$dependency")")
    done < <(load_commands "$file")
    while IFS= read -r rpath; do
        changes+=(-delete_rpath "$rpath")
    done < <(rpaths "$file")
    case $file in
        */bin/*) changes+=(-add_rpath @loader_path/../lib) ;;
        *.dylib) changes+=(-id "@rpath/$(basename "$file")" -add_rpath @loader_path) ;;
        *) changes+=(-add_rpath @loader_path) ;;
    esac
    install_name_tool "${changes[@]}" "$file" 2>&1 | { grep -v 'will invalidate the code signature' || true; } >&2
    codesign --force --sign - "$file"
done
for file in "$bundle"/bin/whisper-transcribator "$bundle"/lib/*; do
    printf '# %s\n' "${file#"$bundle"/}"
    load_commands "$file"
done >"$bundle/share/linked-libraries.txt"

ffmpeg=$(find "$work" -maxdepth 1 -type d -name 'ffmpeg-[0-9]*' | head -n 1)
cp "$work/ffmpeg.tar.xz" "$bundle/sources/$(basename "$ffmpeg").tar.xz"
cp "$ffmpeg/COPYING.LGPLv2.1" "$bundle/licenses/FFmpeg-LGPL-2.1.txt"
cp "$ffmpeg/ffbuild/config.log" "$bundle/sources/ffmpeg-config.log"
# shellcheck disable=SC1091
source "$bundle/share/build-metadata.env"
tar -C "$build/_deps/whisper-src" -czf "$bundle/sources/whisper.cpp-$WT_WHISPER_REVISION.tar.gz" .
cp "$build/_deps/whisper-src/LICENSE" "$bundle/licenses/whisper.cpp-MIT.txt"
tar -C "$build/_deps/nlohmann_json-src" -czf "$bundle/sources/nlohmann-json-$WT_JSON_VERSION.tar.gz" .
cp "$build/_deps/nlohmann_json-src/LICENSE.MIT" "$bundle/licenses/nlohmann-json-MIT.txt"
tar -C "$build/_deps/cli11-src" -czf "$bundle/sources/CLI11-$WT_CLI11_VERSION.tar.gz" .
cp "$build/_deps/cli11-src/LICENSE" "$bundle/licenses/CLI11-BSD.txt"
cp "$repo/LICENSE" "$bundle/licenses/whisper-transcribator-MIT.txt"
(cd "$repo" && tar -czf "$bundle/sources/whisper-transcribator-$WT_PACKAGE_VERSION.tar.gz" \
    CMakeLists.txt CMakePresets.json cmake src tests packaging LICENSE)
"$bundle/bin/whisper-transcribator" --version
name="whisper-transcribator-$WT_PACKAGE_VERSION-$WT_TARGET_OS-$WT_TARGET_ARCH-metal.tar.gz"
rm -f "$output/$name" "$output/SHA256SUMS"
tar -C "$bundle" -czf "$output/$name" .
(cd "$output" && shasum -a 256 "$name" >SHA256SUMS)
echo "Archive: $output/$name"
