#!/usr/bin/env bash
set -euo pipefail # strict mode

BUILD_TYPE="Debug"
CLEAN=0

for arg in "$@"; do
    case "$arg" in
        --clean|-c)
            CLEAN=1
            ;;
        Debug|Release|RelWithDebInfo|MinSizeRel)
            BUILD_TYPE="$arg"
            ;;
        *)
            echo "Usage: $0 [Debug|Release|RelWithDebInfo|MinSizeRel] [--clean|-c]"
            exit 1
            ;;
    esac
done

if [[ "$CLEAN" -eq 1 ]]; then
    rm -rf build
fi

cmake -S . -B build -DCMAKE_BUILD_TYPE="$BUILD_TYPE"
cmake --build build -j
