#ifndef MAAGE_RENDER_H
#define MAAGE_RENDER_H
#include "maage/window.h"

typedef struct maage_renderer maage_renderer;
typedef enum maage_render_backend {
    MAAGE_RENDER_AUTO = 0,
    MAAGE_RENDER_VULKAN,
    MAAGE_RENDER_METAL
} maage_render_backend;
typedef enum maage_render_result {
    MAAGE_RENDER_OK = 0,
    MAAGE_RENDER_SKIPPED,
    MAAGE_RENDER_INVALID_ARGUMENT,
    MAAGE_RENDER_OUT_OF_MEMORY,
    MAAGE_RENDER_UNSUPPORTED,
    MAAGE_RENDER_UNAVAILABLE,
    MAAGE_RENDER_WINDOW_ERROR,
    MAAGE_RENDER_DEVICE_LOST,
    MAAGE_RENDER_SURFACE_LOST,
    MAAGE_RENDER_PLATFORM_ERROR
} maage_render_result;
typedef struct maage_render_desc {
    maage_window *window;
    maage_render_backend backend;
} maage_render_desc;

/* Use the window's initialization thread. The caller owns the window and must
 * keep it alive until renderer destruction. One renderer owns a window's native
 * content at a time. No native graphics types escape this interface. */
void maage_render_desc_init(maage_render_desc *desc);
const char *maage_render_result_string(maage_render_result result);
maage_render_result maage_renderer_create(const maage_render_desc *desc,
    maage_renderer **renderer);
void maage_renderer_destroy(maage_renderer *renderer);
maage_render_backend maage_renderer_get_backend(const maage_renderer *renderer);
/* Components must be finite values in [0, 1]. */
maage_render_result maage_renderer_set_clear_color(maage_renderer *renderer,
    float red, float green, float blue, float alpha);
/* Presents a cleared frame; automatically observes framebuffer size changes.
 * SKIPPED is nonfatal (hidden/minimized/unconfigured, no drawable, or a pending
 * swapchain rebuild). Keep processing events and use a bounded event wait.
 * Fatal errors require destruction; create a new renderer/window to recover. */
maage_render_result maage_renderer_present(maage_renderer *renderer);
/* Force presentation resources to refresh on the next frame, if needed. */
void maage_renderer_resize(maage_renderer *renderer);
#endif
