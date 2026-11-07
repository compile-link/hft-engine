#!/usr/bin/env bash
set -euo pipefail # strict mode

# Thin wrapper around CMakePresets.json. Presets are the single source of truth
# for configure flags, shared by this script, CI and the IDE.
#
# Usage: scripts/dev.sh [preset] [--clean|-c] [--test|-t]
#
# Dependency-resolution presets:
#   debug | release | relwithdebinfo | asan-ubsan | tsan   -> vcpkg (needs VCPKG_ROOT)
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
            sed -n '4,14p' "$0"
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
        if [[ -z "${VCPKG_ROOT:-}" ]]; then
            echo "Preset '${PRESET}' resolves dependencies via vcpkg but VCPKG_ROOT is unset." >&2
            echo "Either export VCPKG_ROOT, or use a 'sys-*' preset to build against system packages." >&2
            exit 1
        fi
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
