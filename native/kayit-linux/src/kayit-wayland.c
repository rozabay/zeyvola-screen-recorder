/*
 * Wayland screen path: xdg-desktop-portal ScreenCast.
 *
 * On Wayland no application may read the screen directly; the compositor shows a dialog, the user
 * picks what to share, and the portal hands back a PipeWire node. This file does that dance and
 * returns the node id and a PipeWire remote fd, which the recorder then feeds to pipewiresrc.
 *
 *   CreateSession → SelectSources(monitor, cursor hidden) → Start → node id + OpenPipeWireRemote fd
 *
 * "cursor hidden" (cursor_mode = 4, METADATA) keeps the pointer out of the video, exactly like
 * ximagesrc show-pointer=false on X11: Zeyvola draws the pointer itself from cursor.jsonl.
 */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <gio/gio.h>
#include <gio/gunixfdlist.h>

#include "ekran.h"
#include "../../ortak/protokol.h"

#define PORTAL_ADI "org.freedesktop.portal.Desktop"
#define PORTAL_YOL "/org/freedesktop/portal/desktop"
#define EKRAN_ARAYUZ "org.freedesktop.portal.ScreenCast"
#define ISTEK_ARAYUZ "org.freedesktop.portal.Request"

/* Source types: 1 = monitor, 2 = window, 4 = virtual. Cursor modes: 4 = metadata (not drawn). */
#define KAYNAK_MONITOR 1u
#define IMLEC_METADATA 4u

typedef struct {
  GDBusConnection *bag;
  GMainLoop *dongu;
  guint kod;
  GVariant *sonuc;
  const char *yol;
} yanit;

static void yanit_geldi(GDBusConnection *bag, const char *gonderen, const char *yol,
                        const char *arayuz, const char *sinyal, GVariant *govde, gpointer veri) {
  (void)bag; (void)gonderen; (void)arayuz; (void)sinyal;
  yanit *y = veri;
  if (g_strcmp0(yol, y->yol) != 0) return;
  if (!govde || !g_variant_is_of_type(govde, G_VARIANT_TYPE("(ua{sv})"))) return;
  guint kod = 0;
  GVariant *sonuc = NULL;
  g_variant_get(govde, "(u@a{sv})", &kod, &sonuc);
  y->kod = kod;
  if (y->sonuc) g_variant_unref(y->sonuc);
  y->sonuc = sonuc;
  g_main_loop_quit(y->dongu);
}

static gboolean zaman_doldu(gpointer veri) {
  g_main_loop_quit((GMainLoop *)veri);
  return G_SOURCE_REMOVE;
}

/* Calls a Request-returning portal method and waits for its Response.
 *
 * The Response signal can arrive before the method's own reply is processed (the portal answers
 * both in the same main context), so the subscription is made *before* the call: subscribing
 * afterwards loses the signal when it was already delivered — that was the reason the share
 * dialog appeared to do nothing. */
static GVariant *istek(GDBusConnection *bag, const char *uye, GVariant *parametreler, GError **hata) {
  /* The request path is derived from our unique name and the token, so it is known up front. */
  char jeton[64];
  snprintf(jeton, sizeof jeton, "zeyvola_%s_%s", uye, g_getenv("ZEYVOLA_PORTAL_TOKEN") ? g_getenv("ZEYVOLA_PORTAL_TOKEN") : "0");
  const char *benzersiz = g_dbus_connection_get_unique_name(bag);
  /* ":1.42" -> "1_42" (the portal's Request path form). */
  char *ad = g_strdup(benzersiz + 1);
  for (char *p = ad; *p; p++) if (*p == '.') *p = '_';
  char *beklenen = g_strdup_printf("%s/request/%s/%s", PORTAL_YOL, ad, jeton);
  g_free(ad);

  GMainLoop *dongu = g_main_loop_new(NULL, FALSE);
  yanit y = { bag, dongu, 2, NULL, beklenen };
  /* Subscribed for every Response on the portal, then filtered by path in the handler: a filter on
   * a path that is not yet known would have to be changed later, losing early signals. */
  const guint abone = g_dbus_connection_signal_subscribe(
      bag, PORTAL_ADI, ISTEK_ARAYUZ, "Response", NULL, NULL, G_DBUS_SIGNAL_FLAGS_NONE,
      yanit_geldi, &y, NULL);
  const guint zamanlayici = g_timeout_add_seconds(120, zaman_doldu, dongu);

  GVariant *cevap = g_dbus_connection_call_sync(bag, PORTAL_ADI, PORTAL_YOL, EKRAN_ARAYUZ, uye,
                                                parametreler, G_VARIANT_TYPE("(o)"),
                                                G_DBUS_CALL_FLAGS_NONE, -1, NULL, hata);
  if (!cevap) {
    g_source_remove(zamanlayici);
    g_dbus_connection_signal_unsubscribe(bag, abone);
    g_main_loop_unref(dongu);
    g_free(beklenen);
    return NULL;
  }
  const char *istek_yolu = NULL;
  g_variant_get(cevap, "(&o)", &istek_yolu);
  /* The portal may hand back a different path than the one guessed; follow what it says. */
  if (g_strcmp0(istek_yolu, y.yol) != 0) {
    g_free(beklenen);
    y.yol = g_strdup(istek_yolu);
    beklenen = (char *)y.yol;
  }
  g_variant_unref(cevap);

  g_main_loop_run(dongu);
  g_source_remove(zamanlayici);
  g_dbus_connection_signal_unsubscribe(bag, abone);
  g_main_loop_unref(dongu);
  g_free(beklenen);
  if (y.kod != 0 || !y.sonuc) {
    if (y.sonuc) g_variant_unref(y.sonuc);
    g_set_error(hata, G_IO_ERROR, G_IO_ERROR_FAILED,
                y.kod == 1 ? "kullanıcı ekran paylaşımını onaylamadı" : "ekran paylaşımı portalı yanıt vermedi");
    return NULL;
  }
  return y.sonuc;
}

