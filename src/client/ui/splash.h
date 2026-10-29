#ifndef MAAGE_CLIENT_UI_SPLASH_H
#define MAAGE_CLIENT_UI_SPLASH_H
#include "maage/render.h"
/* Startup presentation, independent of the not-yet-loaded game GUI. */
typedef struct maage_client_splash {
    maage_window_system *system;
    maage_window *window;
    maage_renderer *renderer;
    maage_render_texture *texture;
    maage_status resource_status;
    double last_frame;
    int gui_scale, close_requested;
} maage_client_splash;
maage_status maage_client_splash_begin(maage_client_splash *splash,
    maage_window_system *system, maage_window *window, maage_renderer *renderer,
    const char *directory, int gui_scale, maage_status_trace *trace);
/* Initialization checkpoint; also the resource loader's callback. */
maage_status maage_client_splash_update(void *context, maage_status_trace *trace);
maage_status maage_client_splash_destroy(maage_client_splash *splash);
#endif
