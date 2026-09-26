#!/usr/bin/env bash
set -euo pipefail
build=$1
destination=$2
mkdir -p "$destination/bin" "$destination/lib"
cp "$build/whisper-transcribator-native" "$destination/bin/"
# Bundle resolved runtime libraries, never the host's glibc or NVIDIA driver.
while IFS= read -r library; do
    case "$(basename "$library")" in
        libc.so.*|libm.so.*|libpthread.so.*|libdl.so.*|librt.so.*|ld-linux*|libcuda.so.*) continue ;;
    esac
    cp -L "$library" "$destination/lib/"
done < <(ldd "$build/whisper-transcribator-native" | awk '/=> \// { print $3 }' | sort -u)
# The loader, not Bash, expands ORIGIN.
# shellcheck disable=SC2016
patchelf --set-rpath '$ORIGIN/../lib' "$destination/bin/whisper-transcribator-native"
for library in "$destination"/lib/*; do
    # shellcheck disable=SC2016
    patchelf --set-rpath '$ORIGIN' "$library"
done
"$destination/bin/whisper-transcribator-native" --version