int zeyvola_ekran_wayland(unsigned int *dugum, int *uzak_fd, char **geri_yukleme_jetonu, GError **hata) {
  *dugum = 0;
  *uzak_fd = -1;
  if (geri_yukleme_jetonu) *geri_yukleme_jetonu = NULL;

  GDBusConnection *bag = g_bus_get_sync(G_BUS_TYPE_SESSION, NULL, hata);
  if (!bag) return -1;

  /* 1) CreateSession */
  GVariantBuilder sec;
  g_variant_builder_init(&sec, G_VARIANT_TYPE_VARDICT);
  char jeton[64];
  snprintf(jeton, sizeof jeton, "zeyvola_%d", (int)getpid());
  g_variant_builder_add(&sec, "{sv}", "session_handle_token", g_variant_new_string(jeton));
  GVariant *sonuc = istek(bag, "CreateSession", g_variant_new("(a{sv})", &sec), hata);
  if (!sonuc) { g_object_unref(bag); return -1; }
  char *oturum = NULL;
  g_variant_lookup(sonuc, "session_handle", "s", &oturum);
  g_variant_unref(sonuc);
  if (!oturum) { g_object_unref(bag); return -1; }

  /* 2) SelectSources: a monitor, the pointer kept OUT of the video (cursor_mode = metadata). */
  GVariantBuilder kaynak_sec;
  g_variant_builder_init(&kaynak_sec, G_VARIANT_TYPE_VARDICT);
  g_variant_builder_add(&kaynak_sec, "{sv}", "types", g_variant_new_uint32(KAYNAK_MONITOR));
  g_variant_builder_add(&kaynak_sec, "{sv}", "cursor_mode", g_variant_new_uint32(IMLEC_METADATA));
  g_variant_builder_add(&kaynak_sec, "{sv}", "multiple", g_variant_new_boolean(FALSE));
  sonuc = istek(bag, "SelectSources", g_variant_new("(oa{sv})", oturum, &kaynak_sec), hata);
  if (!sonuc) { g_free(oturum); g_object_unref(bag); return -1; }
  g_variant_unref(sonuc);

  /* 3) Start: the user's dialog; results carry the stream's PipeWire node id and a restore token. */
  GVariantBuilder bas_sec;
  g_variant_builder_init(&bas_sec, G_VARIANT_TYPE_VARDICT);
  sonuc = istek(bag, "Start", g_variant_new("(osa{sv})", oturum, "", &bas_sec), hata);
  if (!sonuc) { g_free(oturum); g_object_unref(bag); return -1; }
  GVariant *akislar = g_variant_lookup_value(sonuc, "streams", G_VARIANT_TYPE("a(ua{sv})"));
  if (akislar) {
    GVariantIter it;
    guint node = 0;
    g_variant_iter_init(&it, akislar);
    if (g_variant_iter_next(&it, "(u@a{sv})", &node, NULL)) *dugum = node;
    g_variant_unref(akislar);
  }
  if (geri_yukleme_jetonu) {
    char *jeton2 = NULL;
    if (g_variant_lookup(sonuc, "restore_token", "s", &jeton2)) *geri_yukleme_jetonu = jeton2;
  }
  g_variant_unref(sonuc);

  /* 4) OpenPipeWireRemote: the fd through which pipewiresrc reaches the node. */
  GVariantBuilder uzak_sec;
  g_variant_builder_init(&uzak_sec, G_VARIANT_TYPE_VARDICT);
  GUnixFDList *fd_listesi = NULL;
  GError *uzak_hata = NULL;
  GVariant *fd_cevap = g_dbus_connection_call_with_unix_fd_list_sync(
      bag, PORTAL_ADI, PORTAL_YOL, EKRAN_ARAYUZ, "OpenPipeWireRemote",
      g_variant_new("(oa{sv})", oturum, &uzak_sec), G_VARIANT_TYPE("(h)"),
      G_DBUS_CALL_FLAGS_NONE, -1, NULL, &fd_listesi, NULL, &uzak_hata);
  g_free(oturum);
  g_object_unref(bag);
  if (!fd_cevap) {
    g_propagate_error(hata, uzak_hata);
    if (fd_listesi) g_object_unref(fd_listesi);
    return -1;
  }
  gint32 idx = -1;
  g_variant_get(fd_cevap, "(h)", &idx);
  g_variant_unref(fd_cevap);
  if (!fd_listesi) { g_set_error(hata, G_IO_ERROR, G_IO_ERROR_FAILED, "PipeWire uzak soketi gelmedi"); return -1; }
  *uzak_fd = g_unix_fd_list_get(fd_listesi, idx, hata);
  g_object_unref(fd_listesi);
  if (*dugum == 0 || *uzak_fd < 0) {
    if (!*hata) g_set_error(hata, G_IO_ERROR, G_IO_ERROR_FAILED, "ekran akışı düğümü yok");
    return -1;
  }
  return 0;
}
