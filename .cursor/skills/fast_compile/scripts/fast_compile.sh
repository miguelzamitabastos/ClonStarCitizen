#!/usr/bin/env bash
# Accelerated ClonStarCitizen build — configure if needed, then parallel compile.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../../.." && pwd)"
cd "$ROOT"

if [[ ! -d build ]] || [[ ! -f build/CMakeCache.txt ]]; then
  cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
fi

cmake --build build -j"$(nproc)"
