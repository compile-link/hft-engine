#!/usr/bin/env bash
set -euo pipefail # strict mode

# Thin wrapper around CMakePresets.json. Presets are the single source of truth
# for configure flags, shared by this script, CI and the IDE.
#
# Usage: scripts/dev.sh [preset] [--clean|-c] [--test|-t]
#
# Dependency-resolution presets:
#   debug | release | relwithdebinfo | asan-ubsan | tsan   -> vcpkg, set up by
#                                                             scripts/setup-vcpkg.sh
#   sys-debug | sys-release                                -> system-installed deps
#
# Run `cmake --list-presets` to see all of them.

PRESET="debug"
CLEAN=0
RUN_TESTS=0

for arg in "$@"; do
    case "$arg" in
        --clean|-c)
            CLEAN=1
            ;;
        --test|-t)
            RUN_TESTS=1
            ;;
        -h|--help)
            sed -n '4,15p' "$0"
            exit 0
            ;;
        -*)
            echo "Unknown option: $arg" >&2
            exit 1
            ;;
        *)
            PRESET="$arg"
            ;;
    esac
done

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"

if ! cmake --list-presets | grep -qE "^  \"${PRESET}\""; then
    echo "Unknown preset: ${PRESET}" >&2
    cmake --list-presets >&2
    exit 1
fi

case "$PRESET" in
    sys-*) ;;
    *)
        # Always the project-managed vcpkg at the pinned baseline. An existing
        # VCPKG_ROOT is not used: it may point at any vcpkg commit.
        VCPKG_ROOT="$(scripts/setup-vcpkg.sh)"
        export VCPKG_ROOT
        ;;
esac

if [[ "$CLEAN" -eq 1 ]]; then
    rm -rf "build/${PRESET}"
fi

cmake --preset "$PRESET"
cmake --build --preset "$PRESET" -j

if [[ "$RUN_TESTS" -eq 1 ]]; then
    ctest --preset "$PRESET"
fi

# compile_commands.json lives in the preset's build dir; symlink the active one
# to the repo root for clang-tidy and editor tooling.
ln -sfn "build/${PRESET}/compile_commands.json" compile_commands.json
