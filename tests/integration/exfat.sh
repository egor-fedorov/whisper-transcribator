#!/usr/bin/env bash
# Repeats publication, checkpoint and model-cache checks on exFAT disk images. Linux mounts them
# through exfat-fuse and requires root or passwordless sudo, losetup, mkfs.exfat and
# mount.exfat-fuse; macOS attaches them with hdiutil and needs no privileges.
set -Eeuo pipefail
# Diagnostics use the original stderr: Bash 3.2 runs traps with the failed command's redirections.
exec 3>&2
build=$(realpath "$1")
fixtures=$(realpath "$2")
mkdir -p "$3"
root=$(realpath "$3")
as_root() {
    if [ "$(id -u)" = 0 ]; then "$@"; else sudo "$@"; fi
}
# Mount points, innermost first, and Linux loop devices.
mounts=()
devices=()
cleanup() {
    local status=$? target device log
    if ((status != 0)); then
        for log in "$root"/*.log; do
            if [[ -f $log ]]; then printf '== %s\n' "$log" >&3; tail -n 20 "$log" >&3; fi
        done
    fi
    for target in ${mounts[@]+"${mounts[@]}"}; do
        if [[ $(uname) == Darwin ]]; then
            hdiutil detach -quiet "$target" || hdiutil detach -quiet -force "$target" || true
        else
            as_root umount "$target" || true
        fi
    done
    for device in ${devices[@]+"${devices[@]}"}; do as_root losetup --detach "$device" || true; done
}
trap cleanup EXIT
trap 'echo "exfat.sh: command failed at line $LINENO" >&3' ERR
mount_exfat() {
    local image=$1 target=$2 device
    mkdir -p "$target"
    if [[ $(uname) == Darwin ]]; then
        # Like a USB drive on macOS: entries report the current user as owner and mode 0777.
        hdiutil create -size 64m -fs ExFAT -volname WT -layout NONE "$image.dmg" >/dev/null
        hdiutil attach -nobrowse -mountpoint "$target" "$image.dmg" >/dev/null
    else
        # Like a drive attached for another user: entries report owner nobody and mode 0777.
        truncate --size 64M "$image.img"
        mkfs.exfat "$image.img" >/dev/null
        device=$(as_root losetup --find --show "$image.img")
        devices+=("$device")
        as_root mount.exfat-fuse -o allow_other,uid=65534,gid=65534,umask=0 "$device" "$target"
    fi
    mounts=("$target" ${mounts[@]+"${mounts[@]}"})
}
disk="$root/disk"
mount_exfat "$root/disk-image" "$disk"

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
mount_exfat "$root/nested-image" "$disk/nested/.whisper-transcribator"
status=0
transcribe --output-dir "$disk/nested" 2>"$root/nested.log" || status=$?
test "$status" = 1
grep -q 'Checkpoint directory must be owned by you with mode 0700' "$root/nested.log"
test ! -e "$disk/nested/jfk.txt"
echo "exFAT publication and checkpoint checks passed"
