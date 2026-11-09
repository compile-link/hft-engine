#!/usr/bin/env bash
set -euo pipefail

# hadolint for the Dockerfile, used locally and by CI.
#
#   scripts/hadolint.sh [hadolint args...]
#
# Rule configuration lives in .hadolint.yaml. The binary is pinned by version
# and SHA-256 (scripts/lib/fetch-tool.sh).
HADOLINT_VERSION="2.15.1"

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"
# shellcheck source=scripts/lib/fetch-tool.sh
source scripts/lib/fetch-tool.sh

case "$(host_platform)" in
    linux_x86_64)   asset=linux-x86_64
                    sha=c7187db94eeeeca956519a6af171adc31453941a1e777961f6e680f697c8c507 ;;
    linux_aarch64)  asset=linux-arm64
                    sha=f6198ef8090f404dbb771abfee086eb8c48ac177f30da7fd3510aca35b344b5d ;;
    darwin_x86_64)  asset=macos-x86_64
                    sha=ffe9bb18b23d5ed1eae50237aecdbb523d016e96da0bd4e7aa432040acfc3fde ;;
    darwin_aarch64) asset=macos-arm64
                    sha=5c09f3213f8e40406abe048233d985eebef336d4a6a20021be47fadb6cf480a2 ;;
esac
fetch_tool hadolint "$HADOLINT_VERSION" \
    "https://github.com/hadolint/hadolint/releases/download/v${HADOLINT_VERSION}/hadolint-${asset}" \
    "$sha"

mapfile -t FILES < <(git ls-files --cached --others --exclude-standard 'Dockerfile' '*.Dockerfile' '*/Dockerfile')
"$TOOL_BIN" "$@" "${FILES[@]}"
echo "hadolint ${HADOLINT_VERSION}: ${#FILES[@]} Dockerfile(s) OK"
