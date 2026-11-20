/*
 * Zeyvola recording helper (Linux).
 *
 * Drives GStreamer the way the macOS helper drives ScreenCaptureKit: the screen without the
 * pointer, the camera, the microphone and the system sound each go into their own file, all on one
 * clock, and a session.jsonl records how to put them back together after a crash.
 *
 * Protocol (apps/desktop/src/main/capture/helper-client.ts):
 *   app → helper   prepare | go | pause | resume | mark | stop | cancel  (one JSON object per line)
 *   helper → app   hello, ready, started, status, interrupted, stopped, cancelled, error
 *
 *   zeyvola-kayit [--probe-files <dir>] [--self-test]
 *
 * Files in the recording folder (the same names and meanings as on macOS):
 *   screen.webm   the display, pointer NOT drawn in (X11: ximagesrc show-pointer=false)
 *   camera.webm   the camera (v4l2src → VP9)
 *   mic.webm      the microphone (Opus)
 *   system.webm   the computer's own sound (Opus; default sink's monitor)
 *   session.jsonl one JSON line per event, fsynced as it is written (recovery.ts reads it)
 *
 * WebM + VP9 + Opus everywhere: no H.264 encoder is required or shipped. A computer whose VA-API
 * offers vah264enc can also write screen.mp4 — the app decides that from the probe report
 * (export/route.ts), not from this helper.
 */
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include <gio/gio.h>
#include <gst/gst.h>
#include <gst/pbutils/pbutils.h>

#include "ekran.h"
#include "../../ortak/protokol.h"

#define EN_FAZLA_KAYNAK 4

/* One recording source: its own GStreamer pipeline, its own file. */
typedef struct {
  const char *ad;      /* screen, camera, mic, system */
  const char *dosya;   /* file name inside the recording folder */
  const char *mime;
  GstElement *hat;
  guint64 ilk_pts;     /* first buffer's running time, ns; UINT64_MAX until seen */
  guint64 son_pts;
  int ornek_sayisi;
  int genislik, yukseklik, fps, ornek_hizi, kanal;
  bool acildi;
} kaynak;

/* The Wayland screen's PipeWire node and remote fd, kept open for the recording's lifetime. */
typedef struct {
  unsigned int dugum;
  int uzak_fd;
  char *jeton;
  bool var;
} wayland_ekran;

typedef struct {
  kaynak kaynaklar[EN_FAZLA_KAYNAK];
  int sayi;
  char klasor[4096];
  char meta[8192];     /* the app's recording description, written unchanged into session.jsonl */
  int gunluk_fd;
  double baslangic_at;
  bool kayit_suruyor;
  bool duraklatildi;
  GMainLoop *dongu;
  guint64 duraklama_ns;  /* total paused time, subtracted from every PTS */
  guint64 duraklama_basi;
  guint64 isaretler[256];
  int isaret_sayisi;
  int durum_zamanlayici;
} durum;

static durum d;

/* ─── session.jsonl ─────────────────────────────────────────────────────────── */

/* One line, written with a single write() and fsynced: a crash never loses an earlier line. */
static void gunluk_yaz(const char *satir, int uzunluk) {
  if (d.gunluk_fd < 0) return;
  ssize_t n = write(d.gunluk_fd, satir, (size_t)uzunluk);
  (void)n;
  fsync(d.gunluk_fd);
}

static void gunluk_printf(const char *bicim, ...) {
  char tampon[16384];
  va_list ap;
  va_start(ap, bicim);
  int n = vsnprintf(tampon, sizeof tampon, bicim, ap);
  va_end(ap);
  if (n > 0) gunluk_yaz(tampon, n < (int)sizeof tampon ? n : (int)sizeof tampon - 1);
}

/* ─── GStreamer ─────────────────────────────────────────────────────────────── */

/* Milliseconds on the recording clock for a pipeline running time. Pauses are removed. */
static guint64 kayit_ms(guint64 calisma_ns) {
  if (calisma_ns < d.duraklama_ns) return 0;
  return (calisma_ns - d.duraklama_ns) / 1000000;
}

