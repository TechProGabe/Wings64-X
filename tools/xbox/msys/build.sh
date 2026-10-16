#!/usr/bin/env bash
# Build default.xbe natively on Windows with MSYS2 (no Docker, no WSL;
# the same layout as tools/xbox/docker/build.sh).
# Run from Git Bash or an MSYS2 shell:
#   tools/xbox/msys/build.sh                       # -> build-xbox/xbe/default.xbe
#   XBOX_CFLAGS=-DXHW_AUTOPAD=1 tools/xbox/msys/build.sh
# One-time setup (docs/toolchain.md):
#   pacman -S make git bison flex cmake ninja patch mingw-w64-x86_64-clang \
#     mingw-w64-x86_64-lld mingw-w64-x86_64-llvm mingw-w64-x86_64-python \
#     mingw-w64-x86_64-python-pillow
#   (not mingw-w64-x86_64-cmake/ninja: they can't run nxdk's shell wrappers)
#   nxdk at the pinned NXDK_SHA (tools/xbox/setup.sh) in $NXDK_DIR (default
#   /c/xdev/nxdk), built with `make tools` and `make NXDK_ONLY=y NXDK_SDL=y NXDK_CXX=y`.
# MSYS2's clang is LLVM 21.1.8, the docker image's version.
set -euo pipefail
root="$(cd "$(dirname "$0")/../../.." && pwd)"
msys="${MSYS2_ROOT:-/c/msys64}"
[ -x "$msys/usr/bin/bash" ] || { echo "no MSYS2 at $msys"; exit 1; }
rootu="$(cd "$root" && pwd)"
# Git Bash's environment doesn't reach MSYS2's bash: hand the knobs over as arguments
knobs=()
for v in XBOX_CFLAGS XBOX_CMAKE_ARGS XBOX_NINJA_ARGS XBOX_FORCE XBOX_KEEP_TEMPS XBOX_NO_ICON XBOX_LTO NXDK_DIR; do
  [ -n "${!v:-}" ] && knobs+=("$v=${!v}")
done
exec "$msys/usr/bin/bash" -lc '
  set -euo pipefail
  export PATH=/usr/bin:/mingw64/bin:$PATH
  export NXDK_DIR="${NXDK_DIR:-/c/xdev/nxdk}" LLVM=/mingw64 PYTHONUTF8=1
  root="$1"; shift; cd "$root"
  for kv in "$@"; do export "$kv"; done
  xbox/build.sh
  xbe=build-xbox/xbe/default.xbe
  if [ -z "${XBOX_NO_ICON:-}" ] && [ -f "$xbe" ]; then
    python3 tools/xbox/xbe_title_image.py "$xbe" xbox/assets/logo.png build-xbox/xbe/default.tbn
  fi' _ "$rootu" ${knobs[@]+"${knobs[@]}"}
