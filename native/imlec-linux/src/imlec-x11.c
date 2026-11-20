/*
 * X11 pointer path: XInput2 raw events.
 *
 * Raw events do not depend on any window having focus or grabbing, which is exactly what a
 * recording needs: the helper sees every button press and release on the whole desktop without
 * reading the keyboard. XI_RawMotion tells us motion happened; the position itself is read with
 * XQueryPointer, because raw motion values are relative deltas that depend on the device.
 */
#define _GNU_SOURCE
#include <errno.h>
#include <poll.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include <X11/Xlib.h>
#include <X11/extensions/XInput2.h>
#include <X11/keysym.h>

#include "imlec.h"
#include "olay.h"
#include "../../ortak/protokol.h"

/* The pointer as the app sees it: global top-left pixels (X11's root origin is already top-left). */
static nokta imleci_oku(Display *d, Window kok) {
  Window kok_donus, cocuk;
  int kok_x, kok_y, kazan_x, kazan_y;
  unsigned int maske;
  nokta p = {0, 0};
  if (XQueryPointer(d, kok, &kok_donus, &cocuk, &kok_x, &kok_y, &kazan_x, &kazan_y, &maske)) {
    p.gx = kok_x;
    p.gy = kok_y;
  }
  return p;
}

/* The recorded window's frame in root pixels: translate (0,0) and its size. */
static bool pencere_cercevesi(Display *d, Window kok, unsigned long pencere, nokta *sol_ust, double *w, double *h) {
  Window kok_donus, cocuk, ebeveyn;
  Window *cocuklar = NULL;
  unsigned int n = 0;
  XWindowAttributes va;
  int x = 0, y = 0;
  if (!XGetWindowAttributes(d, (Window)pencere, &va)) return false;
  if (!XTranslateCoordinates(d, (Window)pencere, kok, 0, 0, &x, &y, &kok_donus)) return false;
  XFree(cocuklar);
  (void)cocuk; (void)ebeveyn; (void)n;
  sol_ust->gx = (double)x;
  sol_ust->gy = (double)y;
  *w = (double)va.width;
  *h = (double)va.height;
  return *w > 0 && *h > 0;
}

