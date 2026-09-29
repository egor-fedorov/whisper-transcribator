#!/usr/bin/env bash
set -euo pipefail
helper=$(realpath "$(dirname "$0")/../../packaging/release/preflight.sh")
root=$(mktemp -d)
trap 'rm -rf "$root"' EXIT
mkdir -p "$root/repo" "$root/bin"
# shellcheck disable=SC2016
printf '%s\n' '#!/usr/bin/env bash' 'set -euo pipefail' \
    'case "$1 $2" in' \
    '  "run list") if [[ ${NO_CI:-0} = 0 ]]; then echo 123; fi ;;' \
    '  "api repos/example/project/actions/runs/123/artifacts")' \
    '    echo native-cpu; echo native-cpu-aarch64; echo native-macos-arm64; echo native-windows-x86_64' \
    '    if [[ ${MISSING_ARTIFACT:-0} = 0 ]]; then echo native-windows-arm64; fi ;;' \
    '  "api "*/deployment-branch-policies)' \
    '    jq -n --arg branch "${ALLOW_BRANCH:-main}" '\''{total_count: 1, branch_policies: [{name: $branch, type: "branch"}]}'\'' ;;' \
    '  "api repos/example/project/environments/"*)' \
    '    jq -n --argjson unprotected "${UNPROTECTED:-false}" '\''{deployment_branch_policy: {custom_branch_policies: true}, protection_rules:
            (if $unprotected then [] else [{type: "required_reviewers", prevent_self_review: false,
            reviewers: [{type: "User", reviewer: {login: "example"}}]}] end)}'\'' ;;' \
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
mkdir -p docs/releases
printf 'project(whisper_transcribator VERSION 0.5.0 LANGUAGES C CXX)\nset(WT_VERSION_SUFFIX "")\n' >CMakeLists.txt
printf '# Release\n' >docs/releases/0.5.0.md
git add .
git commit -qm Fixture
git tag v0.5.0
git update-ref refs/remotes/origin/main HEAD
bash "$helper" example/project v0.5.0 >"$root/result.json"
jq -e --arg revision "$(git rev-parse HEAD)" \
    '.version == "0.5.0" and .revision == $revision and .ci_run == "123"' "$root/result.json"
reject() {
    local message=$1
    shift
    if bash "$helper" example/project "$@" >"$root/failure.log" 2>&1; then
        echo 'Unexpected preflight success' >&2; exit 1
    fi
    grep -Fq "$message" "$root/failure.log"
}
reject 'Expected a vX.Y.Z' '../v0.5.0'
NO_CI=1 reject 'No successful main CI' v0.5.0
MISSING_ARTIFACT=1 reject 'Missing or expired CI artifact' v0.5.0
UNPROTECTED=true reject 'Configure owner approval' v0.5.0
ALLOW_BRANCH='*' reject 'Environment must allow only the main branch' v0.5.0
git tag v0.5.1
reject 'Tag version differs' v0.5.1
printf 'project(whisper_transcribator VERSION 0.6.0 LANGUAGES C CXX)\nset(WT_VERSION_SUFFIX "-dev")\n' >CMakeLists.txt
git add CMakeLists.txt
git commit -qm Development
git tag v0.6.0
reject 'Release tag is not on main' v0.6.0
git update-ref refs/remotes/origin/main HEAD
reject 'development suffix' v0.6.0
echo 'Release preflight checks passed'
