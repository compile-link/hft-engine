#!/usr/bin/env bash
set -euo pipefail

# Test coverage of the Rust strategy crate (strategy_engine), used locally and by CI.
#
#   scripts/coverage.sh                  # summary table: regions, functions, lines
#   scripts/coverage.sh --html --open    # browsable report (strategy_engine/target/llvm-cov/html)
#   scripts/coverage.sh --lcov --output-path lcov.info
#   scripts/coverage.sh --no-report      # run the tests only, then:
#   scripts/coverage.sh report [args]    # report from that run, as often as needed
#
# Arguments are passed to `cargo llvm-cov`. It builds an instrumented copy of
# the crate in its own target directory, runs the full test suite (unit and
# integration tests), and reports what the tests executed. Only production code
# is reported: the unit tests (src/tests.rs; tests/ is never included) would
# count themselves as covered, and the code generated from the .proto file is
# prost's, not ours.
#
# The binary is pinned by version and SHA-256 (scripts/lib/fetch-tool.sh). It
# needs the llvm-tools rustup component of the pinned toolchain, installed
# here on first use rather than in rust-toolchain.toml so builds that never
# measure coverage do not download it.
CARGO_LLVM_COV_VERSION="0.9.1"

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"
# shellcheck source=scripts/lib/fetch-tool.sh
source scripts/lib/fetch-tool.sh

case "$(host_platform)" in
    linux_x86_64)   triple=x86_64-unknown-linux-gnu
                    sha=b3f68e625481fed9b16444174f3fa5ebcdbde4a1878803a35eabe2dcefcdc41a ;;
    linux_aarch64)  triple=aarch64-unknown-linux-gnu
                    sha=abf5f13c1520f8756d2192bfaaeadb0208f5b7eaa8cadc15bf847befa42b7360 ;;
    darwin_x86_64)  triple=x86_64-apple-darwin
                    sha=ddf949b82a63017441ea76aa068bdd7e2dde8055f47cd3ef736fdbbd633bc33e ;;
    darwin_aarch64) triple=aarch64-apple-darwin
                    sha=7e821a6c90c0884f79f759f5d9be50799ade67bb82515186d922f26032ef43b8 ;;
esac
fetch_tool cargo-llvm-cov "$CARGO_LLVM_COV_VERSION" \
    "https://github.com/taiki-e/cargo-llvm-cov/releases/download/v${CARGO_LLVM_COV_VERSION}/cargo-llvm-cov-${triple}.tar.gz" \
    "$sha" cargo-llvm-cov

cd strategy_engine
# Run from the crate so rustup resolves the pinned toolchain (rust-toolchain.toml)
rustup component add llvm-tools >/dev/null

# Invoked directly rather than as `cargo llvm-cov`, so the first argument is the
# subcommand name cargo would otherwise pass.
ignore=(--ignore-filename-regex '(/out/hft\.rs|/src/tests\.rs)$')
if [[ "${1:-}" == report ]]; then
    shift
    "$TOOL_BIN" llvm-cov report "${ignore[@]}" "$@"
else
    "$TOOL_BIN" llvm-cov --locked "${ignore[@]}" "$@"
fi
