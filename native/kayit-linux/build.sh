#!/bin/sh
# Builds the Zeyvola recording helper (Linux): the screen without the pointer, the camera, the
# microphone and the system sound, each into its own WebM file on one clock. GStreamer only.
# Output: bin/zeyvola-kayit
set -eu
cd "$(dirname "$0")"

OUT=bin
CC=${CC:-cc}
mkdir -p "$OUT"

CFLAGS="-O2 -std=c11 -Wall -Wextra -Wno-unused-parameter"
INCLUDES="$(pkg-config --cflags gstreamer-1.0 gstreamer-pbutils-1.0 gio-2.0)"
LIBS="$(pkg-config --libs gstreamer-1.0 gstreamer-pbutils-1.0 gio-2.0)"

$CC $CFLAGS $INCLUDES -o "$OUT/zeyvola-kayit" src/ana.c src/kayit-wayland.c $LIBS
strip "$OUT/zeyvola-kayit" 2>/dev/null || true
echo "Oluştu: $OUT/zeyvola-kayit"
