#!/usr/bin/env bash
set -euo pipefail

# Trivy vulnerability scan of a built container image, used locally and by CI.
#
#   scripts/trivy.sh <image> [trivy args...]      e.g. scripts/trivy.sh hft-engine:ci
#
# Fails on HIGH or CRITICAL vulnerabilities that have a fixed version
# available; unfixed ones are reported only, since there is nothing to bump.
# Accepted findings go in .trivyignore with a reason and an expiry.
#
# What this covers: the Ubuntu packages in the image. The hft binary links its
# vcpkg dependencies statically and carries no package metadata, so Trivy
# cannot see them; those versions are pinned by the vcpkg baseline instead.
#
# The binary is pinned by version and SHA-256 (scripts/lib/fetch-tool.sh); the
# vulnerability database is fetched by Trivy and cached in its cache dir.
TRIVY_VERSION="0.76.2"

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"
# shellcheck source=scripts/lib/fetch-tool.sh
source scripts/lib/fetch-tool.sh

[[ $# -ge 1 ]] || { sed -n '4,17p' "$0"; exit 1; }
IMAGE="$1"; shift

case "$(host_platform)" in
    linux_x86_64)   asset=Linux-64bit
                    sha=bc0fc726e017b8927a62b2d3531d7a84ee01769daf5c99ed866dced54089fdbf ;;
    linux_aarch64)  asset=Linux-ARM64
                    sha=ab9d51f86e6a5fb936f605faff6b593ac51bc378fff4d3e8b9d58358b9c367c3 ;;
    darwin_x86_64)  asset=macOS-64bit
                    sha=e08bb9ea449ecf66c6bfb03cbb8697c503ff33d35fc585c9747e3eaeb7ed4c7b ;;
    darwin_aarch64) asset=macOS-ARM64
                    sha=ae7ea358326960406b703c77b2ac490190797d80b306b5b50c2a27221da0a5b8 ;;
esac
fetch_tool trivy "$TRIVY_VERSION" \
    "https://github.com/aquasecurity/trivy/releases/download/v${TRIVY_VERSION}/trivy_${TRIVY_VERSION}_${asset}.tar.gz" \
    "$sha" trivy

"$TOOL_BIN" image \
    --scanners vuln \
    --severity HIGH,CRITICAL \
    --ignore-unfixed \
    --ignorefile .trivyignore \
    --exit-code 1 \
    --no-progress \
    "$@" "$IMAGE"
echo "trivy ${TRIVY_VERSION}: ${IMAGE} OK"