/* Reads the stream's shape from the pad's caps (once, when the first buffer arrives). */
static void caps_oku(kaynak *k, GstPad *ped) {
  GstCaps *caps = gst_pad_get_current_caps(ped);
  if (!caps) return;
  GstStructure *s = gst_caps_get_structure(caps, 0);
  if (gst_structure_has_field(s, "width")) {
    gst_structure_get_int(s, "width", &k->genislik);
    gst_structure_get_int(s, "height", &k->yukseklik);
    gst_structure_get_int(s, "framerate", &k->fps); /* a fraction: the numerator lands here */
  }
  if (gst_structure_has_field(s, "rate")) {
    gst_structure_get_int(s, "rate", &k->ornek_hizi);
    gst_structure_get_int(s, "channels", &k->kanal);
  }
  gst_caps_unref(caps);
}

/* Pad probe: one buffer per written sample (the encoded stream's own timing). */
static GstPadProbeReturn tampon_geldi(GstPad *ped, GstPadProbeInfo *bilgi, gpointer veri) {
  kaynak *k = veri;
  GstBuffer *tampon = GST_PAD_PROBE_INFO_BUFFER(bilgi);
  if (!tampon) return GST_PAD_PROBE_OK;
  GstClockTime pts = GST_BUFFER_PTS(tampon);
  if (!GST_CLOCK_TIME_IS_VALID(pts)) pts = GST_BUFFER_DTS(tampon);
  if (!GST_CLOCK_TIME_IS_VALID(pts)) return GST_PAD_PROBE_OK;
  if (k->ilk_pts == UINT64_MAX) {
    k->ilk_pts = pts;
    caps_oku(k, ped);
    /* The track line goes to the log as soon as its first buffer exists, before the file has much
     * in it, so recovery knows the file's offset even if the helper dies right after. */
    GString *satir = g_string_new(NULL);
    g_string_append_printf(satir,
        "{\"e\":\"track\",\"kind\":\"%s\",\"name\":\"%s\",\"mime\":\"%s\",\"startOffsetMs\":%.3f",
        k->ad, k->dosya, k->mime, (double)kayit_ms(pts));
    if (strcmp(k->mime, "video/webm") == 0 && k->genislik > 0) {
      g_string_append_printf(satir, ",\"width\":%d,\"height\":%d,\"fps\":%d", k->genislik, k->yukseklik, k->fps > 0 ? k->fps : 30);
    }
    if (k->ornek_hizi > 0) g_string_append_printf(satir, ",\"sampleRate\":%d,\"channels\":%d", k->ornek_hizi, k->kanal);
    g_string_append(satir, "}\n");
    gunluk_yaz(satir->str, (int)satir->len);
    g_string_free(satir, TRUE);
    k->acildi = true;
  }
  k->son_pts = pts;
  k->ornek_sayisi++;
  return GST_PAD_PROBE_OK;
}

static gboolean otobus_mesaji(GstBus *bus, GstMessage *msg, gpointer veri) {
  (void)bus;
  kaynak *k = veri;
  switch (GST_MESSAGE_TYPE(msg)) {
    case GST_MESSAGE_ERROR: {
      GError *hata = NULL;
      gchar *ayrinti = NULL;
      gst_message_parse_error(msg, &hata, &ayrinti);
      /* A source that cannot start must not end the whole recording: the app keeps the others and
       * says what happened. Fatal only for the screen, which has no recording without it. */
      printf("{\"type\":\"error\",\"code\":\"source\",\"message\":");
      zeyvola_json_yaz(stdout, k->ad);
      printf(",\"detail\":");
      zeyvola_json_yaz(stdout, hata ? hata->message : "hata");
      printf(",\"fatal\":%s}\n", strcmp(k->ad, "screen") == 0 ? "true" : "false");
      fflush(stdout);
      fprintf(stderr, "zeyvola-kayit: %s: %s (%s)\n", k->ad, hata ? hata->message : "hata", ayrinti ? ayrinti : "");
      if (hata) g_error_free(hata);
      g_free(ayrinti);
      break;
    }
    case GST_MESSAGE_EOS:
      break;
    default:
      break;
  }
  return G_SOURCE_CONTINUE;
}

