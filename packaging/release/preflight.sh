#!/usr/bin/env bash
# Read-only gate, executed on a hosted runner before scheduling the GPU job.
set -euo pipefail
fail() { echo "$*" >&2; exit 1; }
[[ $# = 2 ]] || fail 'Usage: preflight.sh REPOSITORY TAG'
repo=$1 tag=$2
[[ $repo =~ ^[A-Za-z0-9_.-]+/[A-Za-z0-9_.-]+$ ]] || fail 'Invalid repository'
[[ $tag =~ ^v[0-9]+\.[0-9]+\.[0-9]+$ ]] || fail 'Expected a vX.Y.Z release tag'
version=${tag#v}
revision=$(git rev-parse --verify "refs/tags/$tag^{commit}")
git merge-base --is-ancestor "$revision" origin/main || fail 'Release tag is not on main'
git show "$revision:CMakeLists.txt" | grep -Fxq "project(whisper_transcribator VERSION $version LANGUAGES C CXX)" || fail 'Tag version differs from CMake'
git show "$revision:CMakeLists.txt" | grep -Fxq 'set(WT_VERSION_SUFFIX "")' || fail 'Release still has a development suffix'
git cat-file -e "$revision:docs/releases/$version.md" || fail 'Release notes are missing'
run=$(gh run list --repo "$repo" --workflow ci.yml --branch main --event push \
    --commit "$revision" --status success --limit 1 --json databaseId --jq '.[0].databaseId // empty')
[[ $run =~ ^[0-9]+$ ]] || fail 'No successful main CI run for this release commit'
artifacts=$(gh api "repos/$repo/actions/runs/$run/artifacts" --paginate \
    --jq '.artifacts[] | select(.expired == false) | .name')
for name in native-cpu native-cpu-aarch64 native-macos-arm64 native-windows-x86_64 native-windows-arm64; do
    grep -Fxq "$name" <<<"$artifacts" || fail "Missing or expired CI artifact: $name; rerun CI for the release commit"
done
# Referencing an environment in YAML can create it without protection rules.
for environment in cuda-release release; do
    settings=$(gh api "repos/$repo/environments/$environment")
    jq -e --arg owner "${repo%%/*}" '
        .deployment_branch_policy.custom_branch_policies == true and
        any(.protection_rules[]; .type == "required_reviewers" and .prevent_self_review == false and
            (.reviewers | length == 1) and .reviewers[0].type == "User" and .reviewers[0].reviewer.login == $owner)
    ' <<<"$settings" >/dev/null || fail "Configure owner approval and custom main-only branches for environment: $environment"
    policies=$(gh api "repos/$repo/environments/$environment/deployment-branch-policies")
    jq -e '.total_count == 1 and .branch_policies[0].name == "main" and .branch_policies[0].type == "branch"' \
        <<<"$policies" >/dev/null || fail "Environment must allow only the main branch: $environment"
done
jq -n --arg version "$version" --arg revision "$revision" --arg ci_run "$run" \
    '{version: $version, revision: $revision, ci_run: $ci_run}'
