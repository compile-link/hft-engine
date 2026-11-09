#!/usr/bin/env bash
set -euo pipefail

# clang-tidy runner, used locally and by CI. Checks every tracked C++
# translation unit in src/ and tests/; headers are covered through the TUs
# that include them (HeaderFilterRegex in .clang-tidy). Any finding fails.
#
#   scripts/tidy.sh
#
# Uses $TIDY_BUILD_DIR, or the first build/* directory with a
# compile_commands.json. Generated protobuf headers must already exist.
#
# Version pinned for the same reason as clang-format: diagnostics differ
# between releases, so CI and local runs must agree.
CLANG_TIDY_VERSION="23.1.4"

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"

case "${1:-}" in
    "") ;;
    -h|--help) sed -n '4,13p' "$0"; exit 0 ;;
    *) echo "Unknown option: $1" >&2; exit 1 ;;
esac

if command -v uvx >/dev/null 2>&1; then
    TIDY=(uvx --from "clang-tidy==${CLANG_TIDY_VERSION}" clang-tidy)
elif command -v clang-tidy >/dev/null 2>&1 \
    && clang-tidy --version | grep -q "${CLANG_TIDY_VERSION}"; then
    TIDY=(clang-tidy)
else
    echo "Need uvx, or clang-tidy ${CLANG_TIDY_VERSION} on PATH" >&2
    exit 1
fi

# clang-tidy needs the compilation database, and the generated protobuf
# headers it references must already exist.
BUILD_DIR="${TIDY_BUILD_DIR:-}"
if [[ -z "$BUILD_DIR" ]]; then
    for d in build/debug build/sys-debug build/*/; do
        if [[ -f "$d/compile_commands.json" ]]; then BUILD_DIR="${d%/}"; break; fi
    done
fi
if [[ -z "$BUILD_DIR" || ! -f "$BUILD_DIR/compile_commands.json" ]]; then
    echo "No compile_commands.json found. Configure a preset first, e.g." >&2
    echo "  cmake --preset debug && cmake --build --preset debug --target hft_proto" >&2
    exit 1
fi
echo "Using compilation database: $BUILD_DIR"

mapfile -t FILES < <(git ls-files 'src/*.cpp' 'tests/*.cpp')
[[ ${#FILES[@]} -eq 0 ]] && { echo "No files to check."; exit 0; }
"${TIDY[@]}" -p "$BUILD_DIR" --warnings-as-errors='*' "${FILES[@]}"
