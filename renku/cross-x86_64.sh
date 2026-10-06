#!/bin/sh
# Cross-compile the RenkuOS (Haiku) front end for x86_64 on a workstation with
# Docker, into dist/x86_64/RPainter. The binary carries no resources: the
# pkgman recipe adds the icon and attributes on Haiku and makes the package.
#
#   ./renku/cross-x86_64.sh
#
# Uses the haiku/cross-compiler:x86_64-r1beta4 image (x86_64-unknown-haiku-g++
# with a sysroot). Sources go in and the binary comes out through tar over
# stdin, since a bind mount from /private/tmp does not reach Docker on macOS.
# The same sources renku/CMakeLists.txt lists: the toolkit-free engine, the Be
# API front end and the bundled libwebp.
set -e
cd "$(dirname "$0")/.."
OUT=dist/x86_64
mkdir -p "$OUT"
COPYFILE_DISABLE=1 tar cf - --no-xattrs --exclude '*/build' engine renku \
	| docker run --rm -i --platform linux/amd64 haiku/cross-compiler:x86_64-r1beta4 sh -c '
		set -e
		mkdir -p /work && cd /work && tar xf -
		CC=x86_64-unknown-haiku-gcc
		CXX=x86_64-unknown-haiku-g++
		W=renku/third_party/libwebp
		for f in $W/src/dec/*.c $W/src/dsp/*.c $W/src/enc/*.c $W/src/utils/*.c $W/sharpyuv/*.c; do
			$CC -O2 -I$W -c "$f" -o "w_$(echo "$f" | tr / _).o"
		done
		for f in engine/raster.cpp engine/doc.cpp engine/tools.cpp engine/i18n.cpp \
				renku/src/app.cpp renku/src/stb_impl.cpp; do
			$CXX -std=c++17 -O2 -Iengine -c "$f" -o "o_$(basename "$f" .cpp).o"
		done
		$CXX o_*.o w_*.o -o RPainter -lbe -ltranslation -ltracker
		tar cf - RPainter' \
	| tar xf - -C "$OUT"
file "$OUT/RPainter"
