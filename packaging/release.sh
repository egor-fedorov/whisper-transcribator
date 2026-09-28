#!/usr/bin/env bash
# Validate local release assets; remote changes require the explicit --draft flag.
set -euo pipefail
fail() { echo "$*" >&2; exit 1; }
[[ $# -ge 2 && $# -le 3 ]] || fail 'Usage: release.sh VERSION ARTIFACT_ROOT [--draft]'
version=$1
root=$(realpath "$2")
mode=${3:---check}
[[ $version =~ ^[0-9]+\.[0-9]+\.[0-9]+$ ]] || fail 'Expected a numeric X.Y.Z version'
[[ $mode = --check || $mode = --draft ]] || fail 'Expected --check or --draft'
cd "$(git rev-parse --show-toplevel)"
[[ -z $(git status --porcelain --untracked-files=normal) ]] || fail 'Commit changes before preparing a release'
grep -Fxq "project(whisper_transcribator VERSION $version LANGUAGES C CXX)" CMakeLists.txt || fail 'Version differs from CMakeLists.txt'
notes="docs/releases/$version.md"
[[ -f $notes ]] || fail "Missing release notes: $notes"
commit=$(git rev-parse HEAD)
temporary=$(mktemp -d)
trap 'rm -rf "$temporary"' EXIT
inputs=(CMakeLists.txt CMakePresets.json vcpkg.json .gitattributes cmake src tests packaging LICENSE)
git ls-tree -r --name-only HEAD -- "${inputs[@]}" | LC_ALL=C sort >"$temporary/expected"
assets=()
# Artifact directory, operating system, architecture and flavor of every published archive.
targets=(cpu:linux:x86_64:cpu cuda:linux:x86_64:cuda cpu-aarch64:linux:aarch64:cpu
    macos-arm64:macos:arm64:metal)
for target in "${targets[@]}"; do
    IFS=: read -r directory os arch flavor <<<"$target"
    name="whisper-transcribator-$version-$os-$arch-$flavor.tar.gz"
    archive="$root/$directory/$name"
    [[ -s $archive ]] || fail "Missing archive: $archive"
    digest=$(sha256sum <"$archive" | cut -d ' ' -f1)
    printf '%s  %s\n' "$digest" "$name" >"$temporary/$directory.sha"
    cmp "$temporary/$directory.sha" "$root/$directory/SHA256SUMS" || fail "Checksum mismatch: $directory"
    tar -xOzf "$archive" ./share/build-metadata.env >"$temporary/metadata"
    grep -Fxq "WT_PACKAGE_VERSION=$version" "$temporary/metadata" || fail "Refusing a development or mismatched archive: $directory"
    grep -Fxq "WT_SOURCE_REVISION=$commit" "$temporary/metadata" || fail "Archive revision differs from HEAD: $directory"
    grep -Fxq 'WT_SOURCE_DIRTY=false' "$temporary/metadata" || fail "Archive sources are dirty or unknown: $directory"
    grep -Fxq "WT_TARGET_OS=$os" "$temporary/metadata" || fail "Archive system differs from its name: $directory"
    grep -Fxq "WT_TARGET_ARCH=$arch" "$temporary/metadata" || fail "Archive architecture differs from its name: $directory"
    tar -xOzf "$archive" "./sources/whisper-transcribator-$version.tar.gz" >"$temporary/source.tar.gz"
    tar -tzf "$temporary/source.tar.gz" | sed '/\/$/d' | LC_ALL=C sort >"$temporary/actual"
    cmp "$temporary/expected" "$temporary/actual" || fail "Source file list differs from HEAD: $directory"
    while IFS= read -r file; do
        git show "HEAD:$file" >"$temporary/expected-file"
        tar -xOzf "$temporary/source.tar.gz" "$file" >"$temporary/actual-file"
        cmp "$temporary/expected-file" "$temporary/actual-file" || fail "Source differs from HEAD: $directory/$file"
    done <"$temporary/expected"
    assets+=("$archive")
    cat "$temporary/$directory.sha" >>"$temporary/SHA256SUMS"
done
echo "Linux x86_64 CPU/CUDA, Linux aarch64 CPU and macOS arm64 checksums and packaged sources match $commit"
[[ $mode = --draft ]] || exit 0

tag="v$version"
[[ $(git rev-parse "$tag^{commit}") = "$commit" ]] || fail 'Local tag must point to HEAD'
repo=$(gh repo view --json nameWithOwner --jq .nameWithOwner)
[[ $(gh api "repos/$repo/commits/$tag" --jq .sha) = "$commit" ]] || fail 'Remote tag differs from HEAD'
run=$(gh run list --repo "$repo" --workflow ci.yml --branch main --event push \
    --commit "$commit" --status success --limit 1 --json databaseId --jq '.[0].databaseId // empty')
[[ $run =~ ^[0-9]+$ ]] || fail 'No successful main CI run for this commit'
cp "$notes" "$temporary/notes.md"
# Markdown backticks are literal, not shell command substitutions.
# shellcheck disable=SC2016
printf '\nSource commit: `%s`. [Successful CI](https://github.com/%s/actions/runs/%s).\n' \
    "$commit" "$repo" "$run" >>"$temporary/notes.md"
gh release create "$tag" --repo "$repo" --verify-tag --target "$commit" --draft \
    --title "$tag" --notes-file "$temporary/notes.md" "${assets[@]}" "$temporary/SHA256SUMS"
echo "Draft prepared. Review assets and GPU verification before explicitly publishing $tag."
