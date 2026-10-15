#!/usr/bin/env bash
# Build default.xbe -> build-xbox/xbe/default.xbe
# Needs the toolchain from tools/xbox/setup.sh (NXDK_DIR, LLVM).
#   XBOX_CFLAGS   extra C flags for every unit (platform and game), e.g.
#                 "-DXHW_AUTOPAD=1"; reset on every run, so a debug knob never
#                 sticks in the cache (docs/testing.md has the switch table)
#   XBOX_LTO=1    ThinLTO over the game code (tools/xbox/thinlto_link.py);
#                 unset or 0: plain objects
set -euo pipefail
root="$(cd "$(dirname "$0")/.." && pwd)"
export NXDK_DIR="${NXDK_DIR:-/opt/nxdk}"
export LLVM="${LLVM:-/opt/llvm21}"
export PATH="$LLVM/bin:$PATH"
eval "$("$NXDK_DIR/bin/activate" -s)"
case "${XBOX_LTO:-0}" in 0 | "") lto=OFF ;; *) lto=ON ;; esac
cmake -S "$root/xbox" -B "$root/build-xbox" -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE="$NXDK_DIR/share/toolchain-nxdk.cmake" -DCMAKE_C_FLAGS="${XBOX_CFLAGS:-}" \
  -DXBOX_LTO="$lto" \
  ${XBOX_CMAKE_ARGS:-} >/dev/null
ninja -C "$root/build-xbox" ${XBOX_NINJA_ARGS:-}
ls -la "$root/build-xbox/xbe/default.xbe"
