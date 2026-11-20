/* Pointer line output, shared by the X11 and Wayland paths (the format input-helper.ts parses). */
#ifndef ZEYVOLA_OLAY_H
#define ZEYVOLA_OLAY_H

#include <stdio.h>

#include "../../ortak/protokol.h"

/* Pointer position an event happened at, in global top-left pixels. */
typedef struct {
  double gx, gy;
} nokta;

/* {"type":"ready","now":<s>} — the app starts recording once it sees this. */
static inline void zeyvola_hazir_yaz(void) {
  printf("{\"type\":\"ready\",\"now\":%.6f}\n", zeyvola_simdi());
  fflush(stdout);
}

/* One pointer line: e = 'd'/'u' with a button, or 's' with a scroll amount. */
static inline void zeyvola_olay_yaz(char e, const char *b, double dy, double at, nokta p) {
  if (e == 's') {
    printf("{\"e\":\"s\",\"dy\":%.3f,\"at\":%.6f,\"now\":%.6f,\"gx\":%.2f,\"gy\":%.2f}\n", dy, at, zeyvola_simdi(), p.gx, p.gy);
  } else {
    printf("{\"e\":\"%c\",\"b\":\"%s\",\"at\":%.6f,\"now\":%.6f,\"gx\":%.2f,\"gy\":%.2f}\n", e, b, at, zeyvola_simdi(), p.gx, p.gy);
  }
  fflush(stdout);
}

/* {"type":"window",…} — the recorded window's bounds (X11 only). */
static inline void zeyvola_pencere_yaz(double at, double x, double y, double w, double h) {
  printf("{\"type\":\"window\",\"at\":%.6f,\"now\":%.6f,\"gx\":%.2f,\"gy\":%.2f,\"w\":%.2f,\"h\":%.2f}\n",
         at, zeyvola_simdi(), x, y, w, h);
  fflush(stdout);
}

#endif