/* Connects a source's pad to the recorder that measures it, then starts the pipeline. */
static bool kaynak_baslat(kaynak *k, const char *aciklama) {
  GError *hata = NULL;
  k->hat = gst_parse_launch(aciklama, &hata);
  if (!k->hat || hata) {
    printf("{\"type\":\"error\",\"code\":\"pipeline\",\"detail\":");
    zeyvola_json_yaz(stdout, hata ? hata->message : aciklama);
    printf(",\"fatal\":%s}\n", strcmp(k->ad, "screen") == 0 ? "true" : "false");
    fflush(stdout);
    fprintf(stderr, "zeyvola-kayit: %s boru hattı: %s\n", k->ad, hata ? hata->message : aciklama);
    if (hata) g_error_free(hata);
    if (k->hat) { gst_object_unref(k->hat); k->hat = NULL; }
    return false;
  }
  /* The tail element is the filesink; the pad before it carries the encoded buffers. */
  GstElement *dosya = gst_bin_get_by_name(GST_BIN(k->hat), "cikti");
  if (dosya) { g_object_set(dosya, "location", k->dosya, NULL); gst_object_unref(dosya); }
  /* Measuring happens on the encoder's source pad: one buffer per written sample. */
  GstElement *kodlayici = gst_bin_get_by_name(GST_BIN(k->hat), "kodlayici");
  if (kodlayici) {
    GstPad *ped = gst_element_get_static_pad(kodlayici, "src");
    if (ped) { gst_pad_add_probe(ped, GST_PAD_PROBE_TYPE_BUFFER, (GstPadProbeCallback)tampon_geldi, k, NULL); gst_object_unref(ped); }
    gst_object_unref(kodlayici);
  }
  GstBus *otobus = gst_element_get_bus(k->hat);
  gst_bus_add_watch(otobus, otobus_mesaji, k);
  gst_object_unref(otobus);
  k->ilk_pts = UINT64_MAX;
  if (gst_element_set_state(k->hat, GST_STATE_PAUSED) == GST_STATE_CHANGE_FAILURE) {
    printf("{\"type\":\"error\",\"code\":\"pipeline\",\"detail\":\"%s\",\"fatal\":%s}\n", k->ad,
           strcmp(k->ad, "screen") == 0 ? "true" : "false");
    fflush(stdout);
    return false;
  }
  return true;
}

static void kaynak_durdur(kaynak *k) {
  if (!k->hat) return;
  gst_element_send_event(k->hat, gst_event_new_eos());
  GstState durum = GST_STATE_VOID_PENDING;
  gst_element_get_state(k->hat, &durum, NULL, 3 * GST_SECOND);
  gst_element_set_state(k->hat, GST_STATE_NULL);
  gst_object_unref(k->hat);
  k->hat = NULL;
}

static void kaynak_dondur(kaynak *k) {
  if (k->hat) gst_element_set_state(k->hat, GST_STATE_PLAYING);
}

/* ─── Pipelines ─────────────────────────────────────────────────────────────── */

/* Several ready-made pipelines; the helper picks by what the session type and devices allow. */

static const char *EKRAN_X11 =
    "ximagesrc name=kaynak use-damage=false show-pointer=false ! "
    "video/x-raw,framerate=30/1 ! queue max-size-buffers=4 leaky=downstream ! "
    "videoconvert ! vp9enc name=kodlayici deadline=1 cpu-used=8 target-bitrate=12000000 ! "
    "webmmux streamable=true writing-app=Zeyvola ! filesink name=cikti";

static const char *KAMERA =
    "v4l2src name=kaynak device=%s ! "
    "video/x-raw,framerate=30/1 ! videoconvert ! videoscale ! "
    "vp9enc name=kodlayici deadline=1 cpu-used=8 target-bitrate=6000000 ! "
    "webmmux streamable=true writing-app=Zeyvola ! filesink name=cikti";

