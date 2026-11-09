#!/usr/bin/env bash
set -euo pipefail

# The one place vcpkg is installed, used by scripts/dev.sh, CI and the
# Dockerfile.
#
#   scripts/setup-vcpkg.sh [--root DIR]   set up vcpkg, print its root on stdout
#   scripts/setup-vcpkg.sh --baseline     print the baseline commit and exit
#
# vcpkg is checked out at "builtin-baseline" from vcpkg.json (the only place
# the commit is recorded) and bootstrapped. The default root is
# ${XDG_CACHE_HOME:-~/.cache}/hft-engine/vcpkg. Re-running is cheap: an
# up-to-date root is only verified, and a baseline bump moves it in place.
#
# The script manages only roots it created (marked by .hft-engine-vcpkg). It
# refuses to touch any other directory, so an existing personal vcpkg
# checkout is never modified. Progress goes to stderr, so
#   export VCPKG_ROOT="$(scripts/setup-vcpkg.sh)"
# works as expected.

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
MARKER=".hft-engine-vcpkg"

log() { echo "setup-vcpkg: $*" >&2; }

read_baseline() {
    # No jq/python dependency: this also runs in a bare Ubuntu image.
    local matches
    matches=$(sed -n 's/.*"builtin-baseline"[[:space:]]*:[[:space:]]*"\([0-9a-f]\{40\}\)".*/\1/p' \
        "$ROOT/vcpkg.json")
    if [[ $(wc -l <<<"$matches") -ne 1 || -z "$matches" ]]; then
        log "expected exactly one 40-hex builtin-baseline in vcpkg.json"
        exit 1
    fi
    echo "$matches"
}

VCPKG_DIR="${XDG_CACHE_HOME:-$HOME/.cache}/hft-engine/vcpkg"
while [[ $# -gt 0 ]]; do
    case "$1" in
        --root) VCPKG_DIR="$2"; shift 2 ;;
        --baseline) read_baseline; exit 0 ;;
        -h|--help) sed -n '4,19p' "$0"; exit 0 ;;
        *) log "unknown option: $1"; exit 1 ;;
    esac
done

BASELINE="$(read_baseline)"

if [[ -e "$VCPKG_DIR" && ! -f "$VCPKG_DIR/$MARKER" ]]; then
    if [[ -n "$(ls -A "$VCPKG_DIR" 2>/dev/null)" ]]; then
        log "$VCPKG_DIR exists but was not created by this script; refusing to modify it."
        log "Remove it, or pass --root with a different directory."
        exit 1
    fi
fi

if [[ ! -d "$VCPKG_DIR/.git" ]]; then
    log "cloning vcpkg into $VCPKG_DIR"
    mkdir -p "$VCPKG_DIR"
    git clone --quiet --filter=blob:none --no-checkout \
        https://github.com/microsoft/vcpkg.git "$VCPKG_DIR"
    : > "$VCPKG_DIR/$MARKER"
fi

if [[ "$(git -C "$VCPKG_DIR" rev-parse HEAD 2>/dev/null || true)" != "$BASELINE" ]]; then
    if ! git -C "$VCPKG_DIR" cat-file -e "${BASELINE}^{commit}" 2>/dev/null; then
        log "fetching baseline $BASELINE"
        git -C "$VCPKG_DIR" fetch --quiet --filter=blob:none origin "$BASELINE"
    fi
    log "checking out baseline $BASELINE"
    git -C "$VCPKG_DIR" checkout --quiet --force --detach "$BASELINE"
fi

# The bootstrapped binary belongs to a baseline. Record which one it was built
# for, so a baseline bump re-bootstraps rather than running a stale tool.
if [[ ! -x "$VCPKG_DIR/vcpkg" || "$(cat "$VCPKG_DIR/$MARKER")" != "$BASELINE" ]]; then
    # bootstrap-vcpkg.sh downloads the tool from GitHub Releases without
    # retrying; a transient 5xx there has failed CI before.
    for attempt in 1 2 3 4; do
        if "$VCPKG_DIR/bootstrap-vcpkg.sh" -disableMetrics >&2; then
            break
        fi
        if [[ "$attempt" -eq 4 ]]; then
            log "bootstrap failed after $attempt attempts"
            exit 1
        fi
        log "bootstrap failed (attempt $attempt), retrying in $((attempt * 15))s"
        sleep $((attempt * 15))
    done
    echo "$BASELINE" > "$VCPKG_DIR/$MARKER"
fi

log "vcpkg $("$VCPKG_DIR/vcpkg" version | head -n1 | sed 's/.*version //') at baseline ${BASELINE:0:12}"
echo "$VCPKG_DIR"
