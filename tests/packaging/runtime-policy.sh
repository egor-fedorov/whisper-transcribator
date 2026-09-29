#!/usr/bin/env bash
# Model-free helper contracts. Native archive jobs still inspect actual ELF/Mach-O files.
set -euo pipefail
repo=$(cd "$(dirname "$0")/../.." && pwd)
# shellcheck source=packaging/linux/runtime.sh
source "$repo/packaging/linux/runtime.sh"
# shellcheck source=packaging/macos/runtime.sh
source "$repo/packaging/macos/runtime.sh"
temporary=$(mktemp -d)
trap 'rm -rf "$temporary"' EXIT
work="$temporary/work with spaces"
build="$work/build"
mkdir -p "$build/bin" "$build/generated" "$work/media/lib"
printf 'app\n' >"$build/whisper-transcribator"
printf 'metadata\n' >"$build/generated/package.env"
printf 'plugin\n' >"$build/bin/libggml-cpu.so"
printf 'library\n' >"$work/media/lib/libwhisper.dylib"
[[ $(wt_macos_resolve "$work" "$build" @rpath/libggml-cpu.so) = "$build/bin/libggml-cpu.so" ]]
[[ $(wt_macos_resolve "$work" "$build" "$work/media/lib/libwhisper.dylib") = "$work/media/lib/libwhisper.dylib" ]]
for path in /usr/lib/libSystem.B.dylib /System/Library/Frameworks/Metal.framework/Metal \
    /opt/homebrew/lib/external.dylib @rpath/missing.dylib; do
    status=0
    wt_macos_resolve "$work" "$build" "$path" >"$temporary/resolve.log" 2>&1 || status=$?
    case $path in
        /usr/lib/*|/System/Library/*) [[ $status = 1 && ! -s $temporary/resolve.log ]] ;;
        *) [[ $status = 2 ]]; grep -Eq 'outside the archive|Cannot resolve dependency' "$temporary/resolve.log" ;;
    esac
done

# Native commands are replaced only in this test process, not in production helpers.
otool() {
    [[ $1 = -l ]]
    printf 'cmd LC_LOAD_DYLIB\n    name /usr/lib/libSystem.B.dylib (offset 24)\n'
    case $2 in
        */whisper-transcribator|*/libggml-cpu.so)
            printf 'cmd LC_LOAD_WEAK_DYLIB\n    name @rpath/libwhisper.dylib (offset 24)\n' ;;
    esac
    printf 'cmd LC_RPATH\n    path /old/build/lib (offset 12)\n'
}
install_name_tool() { printf '%s\n' "$*" >>"$temporary/relocations"; }
codesign() { printf '%s\n' "$*" >>"$temporary/signatures"; }
wt_macos_stage_runtime "$work" "$build" "$work/bundle"
cmp "$work/media/lib/libwhisper.dylib" "$work/bundle/lib/libwhisper.dylib"
cmp "$build/generated/package.env" "$work/bundle/share/build-metadata.env"
[[ $(wc -l <"$temporary/signatures") -eq 3 ]]
grep -Fq -- '-add_rpath @loader_path/../lib' "$temporary/relocations"
grep -Fq -- '-id @rpath/libwhisper.dylib -add_rpath @loader_path' "$temporary/relocations"
grep -Fxq 'libggml-cpu.so' "$work/bundle/share/backends.txt"
grep -Fxq '@rpath/libwhisper.dylib' "$work/bundle/share/linked-libraries.txt"
cp "$build/bin/libggml-cpu.so" "$build/bin/libggml-duplicate.so"
# Run in a fresh shell so errexit stays enabled inside the helper under test.
export -f otool install_name_tool codesign
if bash -euc 'source "$1"; wt_macos_stage_runtime "$2" "$3" "$4"' -- \
    "$repo/packaging/macos/runtime.sh" "$work" "$build" "$work/duplicate" >"$temporary/failure.log" 2>&1; then
    echo 'Duplicate Mach-O library was accepted' >&2
    exit 1
fi
grep -Fq 'Library bundled under several names' "$temporary/failure.log"

dpkg-query() {
    [[ $1 = -S ]]
    case $2 in
        /usr/lib/fixture.so) printf 'fixture:amd64: /usr/lib/fixture.so\n' ;;
        *) return 1 ;;
    esac
}
ldd() { printf 'fixture.so => %s (0x1)\n' "$1"; }
[[ $(wt_linux_owner_of /lib/fixture.so) = fixture:amd64 ]]
[[ -z $(wt_linux_owner_of /missing/library.so) ]]
wt_linux_dependencies "$build/whisper-transcribator" "$build/bin/libggml-cpu.so" >"$temporary/links"
[[ $(grep -c '^# ' "$temporary/links") -eq 2 ]]
grep -Fxq "# $build/bin/libggml-cpu.so" "$temporary/links"
echo 'Runtime collection helper contracts passed'
