#!/usr/bin/env bash
# Run inside the Ubuntu build image. Never bundle glibc or NVIDIA's driver.
set -euo pipefail
# shellcheck source=packaging/linux/runtime.sh
source "$(dirname "$0")/linux/runtime.sh"
wt_linux_stage_runtime "$1" "$2"
"$2/bin/whisper-transcribator" --version
