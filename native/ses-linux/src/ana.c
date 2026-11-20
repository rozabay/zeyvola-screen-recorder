/*
 * Zeyvola sound helper (Linux).
 *
 * Two jobs, both through GStreamer:
 *
 *   zeyvola-ses --system-audio <out.webm>
 *       Records the computer's own sound (the default sink's monitor) as Opus in WebM, so it
 *       plays in the app like the other tracks. Runs until "stop" arrives on stdin or stdin
 *       closes, then writes the file's trailer and exits.
 *
 *   zeyvola-ses --extract-audio <video> <out.webm>
 *       Turns the sound of an imported video (usually AAC in MP4/MOV, which the app's codec-free
 *       libffmpeg cannot decode) into Opus in WebM, then exits. Prints one line:
 *       {"type":"audio","audio":true,"converted":true,"from":"aac"}
 *       or {"type":"audio","audio":false} when the video has no sound, or an "error" line when
 *       the sound exists but cannot be read (the app then warns instead of losing it silently).
 *
 * The whole point of a separate helper is the same as on macOS: the app ships no AAC decoder.
 */
#define _GNU_SOURCE
#include <errno.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <gio/gio.h>
#include <gst/gst.h>

#include "../../ortak/protokol.h"

static GstElement *boru_hatti = NULL;
static GstBus *otobus = NULL;
static GMainLoop *dongu = NULL;
/* Set by the bus watch: why the pipeline stopped (an error message), NULL for a clean end. */
static char *son_hata = NULL;

/* Ends the loop from the bus watch (an error or EOS). */
static gboolean mesaj_geldi(GstBus *bus, GstMessage *msg, gpointer veri) {
  (void)bus; (void)veri;
  switch (GST_MESSAGE_TYPE(msg)) {
    case GST_MESSAGE_ERROR: {
      GError *hata = NULL;
      gchar *ayrinti = NULL;
      gst_message_parse_error(msg, &hata, &ayrinti);
      fprintf(stderr, "zeyvola-ses: %s (%s)\n", hata ? hata->message : "hata", ayrinti ? ayrinti : "");
      g_free(son_hata);
      son_hata = g_strdup(hata ? hata->message : "hata");
      if (hata) g_error_free(hata);
      g_free(ayrinti);
      g_main_loop_quit(dongu);
      break;
    }
    case GST_MESSAGE_EOS:
      g_main_loop_quit(dongu);
      break;
    default:
      break;
  }
  return G_SOURCE_CONTINUE;
}

/* Builds one pipeline and runs it on a loop; returns 0 when it ended cleanly. */
static int calistir(const char *aciklama, const char *dosya, GstElement **hatti, GstElement **otobus_elemani) {
  GError *hata = NULL;
  GstElement *hat = gst_parse_launch(aciklama, &hata);
  if (!hat || hata) {
    fprintf(stderr, "zeyvola-ses: boru hattı kurulamadı: %s\n", hata ? hata->message : "bilinmeyen");
    if (hata) g_error_free(hata);
    if (hat) gst_object_unref(hat);
    return 1;
  }
  GstElement *kaynak = gst_bin_get_by_name(GST_BIN(hat), "kaynak");
  if (!kaynak) {
    fprintf(stderr, "zeyvola-ses: boru hattında 'kaynak' öğesi yok\n");
    gst_object_unref(hat);
    return 1;
  }
  g_object_set(kaynak, "location", dosya, NULL);
  gst_object_unref(kaynak);

  *hatti = hat;
  otobus = gst_element_get_bus(hat);
  gst_bus_add_watch(otobus, mesaj_geldi, NULL);
  dongu = g_main_loop_new(NULL, FALSE);
  if (gst_element_set_state(hat, GST_STATE_PLAYING) == GST_STATE_CHANGE_FAILURE) {
    fprintf(stderr, "zeyvola-ses: boru hattı başlatılamadı\n");
    g_main_loop_unref(dongu);
    gst_object_unref(otobus);
    gst_object_unref(hat);
    return 1;
  }
  (void)otobus_elemani;
  return 0;
}

static void boru_hattini_kapat(void) {
  if (boru_hatti) {
    gst_element_send_event(boru_hatti, gst_event_new_eos());
    /* Let the muxer write its trailer before the process goes. */
    GstState durum = GST_STATE_VOID_PENDING;
    gst_element_get_state(boru_hatti, &durum, NULL, 3 * GST_SECOND);
    gst_element_set_state(boru_hatti, GST_STATE_NULL);
    gst_object_unref(boru_hatti);
    boru_hatti = NULL;
  }
  if (otobus) { gst_object_unref(otobus); otobus = NULL; }
  if (dongu) { g_main_loop_unref(dongu); dongu = NULL; }
}

