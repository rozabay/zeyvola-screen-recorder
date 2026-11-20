#!/bin/sh
# Builds the Zeyvola sound helper (Linux): the computer's own sound (PipeWire/PulseAudio monitor)
# and the sound of imported videos, both as Opus in WebM. GStreamer only.
# Output: bin/zeyvola-ses
set -eu
cd "$(dirname "$0")"

OUT=bin
CC=${CC:-cc}
mkdir -p "$OUT"

CFLAGS="-O2 -std=c11 -Wall -Wextra -Wno-unused-parameter"
INCLUDES="$(pkg-config --cflags gstreamer-1.0 gio-2.0)"
LIBS="$(pkg-config --libs gstreamer-1.0 gio-2.0)"

$CC $CFLAGS $INCLUDES -o "$OUT/zeyvola-ses" src/ana.c $LIBS
strip "$OUT/zeyvola-ses" 2>/dev/null || true
echo "Oluştu: $OUT/zeyvola-ses"
