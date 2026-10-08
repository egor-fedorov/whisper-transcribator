#!/usr/bin/env bash
# Build-time headers/shader compiler only. Link against Ubuntu's redistributable loader.
set -euo pipefail
root=$1
pins=$(dirname "$0")/../vulkan-sdk.json
version=$(jq -er .version "$pins")
url=$(jq -er .linux.url "$pins")
digest=$(jq -er .linux.sha256 "$pins")
mkdir -p "$root"
curl --fail --location --retry 3 --proto '=https' --proto-redir '=https' "$url" -o "$root/sdk.tar.xz"
printf '%s  %s\n' "$digest" "$root/sdk.tar.xz" | sha256sum -c -
tar -xJf "$root/sdk.tar.xz" -C "$root"
mv "$root/$version/x86_64" "$root/sdk"
"$root/sdk/bin/glslc" --version
rm "$root/sdk.tar.xz"
