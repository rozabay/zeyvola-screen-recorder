/* Enters the pointer loop for one session type; both live in imlec-x11.c / imlec-wayland.c. */
#ifndef ZEYVOLA_IMLEC_H
#define ZEYVOLA_IMLEC_H

/* X11: XInput2 raw button and motion events. Returns the process exit code. */
int zeyvola_imlec_x11(unsigned long pencere);

/* Wayland: buttons and relative motion through xdg-desktop-portal RemoteDesktop. */
int zeyvola_imlec_wayland(unsigned long pencere);

#endif
