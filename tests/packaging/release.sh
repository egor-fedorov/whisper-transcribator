#!/usr/bin/env bash
set -euo pipefail
helper=$(realpath "$(dirname "$0")/../../packaging/release.sh")
root=$(mktemp -d)
trap 'rm -rf "$root"' EXIT
mkdir -p "$root/repo" "$root/bin"
export GH_TEST_LOG="$root/gh.log"
# Variables must expand in the mock process, not while generating it.
# shellcheck disable=SC2016
printf '%s\n' '#!/usr/bin/env bash' 'set -euo pipefail' \
    'case "$1 $2" in' \
    '  "repo view") echo example/project ;;' \
    '  "api repos/example/project/commits/v0.3.0") echo "${GH_TEST_REMOTE_SHA:-$GH_TEST_COMMIT}" ;;' \
    '  "run list") if [[ ${GH_TEST_NO_CI:-0} = 0 ]]; then echo 123; fi ;;' \
    '  "release create") printf "%s\n" "$@" >"$GH_TEST_LOG" ;;' \
    '  *) echo "Unexpected gh call: $*" >&2; exit 1 ;;' \
    'esac' >"$root/bin/gh"
chmod +x "$root/bin/gh"
export PATH="$root/bin:$PATH"
cd "$root/repo"
git init -q -b main
git config user.name 'Release Test'
git config user.email 'test@users.noreply.github.com'
git config commit.gpgsign false
git config tag.gpgsign false
git config core.hooksPath /dev/null
mkdir -p cmake src tests packaging docs/releases
for directory in cmake tests packaging; do
    printf 'fixture\n' >"$directory/fixture"
done
printf 'project(whisper_transcribator VERSION 0.3.0 LANGUAGES C CXX)\n' >CMakeLists.txt
printf '{}\n' >CMakePresets.json
printf 'fixture\n' >LICENSE
printf 'source\n' >src/main.cpp
printf '# Release\n' >docs/releases/0.3.0.md
git add .
git commit -qm 'Fixture'
assets="$root/artifacts with spaces"
build_assets() {
    local flavor name
    mkdir -p "$root/bundle/sources" "$root/bundle/share"
    printf 'WT_PACKAGE_VERSION=0.3.0\nWT_SOURCE_REVISION=%s\nWT_SOURCE_DIRTY=false\n' \
        "$(git rev-parse HEAD)" >"$root/bundle/share/build-metadata.env"
    git archive HEAD CMakeLists.txt CMakePresets.json cmake src tests packaging LICENSE \
        -o "$root/bundle/sources/whisper-transcribator-0.3.0.tar.gz"
    for flavor in cpu cuda; do
        mkdir -p "$assets/$flavor"
        name="whisper-transcribator-0.3.0-linux-x86_64-$flavor.tar.gz"
        tar -C "$root/bundle" -czf "$assets/$flavor/$name" .
        (cd "$assets/$flavor" && sha256sum "$name" >SHA256SUMS)
    done
}
repack_assets() {
    local flavor name
    for flavor in cpu cuda; do
        name="whisper-transcribator-0.3.0-linux-x86_64-$flavor.tar.gz"
        tar -C "$root/bundle" -czf "$assets/$flavor/$name" .
        (cd "$assets/$flavor" && sha256sum "$name" >SHA256SUMS)
    done
}
reject() {
    local message=$1
    shift
    if bash "$helper" "$@" >"$root/failure.log" 2>&1; then
        echo "Unexpected release success: $*" >&2
        exit 1
    fi
    grep -Fq "$message" "$root/failure.log"
    test ! -e "$GH_TEST_LOG"
}
build_assets
bash "$helper" 0.3.0 "$assets"
test ! -e "$GH_TEST_LOG"
printf 'WT_PACKAGE_VERSION=0.3.0-dev\n' >"$root/bundle/share/build-metadata.env"
repack_assets
reject 'Refusing a development' 0.3.0 "$assets"
build_assets
printf 'tampered packaged source\n' >src/main.cpp
tar -czf "$root/bundle/sources/whisper-transcribator-0.3.0.tar.gz" CMakeLists.txt CMakePresets.json cmake src tests packaging LICENSE
git show HEAD:src/main.cpp >src/main.cpp
repack_assets
reject 'Source differs' 0.3.0 "$assets"
build_assets
reject 'Expected --check or --draft' 0.3.0 "$assets" --publish
reject 'Expected a numeric X.Y.Z version' '../0.3.0' "$assets"
reject 'Version differs' 0.3.1 "$assets"
printf 'broken checksum\n' >"$assets/cpu/SHA256SUMS"
reject 'Checksum mismatch' 0.3.0 "$assets" --draft
build_assets
printf 'changed source\n' >src/main.cpp
reject 'Commit changes' 0.3.0 "$assets" --draft
git add src/main.cpp
git commit -qm 'Change source'
reject 'Archive revision differs' 0.3.0 "$assets" --draft
build_assets
reject 'Local tag must point to HEAD' 0.3.0 "$assets" --draft
git tag v0.3.0
export GH_TEST_COMMIT
GH_TEST_COMMIT=$(git rev-parse HEAD)
export GH_TEST_REMOTE_SHA=0000000000000000000000000000000000000000
reject 'Remote tag differs' 0.3.0 "$assets" --draft
unset GH_TEST_REMOTE_SHA
export GH_TEST_NO_CI=1
reject 'No successful main CI run' 0.3.0 "$assets" --draft
unset GH_TEST_NO_CI
bash "$helper" 0.3.0 "$assets" --draft
grep -Fxq -- '--draft' "$GH_TEST_LOG"
grep -Fxq -- '--verify-tag' "$GH_TEST_LOG"
grep -Fxq -- "$assets/cpu/whisper-transcribator-0.3.0-linux-x86_64-cpu.tar.gz" "$GH_TEST_LOG"
echo 'Release preparation checks passed'
