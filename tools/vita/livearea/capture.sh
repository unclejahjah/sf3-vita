#!/usr/bin/env bash
# Records the desktop build's attract mode (CAPCOM logo, intro, title screen, player select)
# so compose.py can cut LiveArea art out of it. Needs the desktop build, Xvfb and ffmpeg, and
# SF33RD.AFS in the desktop data folder (~/.local/share/CrowdedStreet/3SX/resources/).
#
# Usage: tools/vita/livearea/capture.sh [out_dir] [desktop_binary]
# The frames are copyrighted game art: they stay on your machine (out_dir defaults to build/livearea-frames).

set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)"
OUT_DIR="${1:-${REPO_ROOT}/build/livearea-frames}"
GAME="${2:-${REPO_ROOT}/build/application/bin/3sx}"
DISPLAY_NUM="${LIVEAREA_DISPLAY:-:97}"
SECONDS_TO_RECORD="${LIVEAREA_SECONDS:-70}"

if [ ! -x "${GAME}" ]; then
  echo "Desktop build not found at ${GAME}. Build it and run 'cmake --install build/application --prefix build/application'." >&2
  exit 1
fi

mkdir -p "${OUT_DIR}"
rm -f "${OUT_DIR}"/f*.png

Xvfb "${DISPLAY_NUM}" -screen 0 1024x768x24 >/dev/null 2>&1 &
XVFB_PID=$!
GAME_PID=""
cleanup() {
  [ -n "${GAME_PID}" ] && kill "${GAME_PID}" 2>/dev/null || true
  kill "${XVFB_PID}" 2>/dev/null || true
}
trap cleanup EXIT
sleep 2

DISPLAY="${DISPLAY_NUM}" LIBGL_ALWAYS_SOFTWARE=1 SDL_AUDIODRIVER=dummy "${GAME}" --no-sound >"${OUT_DIR}/game.log" 2>&1 &
GAME_PID=$!
sleep 3

# The game window sits at the top left at 640x480 (4:3).
DISPLAY="${DISPLAY_NUM}" ffmpeg -loglevel error -f x11grab -draw_mouse 0 -video_size 640x480 -framerate 2 -i "${DISPLAY_NUM}" \
  -t "${SECONDS_TO_RECORD}" "${OUT_DIR}/f%03d.png"

echo "Captured $(ls "${OUT_DIR}"/f*.png | wc -l) frames in ${OUT_DIR}"
