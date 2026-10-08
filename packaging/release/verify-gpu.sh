#!/usr/bin/env bash
# Evidence is meaningful only when obtained from the trusted GPU job, not an arbitrary upload.
set -euo pipefail
[[ $# = 4 && $1 =~ ^(cuda|vulkan)$ ]] || { echo 'Usage: verify-gpu.sh cuda|vulkan ARCHIVE REPORT SOURCE_REVISION' >&2; exit 1; }
device=$1 archive=$2 report=$3 revision=$4
digest=$(sha256sum <"$archive" | cut -d ' ' -f1)
jq -e --arg device "$device" --arg archive "$(basename "$archive")" --arg digest "$digest" --arg revision "$revision" '
    .schema_version == 1 and .archive == $archive and .sha256 == $digest and
    .source_revision == $revision and .device == $device and
    .checks == {smoke: true, streaming_resume: true, offline: true} and
    .doctor.device == $device and .doctor.errors == [] and
    .doctor.source_revision == $revision and .doctor.source_dirty == false and
    (.doctor.selected_device.description | type == "string" and length > 0)
' "$report" >/dev/null || { echo "Missing or incompatible $device hardware verification for this archive" >&2; exit 1; }
