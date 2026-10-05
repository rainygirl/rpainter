#!/bin/sh
# Haiku: builds the native RPainter binary (Be API, no Qt) and an installable .hpkg in ../dist.
# Dependencies: pkgman install gcc binutils make cmake haiku_devel
set -e
cd "$(dirname "$0")"
VER=1.0.0
ARCH="$(getarch)"
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j"$(nproc)"
./build/rpainter_engine_test
# application signature and version info
rc -o build/RPainter.rsrc RPainter.rdef
xres -o build/RPainter build/RPainter.rsrc
mimeset -f build/RPainter
STAGE="$(mktemp -d)"
mkdir -p "$STAGE/apps" "$STAGE/data/deskbar/menu/Applications"
cp build/RPainter "$STAGE/apps/RPainter"
copyattr -d build/RPainter "$STAGE/apps/RPainter" 2>/dev/null || true
ln -s ../../../../apps/RPainter "$STAGE/data/deskbar/menu/Applications/R Painter"
cat > "$STAGE/.PackageInfo" <<INFO
name			rpainter
version			$VER-1
architecture	$ARCH
summary			"Layer-based image editor"
description		"R Painter is a layer-based image editor: layers and blend modes, selections, magic wand cutouts, color adjustments, resizing and PNG/JPG/WebP export."
packager		"rainygirl <rainygirl@gmail.com>"
vendor			"rainygirl"
copyrights		{ "2026 Lee JunHaeng" }
licenses		{ "MIT" }
provides {
	rpainter = $VER
	app:RPainter = $VER
}
requires {
	haiku
}
INFO
mkdir -p ../dist
OUT="../dist/rpainter-$VER-1-$ARCH.hpkg"
rm -f "$OUT"
package create -C "$STAGE" "$OUT"
rm -rf "$STAGE"
echo "built $OUT (install: pkgman install \"$OUT\")"
