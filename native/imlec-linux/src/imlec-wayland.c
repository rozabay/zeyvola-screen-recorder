/*
 * Wayland pointer path: xdg-desktop-portal RemoteDesktop + libei.
 *
 * On Wayland an application cannot watch the pointer globally. The sanctioned route is the desktop
 * portal: the user approves "remote control" of this session once, the portal starts an EIS server
 * and hands us a socket (ConnectToEIS). Buttons and relative motion then arrive over that socket;
 * absolute positions are accumulated here.
 *
 * A compositor that has not implemented RemoteDesktop, or a portal without EIS support, leads to a
 * "portal" error: the recording continues without click data, exactly like a missing helper on the
 * other platforms.
 *
 * The portal is spoken to through GDBus (gio-2.0): the Request/Response dance, the a{sv} option
 * dictionaries and the unix fd handover are all plain GVariant work there, and GIO is already a
 * hard dependency of any desktop that runs the portal.
 */
#define _GNU_SOURCE
#include <errno.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <gio/gio.h>
#include <gio/gunixfdlist.h>
#include <libei.h>

#include "imlec.h"
#include "olay.h"
#include "../../ortak/protokol.h"

#define PORTAL_ADI "org.freedesktop.portal.Desktop"
#define PORTAL_YOL "/org/freedesktop/portal/desktop"
#define REMOTE_ARAYUZ "org.freedesktop.portal.RemoteDesktop"
#define ISTEK_ARAYUZ "org.freedesktop.portal.Request"

/* Bit values of AvailableDeviceTypes / SelectDevices "types": pointer only, never keyboard. */
#define TUR_IMLEC 1u

typedef struct {
  GDBusConnection *bag;
  char *oturum; /* the session handle (an object path) */
  int eis_fd;
} portal_durum;

/** What a waiting Response handler needs; pointed to by the subscription's user data. */
typedef struct {
  GMainLoop *dongu;
  guint *kod;
  GVariant **sonuc;
  const char *yol;
} yanit_bekleyen;

/** org.freedesktop.portal.Request.Response(u, a{sv}) for our request path. */
static void yanit_geldi(GDBusConnection *bag, const char *gonderen, const char *yol,
                        const char *arayuz, const char *sinyal, GVariant *govde, gpointer veri) {
  (void)bag; (void)gonderen; (void)arayuz; (void)sinyal;
  yanit_bekleyen *b = veri;
  if (g_strcmp0(yol, b->yol) != 0) return;
  if (!govde || !g_variant_is_of_type(govde, G_VARIANT_TYPE("(ua{sv})"))) return;
  guint k = 0;
  GVariant *sonuc = NULL;
  g_variant_get(govde, "(u@a{sv})", &k, &sonuc);
  *b->kod = k;
  if (b->sonuc) *b->sonuc = sonuc; else g_variant_unref(sonuc);
  g_main_loop_quit(b->dongu);
}

/** Ends the wait after five minutes: the user may be reading the dialog, but not forever. */
static gboolean yanit_zaman_doldu(gpointer veri) {
  g_main_loop_quit((GMainLoop *)veri);
  return G_SOURCE_REMOVE;
}

static void portal_temizle(portal_durum *p) {
  if (p->eis_fd >= 0) close(p->eis_fd);
  g_free(p->oturum);
  if (p->bag) g_object_unref(p->bag);
  memset(p, 0, sizeof *p);
  p->eis_fd = -1;
}

/* The portal answers a Request object with a Response(u, a{sv}) signal; this waits for it. */
static guint istek_yaniti(portal_durum *p, const char *istek_yolu, GVariant **sonuc) {
  GMainLoop *dongu = g_main_loop_new(NULL, FALSE);
  guint kod = 2; /* the portal's "other error" */
  yanit_bekleyen bekleyen = { dongu, &kod, sonuc, istek_yolu };
  const guint abone = g_dbus_connection_signal_subscribe(
      p->bag, PORTAL_ADI, ISTEK_ARAYUZ, "Response", NULL, NULL, G_DBUS_SIGNAL_FLAGS_NONE,
      yanit_geldi, &bekleyen, NULL);
  const guint zamanlayici = g_timeout_add_seconds(300, yanit_zaman_doldu, dongu);
  g_main_loop_run(dongu);
  g_source_remove(zamanlayici);
  g_dbus_connection_signal_unsubscribe(p->bag, abone);
  g_main_loop_unref(dongu);
  return kod;
}

