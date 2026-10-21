#ifndef MAAGE_WINDOW_H
#define MAAGE_WINDOW_H
#include "maage/pixels.h"
#include "maage/status.h"

/* All calls, including destruction, belong to the initialization thread (the
 * main thread on macOS). One system may own multiple windows. No thread safety.
 * Destroy windows before their system; system destruction also cleans survivors.
 * Strings are UTF-8 and copied by native creation/title operations. */
typedef struct maage_window_system maage_window_system;
typedef struct maage_window maage_window;


typedef enum maage_window_backend {
    MAAGE_WINDOW_AUTO = 0,
    MAAGE_WINDOW_WIN32,
    MAAGE_WINDOW_COCOA,
    MAAGE_WINDOW_X11,
    MAAGE_WINDOW_WAYLAND
} maage_window_backend;

typedef enum maage_window_mode {
    MAAGE_WINDOW_WINDOWED = 0,
    MAAGE_WINDOW_BORDERLESS_WINDOWED,
    MAAGE_WINDOW_BORDERLESS_FULLSCREEN,
    MAAGE_WINDOW_NATIVE_FULLSCREEN
} maage_window_mode;

typedef struct maage_window_config {
    const char *title;
    int width;
    int height;
    int visible;
    int full_display_content;
    maage_window_mode mode;
} maage_window_config;

typedef struct maage_window_insets {
    double top;
    double right;
    double bottom;
    double left;
} maage_window_insets;

/* Dimensions and safe-area insets use logical units, except framebuffer sizes
 * which use backing pixels. Zero insets mean no known display obstruction.
 * Renderers may cover the full backing area; UI can respect the safe rectangle.
 * Wayland's initial empty backing uses scale 1 (compositor scaling still applies). */
typedef struct maage_window_state {
    int width;
    int height;
    int framebuffer_width;
    int framebuffer_height;
    int visible;
    int close_requested;
    int resized;
    int mode_transitioning; /* Native fullscreen is still entering or exiting. */
    maage_window_mode mode;
    maage_window_insets safe_area;
} maage_window_state;

/* Borrowed handles; never destroy them. Valid only while the window/system
 * lives. Check backend before interpreting its matching union member. Display
 * connections must remain under this library's event ownership. Cocoa pointers
 * identify NSWindow/NSView; Wayland pointers identify wl_display/wl_surface. */
typedef struct maage_window_native {
    maage_window_backend backend;
    union {
        struct { void *instance; void *window; } win32;
        struct { void *window; void *view; } cocoa;
        struct { void *display; unsigned long window; } x11;
        struct { void *display; void *surface; } wayland;
    } handles;
} maage_window_native;

maage_status maage_window_config_init(maage_window_config *config);
/* AUTO: native backend; on Linux try Wayland when advertised, then X11.
 * Explicit selection never falls back. Failure sets *system to null; PARTIAL
 * owns a usable system after recovering with another native backend. */
maage_status maage_window_system_create(maage_window_backend backend,
    maage_window_system **system, maage_status_trace *trace);
maage_status maage_window_system_destroy(maage_window_system *system);
maage_status maage_window_create(maage_window_system *system,
    const maage_window_config *config, maage_window **window, maage_status_trace *trace);
maage_status maage_window_destroy(maage_window *window);
/* wait != 0 may block until OS events arrive; otherwise dispatch available
 * events only. Close requests remain set; resized is consumed by get_state.
 * A failed poll means the application should destroy its windows and exit. */
maage_status maage_window_poll_events(maage_window_system *system, int wait);
/* Process events, waiting at most timeout_ms milliseconds (zero never blocks). */
maage_status maage_window_wait_events(maage_window_system *system, int timeout_ms);
maage_status maage_window_get_state(maage_window *window,
    maage_window_state *state);
maage_status maage_window_set_title(maage_window *window, const char *title);
maage_status maage_window_set_visible(maage_window *window, int visible);
/* Sizes are logical content dimensions, not outer frames. Fullscreen sizes are
 * OS-controlled; resize returns UNSUPPORTED there. Mode changes may be async:
 * OK acknowledges a request, not compositor acceptance. Query state after polls.
 * Native fullscreen currently means AppKit fullscreen Spaces and requires a
 * visible window; other backends use BORDERLESS_FULLSCREEN and reject NATIVE.
 * Wayland client-requested resize and forced borderless mode without decoration
 * negotiation return UNSUPPORTED. Normal Wayland decoration is compositor-owned.
 * Unsupported native fullscreen/borderless semantics return UNSUPPORTED. */
maage_status maage_window_resize(maage_window *window, int width, int height);
maage_status maage_window_set_mode(maage_window *window, maage_window_mode mode);
maage_status maage_window_get_native(maage_window *window,
    maage_window_native *native);
/* Exclusive native-content ownership for a renderer or other surface producer.
 * Acquire once before attaching graphics resources; release after destroying
 * them, before window destruction. Borrowed native handles keep their lifetime.
 * Wayland suspends its empty wl_shm backing while an external owner is active. */
maage_status maage_window_acquire_surface(maage_window *window,
    maage_window_native *native);
maage_status maage_window_release_surface(maage_window *window);
/* Native CPU-image presentation while the caller owns the surface. Copies or
 * consumes the image before returning; never retains borrowed pixel storage.
 * UNAVAILABLE means the native surface/buffer is not ready; process events and
 * retry. Size must match the current framebuffer. Native surfaces may ignore
 * alpha. No GPU SDK is required. */
maage_status maage_window_present_pixels(maage_window *window,
    const maage_pixel_buffer *buffer);

#endif
