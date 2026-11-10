#!/usr/bin/env bash
set -euo pipefail

# RustSec advisory check of strategy_engine/Cargo.lock, used locally and by CI.
#
#   scripts/audit.sh [cargo-audit args...]
#
# Fails on any vulnerability advisory and on yanked crates. Unmaintained and
# unsound advisories are reported but do not fail. Accepted advisories go in
# strategy_engine/.cargo/audit.toml ([advisories] ignore = [...]) with a reason.
#
# The binary is pinned by version and SHA-256 (scripts/lib/fetch-tool.sh); the
# advisory database is fetched fresh on every run, which is the point.
CARGO_AUDIT_VERSION="0.22.2"

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"
# shellcheck source=scripts/lib/fetch-tool.sh
source scripts/lib/fetch-tool.sh

case "$(host_platform)" in
    linux_x86_64)   triple=x86_64-unknown-linux-gnu
                    sha=ab28a1bdb54db4d5d8ad5981cf1f959410370b3d28250dbd35f6a44248620e39 ;;
    linux_aarch64)  triple=aarch64-unknown-linux-gnu
                    sha=c6603814ddaa45e51263dafd31c0ac98808f688d26f7395804f9670b0fd599dd ;;
    darwin_x86_64)  triple=x86_64-apple-darwin
                    sha=847831323de932155b226ab60ee4a180e13e5d007a019f0d4b7b4d89a6de2ab2 ;;
    darwin_aarch64) triple=aarch64-apple-darwin
                    sha=ec7ca4263769593df4d909be85b94a6b79efa2897be5d2bb8ebd516e823175af ;;
esac
name="cargo-audit-${triple}-v${CARGO_AUDIT_VERSION}"
fetch_tool cargo-audit "$CARGO_AUDIT_VERSION" \
    "https://github.com/rustsec/rustsec/releases/download/cargo-audit/v${CARGO_AUDIT_VERSION}/${name}.tgz" \
    "$sha" "${name}/cargo-audit"

cd strategy_engine
"$TOOL_BIN" audit --deny yanked "$@"
echo "cargo-audit ${CARGO_AUDIT_VERSION}: OK"
