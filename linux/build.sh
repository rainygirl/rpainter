#!/bin/sh
# Linux: builds the RPainter binary and a .tar.gz in ../dist.
# Dependencies (Qt 6 or Qt 5, Widgets only):
#   Debian/Ubuntu: sudo apt install build-essential cmake qt6-base-dev qt6-image-formats-plugins
#   Fedora:        sudo dnf install gcc-c++ cmake qt6-qtbase-devel qt6-qtimageformats
#   Arch:          sudo pacman -S base-devel cmake qt6-base qt6-imageformats
# (the image formats plugin is only needed for WebP)
set -e
cd "$(dirname "$0")"
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/usr
cmake --build build -j"${JOBS:-$(nproc)}"
QT_QPA_PLATFORM=offscreen ./build/rpainter_selftest
STAGE="$(mktemp -d)"
DESTDIR="$STAGE/r-painter" cmake --install build
mkdir -p ../dist
OUT="../dist/r-painter-1.0.0-linux-$(uname -m).tar.gz"
tar -C "$STAGE" -czf "$OUT" r-painter
rm -rf "$STAGE"
echo "built $OUT (system-wide install: sudo cmake --install build)"
