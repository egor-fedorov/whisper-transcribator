#!/usr/bin/env bash
set -euo pipefail
root=$(mktemp -d)
trap 'rm -rf "$root"' EXIT
if command -v cygpath >/dev/null; then
    root=$(cygpath -m "$root")
    export MSYS_NO_PATHCONV=1
fi
"$2" req -x509 -newkey rsa:2048 -nodes -days 1 -subj /CN=127.0.0.1 \
    -addext subjectAltName=IP:127.0.0.1 -keyout "$root/key.pem" -out "$root/cert.pem" 2>/dev/null
"$1" "$root/cert.pem" "$root/key.pem"
