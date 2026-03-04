#!/usr/bin/env bash
set -euo pipefail # strict mode

cmake -S . -B build
cmake --build build -j