static const char *MIKROFON =
    "pulsesrc name=kaynak ! audioconvert ! audioresample ! "
    "audio/x-raw,rate=48000,channels=1 ! opusenc name=kodlayici bitrate=128000 ! "
    "webmmux streamable=true writing-app=Zeyvola ! filesink name=cikti";

static const char *SISTEM =
    "pulsesrc name=kaynak device=@DEFAULT_MONITOR@ ! audioconvert ! audioresample ! "
    "audio/x-raw,rate=48000,channels=2 ! opusenc name=kodlayici bitrate=160000 ! "
    "webmmux streamable=true writing-app=Zeyvola ! filesink name=cikti";

/* Wayland: the portal's PipeWire node, reached through the fd from OpenPipeWireRemote. */
static const char *EKRAN_WAYLAND =
    "pipewiresrc name=kaynak fd=%d path=%u ! "
    "video/x-raw,framerate=30/1 ! queue max-size-buffers=4 leaky=downstream ! "
    "videoconvert ! vp9enc name=kodlayici deadline=1 cpu-used=8 target-bitrate=12000000 ! "
    "webmmux streamable=true writing-app=Zeyvola ! filesink name=cikti";

/* ─── prepare: opens every source during the countdown ──────────────────────── */

static bool kaynak_ekle(const char *ad, const char *dosya, const char *mime, const char *aciklama) {
  if (d.sayi >= EN_FAZLA_KAYNAK) return false;
  kaynak *k = &d.kaynaklar[d.sayi];
  memset(k, 0, sizeof *k);
  k->ad = ad;
  k->dosya = dosya;
  k->mime = mime;
  if (!kaynak_baslat(k, aciklama)) return false;
  d.sayi++;
  return true;
}

/* Wayland or X11: the session type decides where the screen comes from. */
static bool wayland_oturumu(void) {
  const char *tur = getenv("XDG_SESSION_TYPE");
  if (tur && strcasecmp(tur, "wayland") == 0) return true;
  if (tur && strcasecmp(tur, "x11") == 0) return false;
  return getenv("WAYLAND_DISPLAY") != NULL;
}

static wayland_ekran wy;

static void ekran_baslat(void) {
  if (!wayland_oturumu()) {
    kaynak_ekle("screen", "screen.webm", "video/webm", EKRAN_X11);
    return;
  }
  /* Wayland: ask the portal for the screen; the user approves once per session. */
  GError *hata = NULL;
  if (zeyvola_ekran_wayland(&wy.dugum, &wy.uzak_fd, &wy.jeton, &hata) != 0) {
    printf("{\"type\":\"error\",\"code\":\"portal\",\"detail\":");
    zeyvola_json_yaz(stdout, hata ? hata->message : "ekran paylaşımı portalı açılamadı");
    printf(",\"fatal\":true}\n");
    if (hata) g_error_free(hata);
    return;
  }
  wy.var = true;
  char boru[1024];
  snprintf(boru, sizeof boru, EKRAN_WAYLAND, wy.uzak_fd, wy.dugum);
  kaynak_ekle("screen", "screen.webm", "video/webm", boru);
}

static void hazirla(void) {
  const char *kamera = getenv("ZEYVOLA_KAMERA");
  char kamera_boru[1024];
  if (kamera && *kamera) {
    snprintf(kamera_boru, sizeof kamera_boru, KAMERA, kamera);
    kaynak_ekle("camera", "camera.webm", "video/webm", kamera_boru);
  }
  kaynak_ekle("mic", "mic.webm", "audio/webm", MIKROFON);
  kaynak_ekle("system", "system.webm", "audio/webm", SISTEM);
}

/* ─── commands ──────────────────────────────────────────────────────────────── */

