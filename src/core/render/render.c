#include "render_internal.h"
#include "cmakecfg.h"
#include "maage/log.h"
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
static const maage_render_backend_api *maage_render_api(maage_render_backend backend)
{
    switch (backend) {
#if defined(MAAGE_RENDER_HAS_METAL)
    case MAAGE_RENDER_METAL: return &maage_render_metal_api;
#endif
#if defined(MAAGE_RENDER_HAS_VULKAN)
    case MAAGE_RENDER_VULKAN: return &maage_render_vulkan_api;
#endif
#if defined(MAAGE_RENDER_HAS_OPENGL)
    case MAAGE_RENDER_OPENGL: return &maage_render_opengl_api;
#endif
#if defined(MAAGE_RENDER_HAS_OPENGL_LEGACY)
    case MAAGE_RENDER_OPENGL_LEGACY: return &maage_render_opengl_legacy_api;
#endif
#if defined(MAAGE_RENDER_HAS_D3D12)
    case MAAGE_RENDER_D3D12: return &maage_render_d3d12_api;
#endif
#if defined(MAAGE_RENDER_HAS_D3D2)
    case MAAGE_RENDER_D3D2: return &maage_render_d3d2_api;
#endif
#if defined(MAAGE_RENDER_HAS_SOFTWARE)
    case MAAGE_RENDER_SOFTWARE: return &maage_render_software_api;
#endif
    default: return NULL;
    }
}
const char *maage_render_backend_name(maage_render_backend backend)
{
    switch (backend) {
    case MAAGE_RENDER_AUTO: return "auto";
    case MAAGE_RENDER_METAL: return "metal";
    case MAAGE_RENDER_VULKAN: return "vulkan";
    case MAAGE_RENDER_OPENGL: return "opengl";
    case MAAGE_RENDER_OPENGL_LEGACY: return "opengl-legacy";
    case MAAGE_RENDER_D3D12: return "d3d12";
    case MAAGE_RENDER_D3D2: return "d3d2";
    case MAAGE_RENDER_SOFTWARE: return "software";
    default: return "unknown";
    }
}
int maage_render_backend_compiled(maage_render_backend backend)
{
    const maage_render_backend_api *api = maage_render_api(backend);
    return api && api->version == MAAGE_RENDER_CAPABILITIES_VERSION;
}
maage_render_result maage_render_backend_from_args(int argc, char *const argv[], maage_render_backend *backend)
{
    int index, kind, found;
    if (!backend) return MAAGE_RENDER_INVALID_ARGUMENT;
    *backend = MAAGE_RENDER_AUTO;
    for (index = 1; argv && index < argc; ++index) {
        if (!argv[index] || strncmp(argv[index], "--renderer=", 11) != 0) continue;
        found = 0;
        for (kind = MAAGE_RENDER_AUTO; kind <= MAAGE_RENDER_SOFTWARE; ++kind) {
            if (!strcmp(argv[index] + 11, maage_render_backend_name((maage_render_backend)kind))) {
                *backend = (maage_render_backend)kind;
                found = 1;
                break;
            }
        }
        if (!found) return MAAGE_RENDER_INVALID_ARGUMENT;
    }
    return MAAGE_RENDER_OK;
}
maage_render_result maage_renderer_create(const maage_render_desc *desc, maage_renderer **output)
{
    static const maage_render_backend order[] = {
#if MAAGE_PLATFORM_MACOS
        MAAGE_RENDER_METAL,
#elif MAAGE_PLATFORM_WINDOWS
        MAAGE_RENDER_VULKAN, MAAGE_RENDER_D3D12,
#else
        MAAGE_RENDER_VULKAN,
#endif
        MAAGE_RENDER_OPENGL, MAAGE_RENDER_OPENGL_LEGACY, MAAGE_RENDER_SOFTWARE
    };
    maage_renderer *renderer;
    maage_render_result result = MAAGE_RENDER_UNAVAILABLE;
    const maage_render_backend_api *api;
    size_t index, count = desc && desc->backend == MAAGE_RENDER_AUTO ? sizeof(order) / sizeof(order[0]) : 1;
    maage_render_backend backend;
    if (!output) return MAAGE_RENDER_INVALID_ARGUMENT;
    *output = NULL;
    if (!desc || !desc->window || desc->backend < MAAGE_RENDER_AUTO || desc->backend > MAAGE_RENDER_SOFTWARE)
        return MAAGE_RENDER_INVALID_ARGUMENT;
    renderer = (maage_renderer *)calloc(1, sizeof(*renderer));
    if (!renderer) return MAAGE_RENDER_OUT_OF_MEMORY;
    renderer->window = desc->window;
    renderer->clear[3] = 1.0f;
    renderer->refresh = 1;
    if (maage_window_acquire_surface(desc->window, &renderer->native) != MAAGE_WINDOW_OK) {
        free(renderer);
        return MAAGE_RENDER_WINDOW_ERROR;
    }
    for (index = 0; index < count; ++index) {
        backend = desc->backend == MAAGE_RENDER_AUTO ? order[index] : desc->backend;
        api = maage_render_api(backend);
        if (!api || api->version != MAAGE_RENDER_CAPABILITIES_VERSION) {
            if (desc->backend == MAAGE_RENDER_AUTO) continue;
            maage_log_error("maage.render", "Renderer %s is not implemented in this build", maage_render_backend_name(backend));
            result = MAAGE_RENDER_UNSUPPORTED;
            break;
        }
        renderer->api = api;
        memset(&renderer->capabilities, 0, sizeof(renderer->capabilities));
        renderer->capabilities.version = MAAGE_RENDER_CAPABILITIES_VERSION;
        renderer->capabilities.features = MAAGE_RENDER_CAP_CLEAR | MAAGE_RENDER_CAP_PRESENT;
        renderer->capabilities.max_frames_in_flight = 1;
        result = api->create(renderer);
        if (result == MAAGE_RENDER_OK &&
            (renderer->capabilities.features & desc->required_features) != desc->required_features)
            result = MAAGE_RENDER_UNSUPPORTED;
        if (result == MAAGE_RENDER_OK) {
            maage_log_info("maage.render", "Selected renderer: %s", maage_render_backend_name(backend));
            *output = renderer;
            return result;
        }
        maage_log_warn("maage.render", "Renderer %s initialization failed: %s",
            maage_render_backend_name(backend), maage_render_result_string(result));
        api->destroy(renderer);
        renderer->platform = NULL;
        renderer->api = NULL;
        renderer->refresh = 1;
    }
    maage_window_release_surface(renderer->window);
    free(renderer);
    return result;
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
maage_render_result maage_renderer_get_capabilities(const maage_renderer *renderer,
    maage_render_capabilities *capabilities)
{
    if (!renderer || !capabilities) return MAAGE_RENDER_INVALID_ARGUMENT;
    *capabilities = renderer->capabilities;
    return MAAGE_RENDER_OK;
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
