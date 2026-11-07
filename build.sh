#!/usr/bin/env bash
# Build the HML runtime on Linux (Raspberry Pi native or cross-compile).
#
#   ./build.sh [sdl|fbdev|drm] [--cross aarch64|armhf] [--debug] [--clean]
#
#   sdl    desktop window (needs: sudo apt install libsdl2-dev)
#   fbdev  Linux framebuffer + evdev, no extra packages   <- Raspberry Pi default
#   drm    DRM/KMS + evdev (needs: sudo apt install libdrm-dev)
#
# Output: build-<backend>[-<arch>]/bin/hml_runtime
set -euo pipefail
cd "$(dirname "$0")"

BACKEND=fbdev
CROSS=""
CFG=Release
CLEAN=0
while [ $# -gt 0 ]; do
  case "$1" in
    sdl|fbdev|drm) BACKEND=$1 ;;
    --cross) CROSS=$2; shift ;;
    --debug) CFG=Debug ;;
    --clean) CLEAN=1 ;;
    -h|--help) sed -n 2,11p "$0"; exit 0 ;;
    *) echo "unknown argument: $1" >&2; exit 1 ;;
  esac
  shift
done

BUILD=build-$BACKEND
EXTRA=()
case "$CROSS" in
  "") ;;
  aarch64) BUILD=$BUILD-aarch64; EXTRA+=(-DCMAKE_TOOLCHAIN_FILE=cmake/toolchain-aarch64-linux-gnu.cmake) ;;
  armhf)   BUILD=$BUILD-armhf;   EXTRA+=(-DCMAKE_TOOLCHAIN_FILE=cmake/toolchain-arm-linux-gnueabihf.cmake) ;;
  *) echo "--cross must be aarch64 or armhf" >&2; exit 1 ;;
esac

if command -v ninja >/dev/null 2>&1; then EXTRA+=(-G Ninja); fi
[ "$CLEAN" = 1 ] && rm -rf "$BUILD"

cmake -S . -B "$BUILD" -DCMAKE_BUILD_TYPE=$CFG -DHML_BACKEND=$BACKEND "${EXTRA[@]}"
cmake --build "$BUILD" -j"$(nproc)"
echo
echo "Built: $BUILD/bin/hml_runtime"