static void durum_yaz(void) {
  if (!d.kayit_suruyor) return;
  const guint64 ms = (guint64)((zeyvola_simdi() - d.baslangic_at) * 1000) - d.duraklama_ns / 1000000;
  printf("{\"type\":\"status\",\"elapsedMs\":%llu,\"micLevel\":0,\"dropped\":0}\n", (unsigned long long)ms);
  fflush(stdout);
}

static gboolean durum_zaman(gpointer veri) {
  (void)veri;
  durum_yaz();
  return G_SOURCE_CONTINUE;
}

static void basla(void) {
  if (d.kayit_suruyor) return;
  d.baslangic_at = zeyvola_simdi();
  d.kayit_suruyor = true;
  d.duraklama_ns = 0;
  /* The session line and "started" go out before the pipelines are switched to PLAYING: the app's
   * countdown ends on "started", and a slow first frame must not delay the recording clock. */
  gunluk_printf("{\"e\":\"session\",\"v\":1,\"startedAtMs\":%lld,\"pid\":%d,\"helper\":\"zeyvola-kayit\",\"version\":\"%s\",\"platform\":\"linux-%s\",\"fps\":30,\"cursor\":\"cursor.jsonl\"%s%s}\n",
                (long long)(d.baslangic_at * 1000.0), (int)getpid(), "0.1.0",
                sizeof(void *) == 8 ? "x64" : "arm",
                d.meta[0] ? ",\"meta\":" : "", d.meta);
  printf("{\"type\":\"started\",\"startedAtMs\":%lld}\n", (long long)(d.baslangic_at * 1000.0));
  fflush(stdout);
  for (int i = 0; i < d.sayi; i++) kaynak_dondur(&d.kaynaklar[i]);
  d.durum_zamanlayici = g_timeout_add(500, durum_zaman, NULL);
}

static void bitir(void) {
  if (!d.kayit_suruyor) return;
  d.kayit_suruyor = false;
  if (d.durum_zamanlayici) { g_source_remove(d.durum_zamanlayici); d.durum_zamanlayici = 0; }
  const guint64 ms = (guint64)((zeyvola_simdi() - d.baslangic_at) * 1000) - d.duraklama_ns / 1000000;

  /* The stop report names each file with the length that reached it, so the app can build the
   * recording without opening them (the browser path needs a probe; this one does not). */
  GString *dosyalar = g_string_new("[");
  for (int i = 0; i < d.sayi; i++) {
    kaynak *k = &d.kaynaklar[i];
    if (k->ornek_sayisi == 0) continue;
    const double bas = k->ilk_pts == UINT64_MAX ? 0.0 : (double)kayit_ms(k->ilk_pts);
    const double son = (double)kayit_ms(k->son_pts);
    if (dosyalar->len > 1) g_string_append_c(dosyalar, ',');
    g_string_append_printf(
        dosyalar,
        "{\"kind\":\"%s\",\"name\":\"%s\",\"mime\":\"%s\",\"startOffsetMs\":%.3f,\"durationMs\":%.3f",
        k->ad, k->dosya, k->mime, bas, son - bas);
    if (k->genislik > 0 && strcmp(k->mime, "video/webm") == 0) g_string_append_printf(dosyalar, ",\"width\":%d,\"height\":%d,\"fps\":%d", k->genislik, k->yukseklik, k->fps > 0 ? k->fps : 30);
    if (k->ornek_hizi > 0) g_string_append_printf(dosyalar, ",\"sampleRate\":%d,\"channels\":%d", k->ornek_hizi, k->kanal);
    g_string_append_printf(dosyalar, ",\"samples\":%d}", k->ornek_sayisi);
  }
  g_string_append_c(dosyalar, ']');

  GString *isaretler = g_string_new("[");
  for (int i = 0; i < d.isaret_sayisi; i++) {
    if (i) g_string_append_c(isaretler, ',');
    g_string_append_printf(isaretler, "%llu", (unsigned long long)d.isaretler[i]);
  }
  g_string_append_c(isaretler, ']');

  for (int i = 0; i < d.sayi; i++) kaynak_durdur(&d.kaynaklar[i]);
  gunluk_printf("{\"e\":\"end\",\"durationMs\":%llu}\n", (unsigned long long)ms);
  printf("{\"type\":\"stopped\",\"durationMs\":%llu,\"files\":%s,\"cursor\":\"cursor.jsonl\",\"markers\":%s,\"hasClicks\":false}\n",
         (unsigned long long)ms, dosyalar->str, isaretler->str);
  fflush(stdout);
  g_string_free(dosyalar, TRUE);
  g_string_free(isaretler, TRUE);
}

