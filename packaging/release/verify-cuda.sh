#!/usr/bin/env bash
# Evidence is meaningful only when obtained from the trusted CUDA job, not an arbitrary upload.
set -euo pipefail
[[ $# = 3 ]] || { echo 'Usage: verify-cuda.sh ARCHIVE REPORT SOURCE_REVISION' >&2; exit 1; }
archive=$1 report=$2 revision=$3
digest=$(sha256sum <"$archive" | cut -d ' ' -f1)
jq -e --arg archive "$(basename "$archive")" --arg digest "$digest" --arg revision "$revision" '
    .schema_version == 1 and .archive == $archive and .sha256 == $digest and
    .source_revision == $revision and .device == "cuda" and
    .checks == {smoke: true, streaming_resume: true, offline: true} and
    .doctor.device == "cuda" and .doctor.errors == [] and
    .doctor.source_revision == $revision and .doctor.source_dirty == false and
    (.doctor.selected_device.description | type == "string" and length > 0)
' "$report" >/dev/null || { echo 'Missing or incompatible CUDA hardware verification for this archive' >&2; exit 1; }
