/*
 * Zeyvola pointer helper (Linux).
 *
 * Started by the Zeyvola app while a screen recording runs, so the editor knows where and when the
 * user clicked (automatic zoom). It reports mouse button and scroll events only: it records no
 * video, no audio and never reads the keyboard. It writes nothing to disk and makes no network
 * requests.
 *
 *   zeyvola-imlec [--window <X11 window id>]
 *
 * --window (a window recording) asks for that window's position too, so the app can make positions
 * relative to it. X11 only: Wayland gives no window geometry without a compositor-specific
 * protocol, so it is ignored there.
 *
 * stdout, one JSON object per line — the same protocol as the macOS helper, so
 * apps/desktop/src/main/capture/input-helper.ts reads it unchanged:
 *   {"type":"window","at":<s>,"now":<s>,"gx":<pt>,"gy":<pt>,"w":<pt>,"h":<pt>}   window bounds
 *   {"type":"ready","now":<s>}
 *   {"e":"d"|"u","b":"l"|"r"|"o","at":<s>,"now":<s>,"gx":<pt>,"gy":<pt>}   button down / up
 *   {"e":"s","dy":<pt>,"at":<s>,"now":<s>,"gx":<pt>,"gy":<pt>}           scroll
 *   {"type":"error","code":"monitor"|"portal"|"display"}                  could not start
 * at = event time, now = time of writing, both in seconds since boot (CLOCK_MONOTONIC); the app
 * maps them onto its recording clock. gx/gy are global pixels with a top-left origin on the
 * display arrangement, like Electron's screen coordinates.
 *
 * Per session type:
 *   X11      XInput2 raw events (XI_RawButtonPress/Release, XI_RawMotion) through an input-only
 *            window; the pointer position comes from XQueryPointer. No permission is needed.
 *   Wayland  xdg-desktop-portal RemoteDesktop: the user approves remote control once, and the
 *            portal hands over a socket that delivers buttons and relative motion. Absolute
 *            positions are then accumulated by the helper. Without the portal the helper reports
 *            "portal" and the recording simply has no click data (it goes on without it).
 *
 * Exits when stdin closes or a "stop" line arrives.
 */
#define _GNU_SOURCE
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "imlec.h"
#include "../../ortak/protokol.h"

/* "wayland" or "x11": the session type, decided once (XDG_SESSION_TYPE, else the display vars). */
static bool wayland_oturumu(void) {
  const char *t = getenv("XDG_SESSION_TYPE");
  if (t && strcasecmp(t, "wayland") == 0) return true;
  if (t && strcasecmp(t, "x11") == 0) return false;
  return getenv("WAYLAND_DISPLAY") != NULL;
}

int main(int argc, char **argv) {
  zeyvola_baslat_stdout();

  unsigned long pencere = 0;
  bool kendini_deneme = false;
  for (int i = 1; i < argc; i++) {
    if (strcmp(argv[i], "--window") == 0 && i + 1 < argc) pencere = strtoul(argv[++i], NULL, 10);
    else if (strcmp(argv[i], "--self-test") == 0) kendini_deneme = true;
  }
  if (kendini_deneme) {
    /* Reports what this build can do without touching a device (tests and the smoke suite). */
    printf("{\"type\":\"selftest\",\"ok\":true,\"session\":\"%s\",\"window\":%lu}\n", wayland_oturumu() ? "wayland" : "x11", pencere);
    return 0;
  }
  return wayland_oturumu() ? zeyvola_imlec_wayland(pencere) : zeyvola_imlec_x11(pencere);
}
