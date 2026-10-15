#ifndef MAAGE_WINDOW_INTERNAL_H
#define MAAGE_WINDOW_INTERNAL_H
#include "maage/window.h"

typedef struct maage_window_backend_api {
    maage_window_backend kind;
    maage_window_result (*init)(maage_window_system *system);
    void (*shutdown)(maage_window_system *system);
    maage_window_result (*create)(maage_window *window, const maage_window_config *config);
    void (*destroy)(maage_window *window);
    maage_window_result (*poll)(maage_window_system *system, int timeout_ms);
    maage_window_result (*state)(maage_window *window);
    maage_window_result (*title)(maage_window *window, const char *title);
    maage_window_result (*visible)(maage_window *window, int visible);
    maage_window_result (*resize)(maage_window *window, int width, int height);
    maage_window_result (*mode)(maage_window *window, maage_window_mode mode);
    void (*native)(maage_window *window, maage_window_native *native);
    maage_window_result (*surface)(maage_window *window, int external);
    maage_window_result (*pixels)(maage_window *window, const maage_pixel_buffer *buffer);
} maage_window_backend_api;

struct maage_window_system {
    const maage_window_backend_api *api;
    void *platform;
    maage_window *windows;
};
struct maage_window {
    maage_window_system *system;
    maage_window *next;
    void *platform;
    maage_window_state state;
    int full_display_content;
    int surface_owned;
};

extern const maage_window_backend_api maage_window_win32_api;
extern const maage_window_backend_api maage_window_cocoa_api;
extern const maage_window_backend_api maage_window_x11_api;
extern const maage_window_backend_api maage_window_wayland_api;
#endif
