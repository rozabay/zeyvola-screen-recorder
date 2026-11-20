#!/bin/sh
# Builds the Zeyvola pointer helper (Linux): mouse buttons and scrolling only. No capture, no
# codecs, no keyboard. X11 needs XInput2; Wayland needs xdg-desktop-portal RemoteDesktop and libei.
# Output: bin/zeyvola-imlec
set -eu
cd "$(dirname "$0")"

OUT=bin
WORK=.build
CC=${CC:-cc}
mkdir -p "$OUT" "$WORK"

CFLAGS="-O2 -std=c11 -Wall -Wextra -Wno-unused-parameter"
INCLUDES="$(pkg-config --cflags x11 xi xext xfixes gio-2.0 gio-unix-2.0 libei-1.0)"
LIBS="$(pkg-config --libs x11 xi xext xfixes gio-2.0 gio-unix-2.0 libei-1.0)"

$CC $CFLAGS $INCLUDES -o "$OUT/zeyvola-imlec" \
  src/ana.c src/imlec-x11.c src/imlec-wayland.c \
  $LIBS

strip "$OUT/zeyvola-imlec" 2>/dev/null || true
echo "Oluştu: $OUT/zeyvola-imlec"
