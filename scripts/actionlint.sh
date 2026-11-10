#!/usr/bin/env bash
set -euo pipefail

# actionlint for the GitHub Actions workflows, used locally and by CI.
#
#   scripts/actionlint.sh [actionlint args...]
#
# Lints .github/workflows/*.yml, including the `run:` scripts in them via the
# pinned shellcheck. The local composite action is read to check the inputs
# workflows pass to it; its own steps are covered by scripts/shellcheck.sh.
#
# Both binaries are pinned by version and SHA-256 (scripts/lib/fetch-tool.sh).
ACTIONLINT_VERSION="1.7.12"

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"
# shellcheck source=scripts/lib/fetch-tool.sh
source scripts/lib/fetch-tool.sh
# shellcheck source=scripts/lib/shellcheck-bin.sh
source scripts/lib/shellcheck-bin.sh

case "$(host_platform)" in
    linux_x86_64)   asset=linux_amd64
                    sha=8aca8db96f1b94770f1b0d72b6dddcb1ebb8123cb3712530b08cc387b349a3d8 ;;
    linux_aarch64)  asset=linux_arm64
                    sha=325e971b6ba9bfa504672e29be93c24981eeb1c07576d730e9f7c8805afff0c6 ;;
    darwin_x86_64)  asset=darwin_amd64
                    sha=5b44c3bc2255115c9b69e30efc0fecdf498fdb63c5d58e17084fd5f16324c644 ;;
    darwin_aarch64) asset=darwin_arm64
                    sha=aba9ced2dee8d27fecca3dc7feb1a7f9a52caefa1eb46f3271ea66b6e0e6953f ;;
esac
fetch_tool actionlint "$ACTIONLINT_VERSION" \
    "https://github.com/rhysd/actionlint/releases/download/v${ACTIONLINT_VERSION}/actionlint_${ACTIONLINT_VERSION}_${asset}.tar.gz" \
    "$sha" actionlint
ACTIONLINT="$TOOL_BIN"

fetch_shellcheck

"$ACTIONLINT" -shellcheck="$SHELLCHECK" -pyflakes= "$@"
echo "actionlint ${ACTIONLINT_VERSION} (shellcheck ${SHELLCHECK_VERSION}): OK"