/* One portal call that returns a Request handle, then its Response; NULL on failure. */
static GVariant *istekli_cagri(portal_durum *p, const char *uye, GVariant *parametreler, GError **hata) {
  GVariant *cevap = g_dbus_connection_call_sync(p->bag, PORTAL_ADI, PORTAL_YOL, REMOTE_ARAYUZ, uye,
                                                parametreler, G_VARIANT_TYPE("(o)"),
                                                G_DBUS_CALL_FLAGS_NONE, -1, NULL, hata);
  if (!cevap) return NULL;
  const char *istek = NULL;
  g_variant_get(cevap, "(&o)", &istek);
  char *kopya = g_strdup(istek);
  g_variant_unref(cevap);
  GVariant *sonuc = NULL;
  guint kod = istek_yaniti(p, kopya, &sonuc);
  g_free(kopya);
  if (kod != 0) {
    if (sonuc) g_variant_unref(sonuc);
    g_set_error(hata, G_IO_ERROR, G_IO_ERROR_FAILED, "portal yanıtı %u", kod);
    return NULL;
  }
  return sonuc;
}

/* CreateSession → SelectDevices → Start → ConnectToEIS(fd). */
static int oturum_ac(portal_durum *p, GError **hata) {
  /* CreateSession(a{sv}) */
  GVariantBuilder secenekler;
  g_variant_builder_init(&secenekler, G_VARIANT_TYPE_VARDICT);
  char jeton[64];
  snprintf(jeton, sizeof jeton, "zeyvola_%d", (int)getpid());
  g_variant_builder_add(&secenekler, "{sv}", "session_handle_token", g_variant_new_string(jeton));
  GVariant *sonuc = istekli_cagri(p, "CreateSession", g_variant_new("(a{sv})", &secenekler), hata);
  if (!sonuc) return -1;
  if (!g_variant_lookup(sonuc, "session_handle", "s", &p->oturum)) {
    g_variant_unref(sonuc);
    g_set_error(hata, G_IO_ERROR, G_IO_ERROR_FAILED, "oturum tanıtıcısı yok");
    return -1;
  }
  g_variant_unref(sonuc);

  /* SelectDevices(session, a{sv} {types: u}) — pointer only. It returns no Response signal. */
  GVariantBuilder sec;
  g_variant_builder_init(&sec, G_VARIANT_TYPE_VARDICT);
  g_variant_builder_add(&sec, "{sv}", "types", g_variant_new_uint32(TUR_IMLEC));
  GError *yerel = NULL;
  GVariant *sec_cevap = g_dbus_connection_call_sync(
      p->bag, PORTAL_ADI, PORTAL_YOL, REMOTE_ARAYUZ, "SelectDevices",
      g_variant_new("(oa{sv})", p->oturum, &sec), NULL, G_DBUS_CALL_FLAGS_NONE, -1, NULL, &yerel);
  if (!sec_cevap) {
    g_propagate_error(hata, yerel);
    return -1;
  }
  g_variant_unref(sec_cevap);

  /* Start(session, parent_window, a{sv}) — the user's approval dialog. */
  GVariantBuilder bas_sec;
  g_variant_builder_init(&bas_sec, G_VARIANT_TYPE_VARDICT);
  sonuc = istekli_cagri(p, "Start", g_variant_new("(osa{sv})", p->oturum, "", &bas_sec), hata);
  if (!sonuc) return -1;
  g_variant_unref(sonuc);

  /* ConnectToEIS(session, a{sv}) → h (an fd in the reply's fd list). */
  GVariantBuilder eis_sec;
  g_variant_builder_init(&eis_sec, G_VARIANT_TYPE_VARDICT);
  GUnixFDList *fd_listesi = NULL;
  GError *eis_hata = NULL;
  GVariant *eis = g_dbus_connection_call_with_unix_fd_list_sync(
      p->bag, PORTAL_ADI, PORTAL_YOL, REMOTE_ARAYUZ, "ConnectToEIS",
      g_variant_new("(oa{sv})", p->oturum, &eis_sec), G_VARIANT_TYPE("(h)"),
      G_DBUS_CALL_FLAGS_NONE, -1, NULL, &fd_listesi, NULL, &eis_hata);
  if (!eis) {
    g_propagate_error(hata, eis_hata);
    if (fd_listesi) g_object_unref(fd_listesi);
    return -1;
  }
  gint32 idx = -1;
  g_variant_get(eis, "(h)", &idx);
  g_variant_unref(eis);
  if (!fd_listesi) {
    g_set_error(hata, G_IO_ERROR, G_IO_ERROR_FAILED, "EIS soketi gelmedi");
    return -1;
  }
  p->eis_fd = g_unix_fd_list_get(fd_listesi, idx, hata);
  g_object_unref(fd_listesi);
  return p->eis_fd >= 0 ? 0 : -1;
}

