#!/usr/bin/env bash
set -euo pipefail
root=$(mktemp -d)
trap 'rm -rf "$root"' EXIT
openssl req -x509 -newkey rsa:2048 -nodes -days 1 -subj /CN=127.0.0.1 \
    -addext subjectAltName=IP:127.0.0.1 -keyout "$root/key.pem" -out "$root/cert.pem" 2>/dev/null
"$1" "$root/cert.pem" "$root/key.pem" "$root/data"
