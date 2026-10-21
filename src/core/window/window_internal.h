#ifndef MAAGE_WINDOW_INTERNAL_H
#define MAAGE_WINDOW_INTERNAL_H
#include "maage/window.h"
#include "maage/input.h"

typedef struct maage_window_backend_api {
    maage_window_backend kind;
    maage_status (*init)(maage_window_system *system);
    void (*shutdown)(maage_window_system *system);
    maage_status (*create)(maage_window *window, const maage_window_config *config);
    void (*destroy)(maage_window *window);
    maage_status (*poll)(maage_window_system *system, int timeout_ms);
    maage_status (*state)(maage_window *window);
    maage_status (*title)(maage_window *window, const char *title);
    maage_status (*visible)(maage_window *window, int visible);
    maage_status (*resize)(maage_window *window, int width, int height);
    maage_status (*mode)(maage_window *window, maage_window_mode mode);
    void (*native)(maage_window *window, maage_window_native *native);
    maage_status (*surface)(maage_window *window, int external);
    maage_status (*pixels)(maage_window *window, const maage_pixel_buffer *buffer);
    maage_status (*capture)(maage_window *window, int capture);
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
    maage_input_snapshot input;
    int full_display_content;
    int surface_owned;
};

extern const maage_window_backend_api maage_window_win32_api;
extern const maage_window_backend_api maage_window_cocoa_api;
extern const maage_window_backend_api maage_window_x11_api;
extern const maage_window_backend_api maage_window_wayland_api;
void maage_window_input_key(maage_window *window, maage_input_key key, int down);
void maage_window_input_focus(maage_window *window, int focused);
#endif
