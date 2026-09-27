#!/usr/bin/env bash
set -euo pipefail
binary=$1
root=$2
mkdir -p "$root"
curl --fail --location --proto '=https' --proto-redir '=https' \
    https://raw.githubusercontent.com/ggml-org/whisper.cpp/927cfce34f31707e17f2bff35c349632fb9e2c3a/samples/jfk.wav \
    -o "$root/jfk.wav"
# macOS provides shasum rather than GNU sha256sum.
checksum=(sha256sum)
if ! command -v sha256sum >/dev/null; then checksum=(shasum -a 256); fi
printf '%s  %s\n' 59dfb9a4acb36fe2a2affc14bacbee2920ff435cb13cc314a08c13f66ba7860e "$root/jfk.wav" |
    "${checksum[@]}" -c -
"$binary" models download tiny --download-root "$root"
"$binary" models download silero-v6.2.0 --download-root "$root"
