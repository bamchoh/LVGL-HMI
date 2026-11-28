#!/usr/bin/env bash
# Build the Raspberry Pi runtime in WSL quickly, from a checkout on a Windows drive.
#
#   wsl -d Ubuntu -- bash -lc "/mnt/d/dev/lvgl-hml/tools/rpi/wsl_build.sh [aarch64|armhf]"
#
# Building straight from /mnt/<drive> is slow: every source and header is read across the
# Windows file system (a one-file change took about 18 minutes). This copies the sources into
# the WSL file system (only what changed), builds there, and puts the program back where
# build.sh would have: build-fbdev-<arch>/bin/hml_runtime in the checkout.
#
# Only the runtime is built (not the tests). The first build fetches and builds the
# libraries and takes a while; later ones only what changed.
set -euo pipefail

ARCH=${1:-aarch64}
case "$ARCH" in
  aarch64) TOOLCHAIN=cmake/toolchain-aarch64-linux-gnu.cmake ;;
  armhf)   TOOLCHAIN=cmake/toolchain-arm-linux-gnueabihf.cmake ;;
  *) echo "usage: $0 [aarch64|armhf]" >&2; exit 1 ;;
esac

SRC=$(cd "$(dirname "$0")/../.." && pwd)
WORK=${HML_WSL_WORK:-$HOME/.cache/tsukuraku}
COPY=$WORK/src
BUILD=$WORK/build-fbdev-$ARCH

mkdir -p "$COPY"
# what the build reads; build folders, the git history and the projects stay behind
rsync -a --delete \
  --exclude '/build*' --exclude '/.git' --exclude '/projects' --exclude 'node_modules' \
  --exclude '__pycache__' --exclude '/tools/designer/fonts' \
  "$SRC/" "$COPY/"

# about 1.5 GB of memory per compiler at the most: WSL often has little
JOBS=$(( $(awk '/MemAvailable/ { print int($2 / 1500000) }' /proc/meminfo) ))
[ "$JOBS" -lt 1 ] && JOBS=1
[ "$JOBS" -gt "$(nproc)" ] && JOBS=$(nproc)

GEN=()
command -v ninja >/dev/null 2>&1 && GEN=(-G Ninja)
if [ ! -f "$BUILD/CMakeCache.txt" ]; then
  cmake -S "$COPY" -B "$BUILD" -DCMAKE_BUILD_TYPE=Release -DHML_BACKEND=fbdev -DHML_TESTS=OFF \
        -DCMAKE_TOOLCHAIN_FILE="$TOOLCHAIN" "${GEN[@]}"
fi
cmake --build "$BUILD" -j"$JOBS" --target hml_runtime

OUT=$SRC/build-fbdev-$ARCH/bin
mkdir -p "$OUT"
cp "$BUILD/bin/hml_runtime" "$OUT/hml_runtime"
echo
echo "Built: build-fbdev-$ARCH/bin/hml_runtime (in $BUILD, $JOBS jobs)"
