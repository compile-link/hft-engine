#!/usr/bin/env bash
set -euo pipefail # strict mode

BUILD_TYPE=${1:-Debug}
cmake -S . -B build -DCMAKE_BUILD_TYPE=$BUILD_TYPE
cmake --build build -j
