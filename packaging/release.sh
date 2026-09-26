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
inputs=(CMakeLists.txt CMakePresets.json cmake src tests packaging LICENSE)
git ls-tree -r --name-only HEAD -- "${inputs[@]}" | LC_ALL=C sort >"$temporary/expected"
assets=()
for flavor in cpu cuda; do
    name="whisper-transcribator-$version-linux-x86_64-$flavor.tar.gz"
    archive="$root/$flavor/$name"
    [[ -s $archive ]] || fail "Missing archive: $archive"
    digest=$(sha256sum <"$archive" | cut -d ' ' -f1)
    printf '%s  %s\n' "$digest" "$name" >"$temporary/$flavor.sha"
    cmp "$temporary/$flavor.sha" "$root/$flavor/SHA256SUMS" || fail "Checksum mismatch: $flavor"
    tar -xOzf "$archive" "./sources/whisper-transcribator-$version.tar.gz" >"$temporary/source.tar.gz"
    tar -tzf "$temporary/source.tar.gz" | sed '/\/$/d' | LC_ALL=C sort >"$temporary/actual"
    cmp "$temporary/expected" "$temporary/actual" || fail "Source file list differs from HEAD: $flavor"
    while IFS= read -r file; do
        git show "HEAD:$file" >"$temporary/expected-file"
        tar -xOzf "$temporary/source.tar.gz" "$file" >"$temporary/actual-file"
        cmp "$temporary/expected-file" "$temporary/actual-file" || fail "Source differs from HEAD: $flavor/$file"
    done <"$temporary/expected"
    assets+=("$archive")
done
echo "CPU/CUDA checksums and packaged sources match $commit"
[[ $mode = --draft ]] || exit 0

tag="v$version"
[[ $(git rev-parse "$tag^{commit}") = "$commit" ]] || fail 'Local tag must point to HEAD'
repo=$(gh repo view --json nameWithOwner --jq .nameWithOwner)
[[ $(gh api "repos/$repo/commits/$tag" --jq .sha) = "$commit" ]] || fail 'Remote tag differs from HEAD'
run=$(gh run list --repo "$repo" --workflow ci.yml --branch main --event push \
    --commit "$commit" --status success --limit 1 --json databaseId --jq '.[0].databaseId // empty')
[[ $run =~ ^[0-9]+$ ]] || fail 'No successful main CI run for this commit'
cat "$temporary/cpu.sha" "$temporary/cuda.sha" >"$temporary/SHA256SUMS"
cp "$notes" "$temporary/notes.md"
# Markdown backticks are literal, not shell command substitutions.
# shellcheck disable=SC2016
printf '\nSource commit: `%s`. [Successful CI](https://github.com/%s/actions/runs/%s).\n' \
    "$commit" "$repo" "$run" >>"$temporary/notes.md"
gh release create "$tag" --repo "$repo" --verify-tag --target "$commit" --draft \
    --title "$tag" --notes-file "$temporary/notes.md" "${assets[@]}" "$temporary/SHA256SUMS"
echo "Draft prepared. Review assets and GPU verification before explicitly publishing $tag."
