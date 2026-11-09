#!/usr/bin/env bash
set -euo pipefail

# actionlint for the GitHub Actions workflows, used locally and by CI.
#
#   scripts/actionlint.sh [actionlint args...]
#
# Lints .github/workflows/*.yml. The local composite action is read to check
# the inputs workflows pass to it, but its own steps are not linted.
#
# The release binary is pinned by version and SHA-256 and cached under
# ${XDG_CACHE_HOME:-~/.cache}/hft-engine. The shellcheck and pyflakes
# integrations are off, so the result does not depend on whatever versions
# happen to be on PATH.
ACTIONLINT_VERSION="1.7.12"

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"

case "$(uname -s)-$(uname -m)" in
    Linux-x86_64)   PLATFORM=linux_amd64
                    SHA256=8aca8db96f1b94770f1b0d72b6dddcb1ebb8123cb3712530b08cc387b349a3d8 ;;
    Linux-aarch64)  PLATFORM=linux_arm64
                    SHA256=325e971b6ba9bfa504672e29be93c24981eeb1c07576d730e9f7c8805afff0c6 ;;
    Darwin-x86_64)  PLATFORM=darwin_amd64
                    SHA256=5b44c3bc2255115c9b69e30efc0fecdf498fdb63c5d58e17084fd5f16324c644 ;;
    Darwin-arm64)   PLATFORM=darwin_arm64
                    SHA256=aba9ced2dee8d27fecca3dc7feb1a7f9a52caefa1eb46f3271ea66b6e0e6953f ;;
    *) echo "Unsupported platform: $(uname -s)-$(uname -m)" >&2; exit 1 ;;
esac

BIN_DIR="${XDG_CACHE_HOME:-$HOME/.cache}/hft-engine/actionlint-${ACTIONLINT_VERSION}"
BIN="$BIN_DIR/actionlint"

if [[ ! -x "$BIN" ]]; then
    TARBALL="actionlint_${ACTIONLINT_VERSION}_${PLATFORM}.tar.gz"
    URL="https://github.com/rhysd/actionlint/releases/download/v${ACTIONLINT_VERSION}/${TARBALL}"
    TMP="$(mktemp -d)"
    trap 'rm -rf "$TMP"' EXIT
    echo "Downloading actionlint ${ACTIONLINT_VERSION} (${PLATFORM})"
    curl -fsSL --retry 4 --retry-all-errors -o "$TMP/$TARBALL" "$URL"
    if command -v sha256sum >/dev/null 2>&1; then
        echo "${SHA256}  $TMP/$TARBALL" | sha256sum -c --quiet -
    else
        echo "${SHA256}  $TMP/$TARBALL" | shasum -a 256 -c --quiet -
    fi
    tar -xzf "$TMP/$TARBALL" -C "$TMP" actionlint
    mkdir -p "$BIN_DIR"
    mv "$TMP/actionlint" "$BIN"
fi

"$BIN" -shellcheck= -pyflakes= "$@"
echo "actionlint ${ACTIONLINT_VERSION}: OK"
