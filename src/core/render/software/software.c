#include "../render_internal.h"
#include <stdlib.h>

typedef struct maage_software_renderer {
    unsigned char *pixels;
    size_t size;
} maage_software_renderer;

static maage_render_result maage_software_create(maage_renderer *renderer)
{
    renderer->platform = calloc(1, sizeof(maage_software_renderer));
    if (!renderer->platform) return MAAGE_RENDER_OUT_OF_MEMORY;
    renderer->capabilities.features |= MAAGE_RENDER_CAP_CPU_FRAMEBUFFER;
    renderer->capabilities.synchronization = MAAGE_RENDER_SYNC_CPU_WAIT;
    return MAAGE_RENDER_OK;
}
static void maage_software_destroy(maage_renderer *renderer)
{
    maage_software_renderer *native = renderer->platform;
    if (!native) return;
    free(native->pixels);
    free(native);
}
static maage_render_result maage_software_present(maage_renderer *renderer, const maage_window_state *state)
{
    maage_software_renderer *native = renderer->platform;
    maage_pixel_buffer buffer;
    maage_window_result result;
    unsigned char color[4], *resized;
    size_t size, index;
    int channel;
    if ((size_t)state->framebuffer_width > (size_t)-1 / 4) return MAAGE_RENDER_UNSUPPORTED;
    buffer.width = state->framebuffer_width;
    buffer.height = state->framebuffer_height;
    buffer.stride = (size_t)buffer.width * 4;
    if ((size_t)buffer.height > (size_t)-1 / buffer.stride) return MAAGE_RENDER_UNSUPPORTED;
    size = buffer.stride * (size_t)buffer.height;
    if (native->size != size) {
        resized = (unsigned char *)realloc(native->pixels, size);
        if (!resized) return MAAGE_RENDER_OUT_OF_MEMORY;
        native->pixels = resized;
        native->size = size;
    }
    for (channel = 0; channel < 4; ++channel) color[channel] = (unsigned char)(renderer->clear[channel] * 255.0f + 0.5f);
    for (index = 0; index < size; index += 4)
        for (channel = 0; channel < 4; ++channel) native->pixels[index + channel] = color[channel];
    buffer.pixels = native->pixels;
    result = maage_window_present_pixels(renderer->window, &buffer);
    renderer->refresh = 0;
    switch (result) {
    case MAAGE_WINDOW_OK: return MAAGE_RENDER_OK;
    case MAAGE_WINDOW_UNAVAILABLE: return MAAGE_RENDER_SKIPPED;
    case MAAGE_WINDOW_UNSUPPORTED: return MAAGE_RENDER_UNSUPPORTED;
    case MAAGE_WINDOW_OUT_OF_MEMORY: return MAAGE_RENDER_OUT_OF_MEMORY;
    default: return MAAGE_RENDER_WINDOW_ERROR;
    }
}
const maage_render_backend_api maage_render_software_api = {
    MAAGE_RENDER_SOFTWARE, maage_software_create, maage_software_destroy, maage_software_present,
    MAAGE_RENDER_CAPABILITIES_VERSION
};
