#ifndef MAAGE_CLIENT_UI_SCALE_H
#define MAAGE_CLIENT_UI_SCALE_H
#include "ui.h"

/* Layout rounds up to whole GUI units; projection retains the exact backing
 * extent so one GUI unit always covers an integer square of physical pixels.
 * Native pointer coordinates use the inverse of that same projection. */
typedef struct maage_client_ui_viewport {
    int scale, maximum_scale, width, height;
    float projection_width, projection_height;
    float mouse_scale_x, mouse_scale_y;
    maage_client_ui_rect content;
} maage_client_ui_viewport;
maage_status maage_client_ui_viewport_resolve(int framebuffer_width, int framebuffer_height, int window_width, int window_height, const maage_window_insets *insets, int requested_scale, maage_client_ui_viewport *output);
#endif
