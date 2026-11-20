/*
 * The line protocol shared by Zeyvola's Linux helpers (apps/desktop/src/main/capture/
 * helper-client.ts and input-helper.ts read it): one JSON object per line on stdout.
 */
#ifndef ZEYVOLA_PROTOKOL_H
#define ZEYVOLA_PROTOKOL_H

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

/* Line-buffered stdout: every line reaches the app as soon as it is written. */
static inline void zeyvola_baslat_stdout(void) { setvbuf(stdout, NULL, _IOLBF, 0); }

/* Seconds since boot, on the clock the whole session is timed with (CLOCK_MONOTONIC). */
static inline double zeyvola_simdi(void) {
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return (double)ts.tv_sec + (double)ts.tv_nsec / 1e9;
}

/* One finished line to stdout. */
static inline void zeyvola_satir(const char *satir) {
  fputs(satir, stdout);
  fputc('\n', stdout);
  fflush(stdout);
}

/* {"type":"error","code":"..."} plus an optional message on stderr. */
static inline void zeyvola_hata(const char *kod, const char *mesaj) {
  if (mesaj && *mesaj) fprintf(stderr, "zeyvola: %s: %s\n", kod, mesaj);
  printf("{\"type\":\"error\",\"code\":\"%s\"}\n", kod);
  fflush(stdout);
}

/* stderr line for the operator; the app only reads the JSON on stdout. */
static inline void zeyvola_hata_yaz(const char *nerede, const char *kod, const char *detay) {
  fprintf(stderr, "zeyvola-%s: %s%s%s\n", nerede, kod, detay ? ": " : "", detay ? detay : "");
}

/* Reads stdin for "stop"; true once the app asked the helper to close (or its pipe ended). */
static inline bool zeyvola_durdur_okundu(int fd) {
  char buf[256];
  ssize_t n = read(fd, buf, sizeof buf - 1);
  if (n <= 0) return true; /* the app closed stdin (or quit) */
  buf[n] = 0;
  char *p = buf;
  while (*p) {
    while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r') p++;
    if (strncmp(p, "stop", 4) == 0) return true;
    while (*p && *p != '\n') p++;
  }
  return false;
}

/* Encodes a string for a JSON value (quotes and backslashes only; the helpers send plain ASCII). */
static inline void zeyvola_json_yaz(FILE *f, const char *s) {
  fputc('"', f);
  for (const unsigned char *p = (const unsigned char *)s; *p; p++) {
    if (*p == '"' || *p == '\\') fputc('\\', f);
    fputc(*p, f);
  }
  fputc('"', f);
}

#endif