int zeyvola_imlec_wayland(unsigned long pencere) {
  (void)pencere; /* no window geometry on Wayland */
  portal_durum p;
  memset(&p, 0, sizeof p);
  p.eis_fd = -1;
  GError *hata = NULL;
  p.bag = g_bus_get_sync(G_BUS_TYPE_SESSION, NULL, &hata);
  if (!p.bag) {
    zeyvola_hata_yaz("imlec", "portal", hata ? hata->message : "oturum veri yolu yok");
    if (hata) g_error_free(hata);
    zeyvola_hata("portal", "D-Bus oturum veri yoluna bağlanılamadı");
    return 2;
  }
  if (oturum_ac(&p, &hata) != 0) {
    zeyvola_hata_yaz("imlec", "portal", hata ? hata->message : "ekran paylaşımı portalı açılamadı");
    if (hata) g_error_free(hata);
    portal_temizle(&p);
    zeyvola_hata("portal", "ekran paylaşımı portalı açılamadı ya da onaylanmadı");
    return 4;
  }

  struct ei *ei = ei_new(NULL);
  if (!ei || !ei_setup_backend_fd(ei, p.eis_fd)) {
    if (ei) ei_unref(ei);
    portal_temizle(&p);
    zeyvola_hata("portal", "EIS soketi kurulamadı (libei)");
    return 5;
  }

  nokta konum = {0, 0};
  int basili = 0;
  zeyvola_hazir_yaz();

  for (;;) {
    ei_dispatch(ei);
    struct ei_event *ev;
    while ((ev = ei_get_event(ei))) {
      const double at = zeyvola_simdi();
      enum ei_event_type t = ei_event_get_type(ev);
      if (t == EI_EVENT_POINTER_MOTION) {
        konum.gx += ei_event_pointer_get_dx(ev);
        konum.gy += ei_event_pointer_get_dy(ev);
      } else if (t == EI_EVENT_POINTER_MOTION_ABSOLUTE) {
        konum.gx = ei_event_pointer_get_absolute_x(ev);
        konum.gy = ei_event_pointer_get_absolute_y(ev);
      } else if (t == EI_EVENT_BUTTON_BUTTON) {
        const unsigned dugme = ei_event_button_get_button(ev);
        const bool indi = ei_event_button_get_is_press(ev);
        const int bit = (dugme == 0x110) ? 1 : (dugme == 0x111) ? 2 : 4; /* BTN_LEFT / BTN_RIGHT */
        if (((basili & bit) != 0) == indi) { ei_event_unref(ev); continue; }
        basili = indi ? (basili | bit) : (basili & ~bit);
        zeyvola_olay_yaz(indi ? 'd' : 'u', bit == 1 ? "l" : bit == 2 ? "r" : "o", 0, at, konum);
      } else if (t == EI_EVENT_SCROLL_DISCRETE) {
        /* Whole notches: positive is down; X11's wheel-up is positive for the app. */
        const int32_t v = ei_event_scroll_get_discrete_dy(ev);
        if (v) zeyvola_olay_yaz('s', NULL, (double)-v, at, konum);
      } else if (t == EI_EVENT_DISCONNECT) {
        ei_event_unref(ev);
        ei_unref(ei);
        portal_temizle(&p);
        return 0;
      }
      ei_event_unref(ev);
    }
    struct pollfd pfd[2];
    pfd[0].fd = ei_get_fd(ei);
    pfd[0].events = POLLIN;
    pfd[1].fd = STDIN_FILENO;
    pfd[1].events = POLLIN;
    const int hazir = poll(pfd, 2, 20);
    if (hazir < 0 && errno != EINTR) break;
    if (pfd[1].revents & (POLLIN | POLLHUP)) {
      if (zeyvola_durdur_okundu(STDIN_FILENO)) break;
    }
  }

  ei_unref(ei);
  portal_temizle(&p);
  return 0;
}
