#ifndef MAAGE_CLIENT_PACING_H
#define MAAGE_CLIENT_PACING_H
#include "maage/window.h"
typedef struct maage_client_pacing {
    int fps;
    double deadline;
} maage_client_pacing;
/* Owner-thread frame boundary; polls native events during whole-ms waits and
 * carries deadlines through fractional wake delays. Zero FPS resets pacing. */
maage_status maage_client_pacing_wait(maage_client_pacing *pacing,
    maage_window_system *system, maage_window *window, int fps,
    double frame_start, int *resized);
#endif
