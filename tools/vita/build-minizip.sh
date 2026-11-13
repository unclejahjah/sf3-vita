#!/usr/bin/env bash

set -euo pipefail

MINIZIP_NG_TAG="4.1.0"
BUILD_TYPE="${CMAKE_BUILD_TYPE:-Release}"
REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
MINIZIP_NG_PREFIX="${REPO_ROOT}/third_party/vita/minizip-ng/build"

if [ -z "${VITASDK:-}" ] || [ ! -f "${VITASDK}/share/vita.toolchain.cmake" ]; then
  echo 'Could not find VitaSDK. Run this inside the vitasdk/vitasdk container or set VITASDK.' >&2
  exit 1
fi

# Set MINIZIP_NG_SRC to an existing checkout to build without network access
minizip_src="${MINIZIP_NG_SRC:-}"
work_dir="$(mktemp -d)"
trap 'rm -rf "${work_dir}"' EXIT

if [ -z "${minizip_src}" ]; then
  minizip_src="${work_dir}/src"

  git clone \
    --branch "${MINIZIP_NG_TAG}" \
    --single-branch \
    https://github.com/zlib-ng/minizip-ng \
    "${minizip_src}"
fi

rm -rf "${MINIZIP_NG_PREFIX}"

cmake -S "${minizip_src}" -B "${work_dir}/build" \
  -DCMAKE_TOOLCHAIN_FILE="${VITASDK}/share/vita.toolchain.cmake" \
  -DCMAKE_BUILD_TYPE="${BUILD_TYPE}" \
  -DCMAKE_INSTALL_PREFIX="${MINIZIP_NG_PREFIX}" \
  -DBUILD_SHARED_LIBS=OFF \
  -DMZ_COMPAT=OFF \
  -DMZ_ZLIB_FLAVOR=zlib \
  -DMZ_BZIP2=OFF \
  -DMZ_LZMA=OFF \
  -DMZ_PPMD=OFF \
  -DMZ_ZSTD=OFF \
  -DMZ_LIBCOMP=OFF \
  -DMZ_PKCRYPT=OFF \
  -DMZ_WZAES=OFF \
  -DMZ_OPENSSL=OFF \
  -DMZ_LIBBSD=OFF \
  -DMZ_ICONV=OFF \
  -DMZ_FETCH_LIBS=OFF \
  -DMZ_DECOMPRESS_ONLY=ON \
  -DMZ_FILE32_API=ON

cmake --build "${work_dir}/build" -j"$(nproc)"
cmake --install "${work_dir}/build"
