#!/usr/bin/env bash
# Repeats publication, checkpoint and model-cache checks on loop-mounted exFAT images through
# exfat-fuse. Requires root or passwordless sudo, losetup, mkfs.exfat and mount.exfat-fuse.
set -euo pipefail
build=$(realpath "$1")
fixtures=$(realpath "$2")
mkdir -p "$3"
root=$(realpath "$3")
as_root() {
    if [ "$(id -u)" = 0 ]; then "$@"; else sudo "$@"; fi
}
mounts=()
devices=()
cleanup() {
    for target in "${mounts[@]}"; do as_root umount "$target" || true; done
    for device in "${devices[@]}"; do as_root losetup --detach "$device" || true; done
}
trap cleanup EXIT
# Mounted like a drive attached for another user: entries report owner nobody and mode 0777.
mount_exfat() {
    local image=$1 target=$2 device
    truncate --size 64M "$image"
    mkfs.exfat "$image" >/dev/null
    device=$(as_root losetup --find --show "$image")
    devices+=("$device")
    mkdir -p "$target"
    as_root mount.exfat-fuse -o allow_other,uid=65534,gid=65534,umask=0 "$device" "$target"
    mounts=("$target" "${mounts[@]}")
}
disk="$root/disk"
mount_exfat "$root/disk.img" "$disk"

# Fixtures are created on the mount through TMPDIR. Checks that plant symlinks or hard links are
# omitted because exFAT cannot create them.
for name in io permissionless model-cache model-concurrency pipeline publication recovery; do
    TMPDIR="$disk" "$build/tests/wt-$name-tests"
done

transcribe() {
    "$build/whisper-transcribator" "$fixtures/jfk.wav" --model "$fixtures/ggml-tiny.bin" \
        --vad-model "$fixtures/ggml-silero-v6.2.0.bin" --language en --device cpu \
        --local-files-only --format all "$@"
}
transcribe --output-dir "$disk/transcripts" 2>"$root/first.log"
grep -q 'Checkpoint privacy cannot be enforced on this filesystem' "$root/first.log"
jq -e '.text | ascii_downcase | contains("country")' "$disk/transcripts/jfk.json" >/dev/null
status=0
transcribe --output-dir "$disk/transcripts" 2>"$root/existing.log" || status=$?
test "$status" = 1
grep -q 'Output exists' "$root/existing.log"
transcribe --output-dir "$disk/transcripts" --overwrite 2>"$root/overwrite.log"

# A checkpoint directory on another filesystem is never trusted on the output's behalf.
mkdir -p "$disk/nested"
mount_exfat "$root/nested.img" "$disk/nested/.whisper-transcribator"
status=0
transcribe --output-dir "$disk/nested" 2>"$root/nested.log" || status=$?
test "$status" = 1
grep -q 'Checkpoint directory must be owned by you with mode 0700' "$root/nested.log"
test ! -e "$disk/nested/jfk.txt"
echo "exFAT publication and checkpoint checks passed"