/* ─── main ──────────────────────────────────────────────────────────────────── */

int main(int argc, char **argv) {
  zeyvola_baslat_stdout();
  signal(SIGPIPE, SIG_IGN);
  memset(&d, 0, sizeof d);
  d.gunluk_fd = -1;

  gst_init(&argc, &argv);

  for (int i = 1; i < argc; i++) {
    if (strcmp(argv[i], "--self-test") == 0) {
      /* Asked after gst_init: the plugin registry is only loaded then. */
      GstElementFactory *vp9 = gst_element_factory_find("vp9enc");
      GstElementFactory *opus = gst_element_factory_find("opusenc");
      GstElementFactory *mux = gst_element_factory_find("webmmux");
      GstElementFactory *x11 = gst_element_factory_find("ximagesrc");
      GstElementFactory *pw = gst_element_factory_find("pipewiresrc");
      printf("{\"type\":\"selftest\",\"ok\":true,\"vp9\":%s,\"opus\":%s,\"webmmux\":%s,\"ximagesrc\":%s,\"pipewiresrc\":%s}\n",
             vp9 ? "true" : "false", opus ? "true" : "false", mux ? "true" : "false",
             x11 ? "true" : "false", pw ? "true" : "false");
      if (vp9) gst_object_unref(vp9);
      if (opus) gst_object_unref(opus);
      if (mux) gst_object_unref(mux);
      if (x11) gst_object_unref(x11);
      if (pw) gst_object_unref(pw);
      return 0;
    }
    if (strcmp(argv[i], "--probe-files") == 0 && i + 1 < argc) {
      printf("{\"type\":\"probe\",\"files\":[]}\n"); /* the app measures WebM itself (webm-probe.ts) */
      return 0;
    }
  }

  d.dongu = g_main_loop_new(NULL, FALSE);
  printf("{\"type\":\"hello\",\"version\":\"0.1.0\",\"arch\":\"%s\"}\n", sizeof(void *) == 8 ? "x64" : "arm");
  fflush(stdout);

  char satir[65536];
  for (;;) {
    /* Commands arrive on stdin, one JSON object per line; a whole line may span reads. */
    fd_set okuma;
    FD_ZERO(&okuma);
    FD_SET(STDIN_FILENO, &okuma);
    struct timeval bekle = { 0, 50000 };
    const int hazir = select(STDIN_FILENO + 1, &okuma, NULL, NULL, &bekle);
    if (hazir > 0) {
      ssize_t n = read(STDIN_FILENO, satir, sizeof satir - 1);
      if (n <= 0) {
        /* The app quit: finish the recording and close the files, like macOS. */
        if (d.kayit_suruyor) bitir();
        break;
      }
      satir[n] = 0;
      char *p = satir;
      while (*p) {
        while (*p == ' ' || *p == '\n' || *p == '\r' || *p == '\t') p++;
        if (!*p) break;
        const bool prepare = strncmp(p, "{\"cmd\":\"prepare\"", sizeof "{\"cmd\":\"prepare\"" - 1) == 0;
        const bool go = strncmp(p, "{\"cmd\":\"go\"", sizeof "{\"cmd\":\"go\"" - 1) == 0;
        if (prepare) {
          /* The command carries outDir and meta; both are copied out verbatim. */
          const char *o = strstr(p, "\"outDir\":\"");
          if (o) {
            o += 10;
            const char *son = strchr(o, '"');
            if (son && (size_t)(son - o) < sizeof d.klasor) {
              memcpy(d.klasor, o, (size_t)(son - o));
              d.klasor[son - o] = 0;
              /* A separate buffer: `satir` still holds the command being parsed. */
              char gunluk_yolu[4200];
              snprintf(gunluk_yolu, sizeof gunluk_yolu, "%s/session.jsonl", d.klasor);
              d.gunluk_fd = open(gunluk_yolu, O_CREAT | O_WRONLY | O_TRUNC | O_CLOEXEC, 0644);
              chdir(d.klasor);
            }
          }
          const char *m = strstr(p, "\"meta\":");
          if (m) {
            /* Stored without the key: the log line adds `"meta":` itself. */
            m += sizeof "\"meta\":" - 1;
            /* The meta object runs to the closing brace of the whole command (meta is last). */
            const char *son = strrchr(p, '}');
            size_t uzunluk = son ? (size_t)(son - m) : 0;
            if (uzunluk > 0 && uzunluk + 1 < sizeof d.meta) {
              memcpy(d.meta, m, uzunluk);
              d.meta[uzunluk] = 0;
            }
          }
          ekran_baslat();
          hazirla();
          printf("{\"type\":\"ready\",\"screenWidth\":%d,\"screenHeight\":%d}\n", 0, 0);
          fflush(stdout);
        } else if (go) {
          basla();
        } else if (strncmp(p, "{\"cmd\":\"stop\"", sizeof "{\"cmd\":\"stop\"" - 1) == 0) {
          bitir();
          g_main_context_iteration(NULL, FALSE);
          if (d.gunluk_fd >= 0) { close(d.gunluk_fd); d.gunluk_fd = -1; }
          break;
        } else if (strncmp(p, "{\"cmd\":\"cancel\"", sizeof "{\"cmd\":\"cancel\"" - 1) == 0) {
          for (int i = 0; i < d.sayi; i++) kaynak_durdur(&d.kaynaklar[i]);
          gunluk_printf("{\"e\":\"cancelled\"}\n");
          if (d.gunluk_fd >= 0) { close(d.gunluk_fd); d.gunluk_fd = -1; }
          printf("{\"type\":\"cancelled\"}\n");
          fflush(stdout);
          break;
        } else if (strncmp(p, "{\"cmd\":\"pause\"", sizeof "{\"cmd\":\"pause\"" - 1) == 0) {
          d.duraklatildi = true;
          d.duraklama_basi = gst_util_get_timestamp();
          gunluk_printf("{\"e\":\"pause\",\"t\":0,\"atMs\":%lld}\n", (long long)(zeyvola_simdi() * 1000.0));
        } else if (strncmp(p, "{\"cmd\":\"resume\"", sizeof "{\"cmd\":\"resume\"" - 1) == 0) {
          if (d.duraklatildi) {
            d.duraklama_ns += gst_util_get_timestamp() - d.duraklama_basi;
            d.duraklatildi = false;
            gunluk_printf("{\"e\":\"resume\",\"t\":0,\"atMs\":%lld}\n", (long long)(zeyvola_simdi() * 1000.0));
          }
        } else if (strncmp(p, "{\"cmd\":\"mark\"", sizeof "{\"cmd\":\"mark\"" - 1) == 0) {
          if (d.isaret_sayisi < (int)(sizeof d.isaretler / sizeof d.isaretler[0])) {
            const guint64 ms = (guint64)((zeyvola_simdi() - d.baslangic_at) * 1000) - d.duraklama_ns / 1000000;
            d.isaretler[d.isaret_sayisi++] = ms;
            gunluk_printf("{\"e\":\"mark\",\"t\":%llu}\n", (unsigned long long)ms);
          }
        }
        while (*p && *p != '\n') p++;
      }
    }
    /* GStreamer's own messages (framerate warnings, element errors) are dispatched here. */
    g_main_context_iteration(NULL, FALSE);
  }

  if (d.gunluk_fd >= 0) close(d.gunluk_fd);
  g_main_loop_unref(d.dongu);
  return 0;
}
