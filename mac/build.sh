#!/bin/sh
# macOS: builds "R Painter.app" (AppKit, no Qt) and a .dmg in ../dist.
# Needs the Xcode command line tools and libwebp for WebP export (brew install webp); libwebp is linked statically.
# Targets macOS 26 or newer, which is what Homebrew builds libwebp for.
set -e
cd "$(dirname "$0")"
WEBP="${WEBP_PREFIX:-$(brew --prefix webp)}"
APP="build/R Painter.app"
rm -rf build
mkdir -p "$APP/Contents/MacOS" "$APP/Contents/Resources"
CXX="clang++ -std=c++17 -O2 -Wall -Wno-unused-lambda-capture -mmacosx-version-min=26.0 -I ../engine"
$CXX ../engine/raster.cpp ../engine/doc.cpp ../engine/tools.cpp ../engine/i18n.cpp ../engine/tests/engine_test.cpp -o build/rpainter_engine_test
./build/rpainter_engine_test | tail -1
$CXX -fobjc-arc -I "$WEBP/include" src/app.mm ../engine/raster.cpp ../engine/doc.cpp ../engine/tools.cpp ../engine/i18n.cpp \
  "$WEBP/lib/libwebp.a" "$WEBP/lib/libsharpyuv.a" \
  -framework Cocoa -framework ImageIO -framework UniformTypeIdentifiers -framework Carbon \
  -o "$APP/Contents/MacOS/RPainter"
cp Info.plist "$APP/Contents/Info.plist"
cp r-painter.icns "$APP/Contents/Resources/r-painter.icns"
codesign --force -s - "$APP"
mkdir -p ../dist
OUT="../dist/r-painter-1.0.0-mac-$(uname -m).dmg"
rm -f "$OUT"
# the disk image shows the app next to a link to /Applications
STAGE="$(mktemp -d)"
cp -R "$APP" "$STAGE/"
ln -s /Applications "$STAGE/Applications"
hdiutil create -quiet -volname "R Painter" -srcfolder "$STAGE" -format UDZO "$OUT"
rm -rf "$STAGE"
echo "built $OUT"
