#include "render_internal.h"
#include <stdlib.h>
#include <string.h>

void maage_render_desc_init(maage_render_desc *desc)
{
    if (!desc) return;
    memset(desc, 0, sizeof(*desc));
    desc->backend = MAAGE_RENDER_AUTO;
}
const char *maage_render_result_string(maage_render_result result)
{
    switch (result) {
    case MAAGE_RENDER_OK: return "success";
    case MAAGE_RENDER_SKIPPED: return "frame deferred until presentation is available";
    case MAAGE_RENDER_INVALID_ARGUMENT: return "invalid rendering argument";
    case MAAGE_RENDER_OUT_OF_MEMORY: return "graphics allocation failed";
    case MAAGE_RENDER_UNSUPPORTED: return "unsupported renderer or surface capabilities";
    case MAAGE_RENDER_UNAVAILABLE: return "no compatible graphics device/backend";
    case MAAGE_RENDER_WINDOW_ERROR: return "window or native surface ownership unavailable";
    case MAAGE_RENDER_DEVICE_LOST: return "graphics device lost";
    case MAAGE_RENDER_SURFACE_LOST: return "native presentation surface lost";
    default: return "native graphics operation failed";
    }
}
maage_render_result maage_renderer_create(const maage_render_desc *desc, maage_renderer **output)
{
    maage_renderer *renderer;
    maage_render_result result;
    const maage_render_backend_api *api = NULL;
    if (!output) return MAAGE_RENDER_INVALID_ARGUMENT;
    *output = NULL;
    if (!desc || !desc->window) return MAAGE_RENDER_INVALID_ARGUMENT;
#if defined(MAAGE_RENDER_HAS_METAL)
    if (desc->backend == MAAGE_RENDER_AUTO || desc->backend == MAAGE_RENDER_METAL)
        api = &maage_render_metal_api;
#elif defined(MAAGE_RENDER_HAS_VULKAN)
    if (desc->backend == MAAGE_RENDER_AUTO || desc->backend == MAAGE_RENDER_VULKAN)
        api = &maage_render_vulkan_api;
#endif
    if (!api) return MAAGE_RENDER_UNSUPPORTED;
    renderer = (maage_renderer *)calloc(1, sizeof(*renderer));
    if (!renderer) return MAAGE_RENDER_OUT_OF_MEMORY;
    renderer->window = desc->window;
    renderer->api = api;
    renderer->clear[3] = 1.0f;
    renderer->refresh = 1;
    if (maage_window_acquire_surface(desc->window, &renderer->native) != MAAGE_WINDOW_OK) {
        free(renderer);
        return MAAGE_RENDER_WINDOW_ERROR;
    }
    result = api->create(renderer);
    if (result != MAAGE_RENDER_OK) { maage_renderer_destroy(renderer); return result; }
    *output = renderer;
    return MAAGE_RENDER_OK;
}
void maage_renderer_destroy(maage_renderer *renderer)
{
    if (!renderer) return;
    renderer->api->destroy(renderer);
    maage_window_release_surface(renderer->window);
    free(renderer);
}
maage_render_backend maage_renderer_get_backend(const maage_renderer *renderer)
{
    return renderer ? renderer->api->kind : MAAGE_RENDER_AUTO;
}
maage_render_result maage_renderer_set_clear_color(maage_renderer *renderer,
    float red, float green, float blue, float alpha)
{
    if (!renderer || !(red >= 0 && red <= 1 && green >= 0 && green <= 1 &&
        blue >= 0 && blue <= 1 && alpha >= 0 && alpha <= 1)) return MAAGE_RENDER_INVALID_ARGUMENT;
    renderer->clear[0] = red;
    renderer->clear[1] = green;
    renderer->clear[2] = blue;
    renderer->clear[3] = alpha;
    return MAAGE_RENDER_OK;
}
maage_render_result maage_renderer_present(maage_renderer *renderer)
{
    maage_window_state state;
    maage_render_result result;
    if (!renderer) return MAAGE_RENDER_INVALID_ARGUMENT;
    if (renderer->failure != MAAGE_RENDER_OK) return renderer->failure;
    if (maage_window_get_state(renderer->window, &state) != MAAGE_WINDOW_OK)
        return MAAGE_RENDER_WINDOW_ERROR;
    if (!state.visible || state.framebuffer_width <= 0 || state.framebuffer_height <= 0)
        return MAAGE_RENDER_SKIPPED;
    result = renderer->api->present(renderer, &state);
    if (result != MAAGE_RENDER_OK && result != MAAGE_RENDER_SKIPPED) renderer->failure = result;
    return result;
}
void maage_renderer_resize(maage_renderer *renderer)
{
    if (renderer) renderer->refresh = 1;
}
