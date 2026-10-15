#ifndef MAAGE_RENDER_H
#define MAAGE_RENDER_H
#include "maage/window.h"

typedef struct maage_renderer maage_renderer;
typedef enum maage_render_backend {
    MAAGE_RENDER_AUTO = 0,
    MAAGE_RENDER_VULKAN,
    MAAGE_RENDER_METAL,
    MAAGE_RENDER_OPENGL,
    MAAGE_RENDER_OPENGL_LEGACY,
    MAAGE_RENDER_SOFTWARE
} maage_render_backend;
/* Requested API family is separate from the concrete active backend above. */
typedef enum maage_render_family {
    MAAGE_RENDER_FAMILY_AUTO = 0,
    MAAGE_RENDER_FAMILY_METAL,
    MAAGE_RENDER_FAMILY_VULKAN,
    MAAGE_RENDER_FAMILY_OPENGL,
    MAAGE_RENDER_FAMILY_SOFTWARE
} maage_render_family;
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
    maage_render_family family;
    unsigned long required_features; /* AUTO skips backends missing these flags. */
} maage_render_desc;

#define MAAGE_RENDER_CAPABILITIES_VERSION 1
#define MAAGE_RENDER_CAP_CLEAR             0x0001UL
#define MAAGE_RENDER_CAP_PRESENT           0x0002UL
#define MAAGE_RENDER_CAP_SHADERS           0x0004UL
#define MAAGE_RENDER_CAP_FIXED_FUNCTION    0x0008UL
#define MAAGE_RENDER_CAP_VERTEX_BUFFERS    0x0010UL
#define MAAGE_RENDER_CAP_INDEX_BUFFERS     0x0020UL
#define MAAGE_RENDER_CAP_RENDER_TARGETS    0x0040UL
#define MAAGE_RENDER_CAP_DEPTH             0x0080UL
#define MAAGE_RENDER_CAP_STENCIL           0x0100UL
#define MAAGE_RENDER_CAP_INSTANCING        0x0200UL
#define MAAGE_RENDER_CAP_COMPUTE           0x0400UL
#define MAAGE_RENDER_CAP_CPU_FRAMEBUFFER   0x0800UL
#define MAAGE_RENDER_CAP_PIXEL_PRESENT     0x1000UL
#define MAAGE_RENDER_FORMAT_RGBA8          0x01UL
#define MAAGE_RENDER_FORMAT_BGRA8          0x02UL
#define MAAGE_RENDER_SYNC_CPU_WAIT         0x01UL
#define MAAGE_RENDER_SYNC_FENCE            0x02UL
#define MAAGE_RENDER_SYNC_BINARY_SEMAPHORE 0x04UL

/* Conservative device/backend capabilities, not a promise that resource/draw APIs already
 * exist. Zero limits mean unknown or inapplicable; never assume a modern GPU.
 * Texture formats describe sampled 2D textures; CPU_FRAMEBUFFER instead uses
 * the neutral RGBA8 pixel-buffer representation in maage/pixels.h. */
typedef struct maage_render_capabilities {
    unsigned int version;
    unsigned long features;
    unsigned long texture_formats;
    unsigned long synchronization;
    unsigned int max_texture_dimension_2d;
    unsigned int max_frames_in_flight;
} maage_render_capabilities;

const char *maage_render_backend_name(maage_render_backend backend);
/* Compile-time availability only, not runtime device/display availability.
 * Unavailable backends return zero. */
int maage_render_backend_compiled(maage_render_backend backend);
/* Parse --renderer=auto|metal|vulkan|opengl|software; last occurrence wins.
 * OpenGL negotiates modern/legacy internally. Explicit selection stays within
 * its family. Removed --renderer-version flags are rejected. */
maage_render_result maage_render_desc_parse_args(int argc, char *const argv[],
    maage_render_desc *desc);
maage_render_result maage_renderer_get_capabilities(const maage_renderer *renderer,
    maage_render_capabilities *capabilities);

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
/* Present a borrowed RGBA8 frame matching the native framebuffer size. Requires
 * PIXEL_PRESENT. Currently Metal and Software; other backends return UNSUPPORTED.
 * No ownership transfer or cross-family fallback occurs. */
maage_render_result maage_renderer_present_pixels(maage_renderer *renderer, const maage_pixel_buffer *pixels);
/* Force presentation resources to refresh on the next frame, if needed. */
void maage_renderer_resize(maage_renderer *renderer);
#endif
