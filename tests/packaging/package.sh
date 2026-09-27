#!/usr/bin/env bash
# Verify the archive for this machine without models or speech recognition: Linux CPU archives
# and the macOS arm64 archive. Use a fresh directory.
set -euo pipefail
case "$(uname -s)-$(uname -m)" in
    Linux-x86_64) os=linux arch=x86_64 flavor=cpu baseline=libggml-cpu-x64.so ;;
    Linux-aarch64) os=linux arch=aarch64 flavor=cpu baseline=libggml-cpu-armv8.0_1.so ;;
    Darwin-arm64) os=macos arch=arm64 flavor=metal baseline=libggml-cpu-apple_m1.so ;;
    *) echo "Unsupported archive platform: $(uname -s) $(uname -m)" >&2; exit 1 ;;
esac
cd "$1"
if [[ $os == macos ]]; then shasum -a 256 -c SHA256SUMS; else sha256sum -c SHA256SUMS; fi
set -- whisper-transcribator-*-"$os-$arch-$flavor".tar.gz
test "$#" = 1
mkdir unpacked
tar -xzf "$1" -C unpacked
# shellcheck disable=SC1091
source unpacked/share/build-metadata.env
test "$WT_TARGET_OS" = "$os"
test "$WT_TARGET_ARCH" = "$arch"
test "$1" = "whisper-transcribator-${WT_PACKAGE_VERSION}-$os-$arch-$flavor.tar.gz"
test "$(unpacked/bin/whisper-transcribator --version)" = "$WT_PACKAGE_VERSION"
test -s unpacked/share/backends.txt
test -s "unpacked/lib/$baseline"
: > dependencies.log
if [[ $os == macos ]]; then
    test -s unpacked/lib/libggml-metal.so
    for binary in unpacked/bin/whisper-transcribator unpacked/lib/*; do
        otool -L "$binary" >> dependencies.log
        codesign --verify --strict "$binary"
        # Every file supports the same oldest macOS as the archive.
        otool -l "$binary" | awk '$1 == "minos" { found = 1; if ($2 != "14.0") wrong = 1 }
            END { exit wrong || !found }' || { echo "Unexpected minimum macOS: $binary" >&2; exit 1; }
    done
    # Only archive libraries through @rpath and libraries that are part of macOS.
    awk '!/:$/ { print $1 }' dependencies.log | sort -u | while IFS= read -r library; do
        case $library in
            /usr/lib/* | /System/Library/*) ;;
            @rpath/*) test -f "unpacked/lib/${library#@rpath/}" || { echo "Missing: $library" >&2; exit 1; } ;;
            *) echo "Dependency outside the archive and macOS: $library" >&2; exit 1 ;;
        esac
    done
else
    for binary in unpacked/bin/whisper-transcribator unpacked/lib/*.so*; do
        if ! ldd "$binary" >> dependencies.log 2>&1; then
            cat dependencies.log >&2
            exit 1
        fi
    done
    if grep -q 'not found' dependencies.log; then
        cat dependencies.log >&2
        exit 1
    fi
fi
unpacked/bin/whisper-transcribator doctor --device cpu --json > doctor.json
jq -e '.device == "cpu" and .errors == [] and .cpu_backend != null' doctor.json
echo "Archive integrity, $os $arch runtime dependencies and doctor passed (no inference)"
