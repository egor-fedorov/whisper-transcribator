#!/usr/bin/env bash
# Compatibility entrypoint for the documented CUDA export command.
set -euo pipefail
exec bash "$(dirname "$0")/check-gpu.sh" cuda "$@"