/* Tries the PipeWire monitor first, then PulseAudio's; both name the default sink's monitor. */
static int sistem_sesi_baslat(const char *dosya) {
  /* Opus in WebM: the same container and codec as the app's own sound tracks. */
  const char *pipewire =
      "pipewiresrc name=kaynak target-object=@DEFAULT_MONITOR@ ! "
      "audio/x-raw,rate=48000,channels=2 ! audioconvert ! audioresample ! "
      "opusenc bitrate=160000 ! webmmux name=mux ! filesink name=cikti";
  const char *pulse =
      "pulsesrc name=kaynak device=@DEFAULT_MONITOR@ ! "
      "audio/x-raw,rate=48000,channels=2 ! audioconvert ! audioresample ! "
      "opusenc bitrate=160000 ! webmmux name=mux ! filesink name=cikti";

  GstElement *hat = NULL;
  int sonuc = calistir(pipewire, dosya, &hat, NULL);
  if (sonuc != 0) {
    fprintf(stderr, "zeyvola-ses: PipeWire monitor açılamadı, PulseAudio deneniyor\n");
    sonuc = calistir(pulse, dosya, &hat, NULL);
  }
  if (sonuc != 0) return sonuc;

  /* Stop when the app closes stdin or writes "stop"; GStreamer runs on its own thread. */
  int fd = gst_bus_poll(otobus, GST_MESSAGE_ANY, 0) ? -1 : -1;
  (void)fd;
  struct pollfd pfd = { STDIN_FILENO, POLLIN, 0 };
  for (;;) {
    /* The main loop is iterated in small steps so stdin can be watched in the same thread. */
    g_main_context_iteration(NULL, FALSE);
    const int hazir = poll(&pfd, 1, 50);
    if (hazir > 0 && (pfd.revents & (POLLIN | POLLHUP))) {
      if (zeyvola_durdur_okundu(STDIN_FILENO)) break;
    }
  }
  boru_hattini_kapat();
  printf("{\"type\":\"stopped\"}\n");
  fflush(stdout);
  return 0;
}

/* Reads one imported video's sound into Opus/WebM. */
static int ses_cikar(const char *video, const char *cikti) {
  /* uridecodebin guesses the container and codec; only audio is asked for, and a video without a
   * sound track is reported as "no sound" instead of ending in a decoder error. */
  char *aciklama = g_strdup_printf(
      "uridecodebin name=kaynak uri=file://%s caps=audio/x-raw ! audioconvert ! audioresample ! "
      "audio/x-raw,rate=48000 ! opusenc bitrate=128000 ! webmmux ! filesink name=cikti",
      video);
  GError *hata = NULL;
  GstElement *hat = gst_parse_launch(aciklama, &hata);
  g_free(aciklama);
  if (!hat || hata) {
    fprintf(stderr, "zeyvola-ses: %s\n", hata ? hata->message : "boru hattı kurulamadı");
    if (hata) g_error_free(hata);
    if (hat) gst_object_unref(hat);
    return 1;
  }
  GstElement *cikis = gst_bin_get_by_name(GST_BIN(hat), "cikti");
  if (cikis) { g_object_set(cikis, "location", cikti, NULL); gst_object_unref(cikis); }

  boru_hatti = hat;
  otobus = gst_element_get_bus(hat);
  gst_bus_add_watch(otobus, mesaj_geldi, NULL);
  dongu = g_main_loop_new(NULL, FALSE);
  g_free(son_hata);
  son_hata = NULL;
  if (gst_element_set_state(hat, GST_STATE_PLAYING) == GST_STATE_CHANGE_FAILURE) {
    fprintf(stderr, "zeyvola-ses: boru hattı başlatılamadı\n");
    boru_hattini_kapat();
    printf("{\"type\":\"audio\",\"audio\":false,\"error\":\"pipeline\"}\n");
    fflush(stdout);
    return 1;
  }
  g_main_loop_run(dongu);
  boru_hattini_kapat();

  /* A failed pipeline means the sound could not be read (a codec the system lacks, a corrupt
   * file). "No sound track" comes from a decoder that never produced a pad: it says so the same
   * way, which the app shows as a warning rather than losing the sound silently. */
  if (son_hata) {
    const bool ses_yok = strstr(son_hata, "not-linked") != NULL || strstr(son_hata, "no element") != NULL;
    printf("{\"type\":\"audio\",\"audio\":%s,\"error\":\"%s\"}\n",
           ses_yok ? "false" : "true", ses_yok ? "no-audio" : "decode");
    g_free(son_hata);
    son_hata = NULL;
    fflush(stdout);
    return ses_yok ? 0 : 1;
  }
  printf("{\"type\":\"audio\",\"audio\":true,\"converted\":true,\"from\":\"video\"}\n");
  fflush(stdout);
  return 0;
}

int main(int argc, char **argv) {
  zeyvola_baslat_stdout();
  gst_init(&argc, &argv);
  signal(SIGPIPE, SIG_IGN);

  const char *sistem = NULL, *video = NULL, *cikti = NULL;
  for (int i = 1; i < argc; i++) {
    if (strcmp(argv[i], "--system-audio") == 0 && i + 1 < argc) sistem = argv[++i];
    else if (strcmp(argv[i], "--extract-audio") == 0 && i + 2 < argc) { video = argv[++i]; cikti = argv[++i]; }
    else if (strcmp(argv[i], "--self-test") == 0) {
      GstElementFactory *f = gst_element_factory_find("opusenc");
      printf("{\"type\":\"selftest\",\"ok\":%s,\"opusenc\":%s,\"pipewire\":%s}\n",
             f ? "true" : "false", f ? "true" : "false",
             gst_element_factory_find("pipewiresrc") ? "true" : "false");
      if (f) gst_object_unref(f);
      return 0;
    }
  }
  if (sistem) return sistem_sesi_baslat(sistem);
  if (video && cikti) return ses_cikar(video, cikti);
  fprintf(stderr, "kullanım: zeyvola-ses --system-audio <çıktı.webm> | --extract-audio <video> <çıktı.webm>\n");
  return 2;
}
