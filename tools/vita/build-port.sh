#!/usr/bin/env bash

set -euo pipefail

BUILD_DIR="${1:-build/vita}"
BUILD_TYPE="${CMAKE_BUILD_TYPE:-Release}"
shift $(( $# > 0 ? 1 : 0 ))
REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"

if [ -z "${VITASDK:-}" ] || [ ! -f "${VITASDK}/share/vita.toolchain.cmake" ]; then
  echo 'Could not find VitaSDK. Run this inside the vitasdk/vitasdk container or set VITASDK.' >&2
  exit 1
fi

cmake -S "${REPO_ROOT}" -B "${REPO_ROOT}/${BUILD_DIR}" \
  -DCMAKE_TOOLCHAIN_FILE="${VITASDK}/share/vita.toolchain.cmake" \
  -DCMAKE_BUILD_TYPE="${BUILD_TYPE}" \
  "$@"

cmake --build "${REPO_ROOT}/${BUILD_DIR}" -j"$(nproc)"

echo "Built ${REPO_ROOT}/${BUILD_DIR}/3sx.vpk"
