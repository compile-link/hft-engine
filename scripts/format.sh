#!/usr/bin/env bash
set -euo pipefail

# Single source of truth for C++ formatting, used locally and by CI.
#   scripts/format.sh          reformat files in place
#   scripts/format.sh --check  fail if any file is not formatted
#
# clang-format output differs between major versions, so the version is pinned
# here and fetched from PyPI via uvx (or pip) rather than taken from the distro.
CLANG_FORMAT_VERSION="23.1.5"

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"

if command -v uvx >/dev/null 2>&1; then
    CF=(uvx "clang-format==${CLANG_FORMAT_VERSION}")
elif command -v clang-format >/dev/null 2>&1 \
    && clang-format --version | grep -q "version ${CLANG_FORMAT_VERSION}"; then
    CF=(clang-format)
else
    echo "Need uvx, or clang-format ${CLANG_FORMAT_VERSION} on PATH" >&2
    echo "  (pip install clang-format==${CLANG_FORMAT_VERSION})" >&2
    exit 1
fi

mapfile -t FILES < <(git ls-files 'src/*.cpp' 'src/*.hpp' 'include/*.hpp' 'include/*.h' 'tests/*.cpp' 'tests/*.hpp')

if [[ "${1:-}" == "--check" ]]; then
    "${CF[@]}" --dry-run --Werror "${FILES[@]}"
    echo "clang-format ${CLANG_FORMAT_VERSION}: ${#FILES[@]} files OK"
else
    "${CF[@]}" -i "${FILES[@]}"
fi