int zeyvola_imlec_x11(unsigned long pencere) {
  Display *d = XOpenDisplay(NULL);
  if (!d) {
    zeyvola_hata("display", "X11 görüntü sunucusuna bağlanılamadı");
    return 2;
  }
  int xi_op, xi_olay, xi_hata;
  if (!XQueryExtension(d, "XInputExtension", &xi_op, &xi_olay, &xi_hata)) {
    XCloseDisplay(d);
    zeyvola_hata("monitor", "XInput2 uzantısı yok");
    return 2;
  }
  int buyuk = 2, kucuk = 2;
  if (XIQueryVersion(d, &buyuk, &kucuk) != Success) {
    XCloseDisplay(d);
    zeyvola_hata("monitor", "XInput2 2.2 gerekli");
    return 2;
  }

  Window kok = DefaultRootWindow(d);
  /* Raw events are selected on the root window: an input-only child window answers
   * BadValue for XI_RawMotion even though XIQueryVersion reports 2.2. */
  unsigned char maske[XIMaskLen(XI_LASTEVENT)];
  memset(maske, 0, sizeof maske);
  XISetMask(maske, XI_RawButtonPress);
  XISetMask(maske, XI_RawButtonRelease);
  XISetMask(maske, XI_RawMotion);
  XIEventMask em;
  em.deviceid = XIAllDevices;
  em.mask_len = sizeof maske;
  em.mask = maske;
  XISelectEvents(d, kok, &em, 1);
  XFlush(d);

  int fd = ConnectionNumber(d);
  nokta p = imleci_oku(d, kok);

  /* The window's frame, written once now and whenever it changes (about 30 times a second). */
  nokta son_sol = {0, 0};
  double son_w = 0, son_h = 0;
  bool cerceve_var = false;
  if (pencere) cerceve_var = pencere_cercevesi(d, kok, pencere, &son_sol, &son_w, &son_h);
  if (cerceve_var) {
    zeyvola_pencere_yaz(zeyvola_simdi(), son_sol.gx, son_sol.gy, son_w, son_h);
  }
  zeyvola_hazir_yaz();

  double son_cerceve = zeyvola_simdi();
  /* Wheel button numbers: 4/5 vertical, 6/7 horizontal (X11's core XButtonEvent convention). */
  int basili = 0;
  char tampon[4096];

  for (;;) {
    struct pollfd pfd[2];
    pfd[0].fd = fd;
    pfd[0].events = POLLIN;
    pfd[1].fd = STDIN_FILENO;
    pfd[1].events = POLLIN;
    int hazir = poll(pfd, 2, 33); /* ~30 Hz: also paces the window-bounds check */
    if (hazir < 0) {
      if (errno == EINTR) continue;
      break;
    }
    if (pfd[1].revents & (POLLIN | POLLHUP)) {
      if (zeyvola_durdur_okundu(STDIN_FILENO)) break;
    }
    if (hazir == 0) {
      /* Timeout: refresh the window frame if it moved. */
      if (cerceve_var && zeyvola_simdi() - son_cerceve > 1.0 / 30.0) {
        son_cerceve = zeyvola_simdi();
        nokta sol;
        double w, h;
        if (pencere_cercevesi(d, kok, pencere, &sol, &w, &h) &&
            (sol.gx != son_sol.gx || sol.gy != son_sol.gy || w != son_w || h != son_h)) {
          son_sol = sol; son_w = w; son_h = h;
          zeyvola_pencere_yaz(son_cerceve, sol.gx, sol.gy, w, h);
        }
      }
      continue;
    }
    if (!(pfd[0].revents & POLLIN)) continue;

    while (XPending(d)) {
      XEvent ev;
      XNextEvent(d, &ev);
      XGenericEventCookie *cookie = &ev.xcookie;
      if (cookie->type != GenericEvent || cookie->extension != xi_op) continue;
      if (!XGetEventData(d, cookie)) continue;
      const double at = zeyvola_simdi();
      switch (cookie->evtype) {
        case XI_RawButtonPress:
        case XI_RawButtonRelease: {
          XIRawEvent *re = cookie->data;
          const int dugme = re->detail;
          p = imleci_oku(d, kok);
          if (dugme == 4 || dugme == 5 || dugme == 6 || dugme == 7) {
            /* A wheel turn is a press/release pair around one delta; report it once, on press. */
            if (cookie->evtype == XI_RawButtonPress) {
              const double dy = (dugme == 4) ? 1.0 : (dugme == 5) ? -1.0 : 0.0;
              const double dx = (dugme == 6) ? 1.0 : (dugme == 7) ? -1.0 : 0.0;
              (void)dx;
              zeyvola_olay_yaz('s', NULL, dy, at, p);
            }
            break;
          }
          const int bit = (dugme == 1) ? 1 : (dugme == 3) ? 2 : 4;
          const bool indi = cookie->evtype == XI_RawButtonPress;
          if (((basili & bit) != 0) == indi) break; /* repeated report of the same state */
          basili = indi ? (basili | bit) : (basili & ~bit);
          zeyvola_olay_yaz(indi ? 'd' : 'u', bit == 1 ? "l" : bit == 2 ? "r" : "o", 0, at, p);
          break;
        }
        case XI_RawMotion:
          /* Motion alone is not an event the app records (it polls the pointer itself); the new
           * position is picked up by the next button or scroll, so nothing is written here. */
          p = imleci_oku(d, kok);
          break;
        default: break;
      }
      XFreeEventData(d, cookie);
    }
    (void)tampon;
  }

  XCloseDisplay(d);
  return 0;
}
