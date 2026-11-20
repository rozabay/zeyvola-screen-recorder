/* Wayland screen capture through xdg-desktop-portal ScreenCast (see kayit-wayland.c). */
#ifndef ZEYVOLA_EKRAN_H
#define ZEYVOLA_EKRAN_H

#include <gio/gio.h>

/*
 * Asks the portal for the screen and returns the PipeWire node id plus a remote fd that reaches it.
 * The user approves once; `geri_yukleme_jetonu` (optional) receives the portal's restore token, so a later
 * recording can skip the dialog where the desktop supports it.
 * Returns 0 on success, -1 with `hata` set otherwise.
 */
int zeyvola_ekran_wayland(unsigned int *dugum, int *uzak_fd, char **geri_yukleme_jetonu, GError **hata);

#endif
