#!/usr/bin/env bash
# Verify a CPU archive without models or speech recognition. Use a fresh directory.
set -euo pipefail
cd "$1"
sha256sum -c SHA256SUMS
set -- whisper-transcribator-*-linux-x86_64-cpu.tar.gz
test "$#" = 1
mkdir unpacked
tar -xzf "$1" -C unpacked
# shellcheck disable=SC1091
source unpacked/share/build-metadata.env
test "$1" = "whisper-transcribator-${WT_PACKAGE_VERSION}-linux-x86_64-cpu.tar.gz"
test "$(unpacked/bin/whisper-transcribator --version)" = "$WT_PACKAGE_VERSION"
test -s unpacked/share/backends.txt
test -s unpacked/lib/libggml-cpu-x64.so
: > dependencies.log
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
unpacked/bin/whisper-transcribator doctor --device cpu --json > doctor.json
jq -e '.device == "cpu" and .errors == []' doctor.json
echo 'Archive integrity, runtime dependencies and doctor passed (no inference)'
