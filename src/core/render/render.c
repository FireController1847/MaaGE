#include "render_internal.h"
#include "cmakecfg.h"
#include "maage/log.h"
#include <stdlib.h>
#include <string.h>

void maage_render_desc_init(maage_render_desc *desc)
{
    if (!desc) return;
    memset(desc, 0, sizeof(*desc));
    desc->family = MAAGE_RENDER_FAMILY_AUTO;
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
    case MAAGE_RENDER_SOFTWARE: return "software";
    default: return "unknown";
    }
}
int maage_render_backend_compiled(maage_render_backend backend)
{
    const maage_render_backend_api *api = maage_render_api(backend);
    return api && api->version == MAAGE_RENDER_CAPABILITIES_VERSION;
}
static const char *const maage_render_family_names[] = {
    "auto", "metal", "vulkan", "opengl", "software"
};
maage_render_result maage_render_desc_parse_args(int argc, char *const argv[], maage_render_desc *desc)
{
    const char *family = "auto";
    int index, kind;
    if (!desc) return MAAGE_RENDER_INVALID_ARGUMENT;
    for (index = 1; argv && index < argc; ++index) {
        if (!argv[index]) continue;
        if (!strncmp(argv[index], "--renderer=", sizeof("--renderer=") - 1))
            family = argv[index] + sizeof("--renderer=") - 1;
        else if (!strcmp(argv[index], "--renderer")) {
            maage_log_error("maage.render", "--renderer requires an =value");
            return MAAGE_RENDER_INVALID_ARGUMENT;
        } else if (!strncmp(argv[index], "--renderer-version", sizeof("--renderer-version") - 1)) {
            maage_log_error("maage.render", "--renderer-version is no longer supported");
            return MAAGE_RENDER_INVALID_ARGUMENT;
        }
    }
    for (kind = MAAGE_RENDER_FAMILY_AUTO; kind <= MAAGE_RENDER_FAMILY_SOFTWARE; ++kind)
        if (!strcmp(family, maage_render_family_names[kind])) break;
    if (kind > MAAGE_RENDER_FAMILY_SOFTWARE) {
        maage_log_error("maage.render", "Unknown renderer '%s'; use auto, metal, vulkan, opengl, or software", family);
        return MAAGE_RENDER_INVALID_ARGUMENT;
    }
    desc->family = (maage_render_family)kind;
    return MAAGE_RENDER_OK;
}
maage_render_result maage_renderer_create(const maage_render_desc *desc, maage_renderer **output)
{
    static const maage_render_backend order[] = {
#if MAAGE_PLATFORM_MACOS
        MAAGE_RENDER_METAL,
#elif MAAGE_PLATFORM_WINDOWS
        MAAGE_RENDER_VULKAN,
#else
        MAAGE_RENDER_VULKAN,
#endif
        MAAGE_RENDER_OPENGL, MAAGE_RENDER_OPENGL_LEGACY, MAAGE_RENDER_SOFTWARE
    };
    static const maage_render_backend opengl[] = { MAAGE_RENDER_OPENGL, MAAGE_RENDER_OPENGL_LEGACY };
    maage_renderer *renderer;
    maage_render_result result = MAAGE_RENDER_UNAVAILABLE;
    const maage_render_backend_api *api;
    size_t index, count = 1;
    maage_render_backend backend, single = MAAGE_RENDER_AUTO;
    const maage_render_backend *candidates = &single;
    if (!output) return MAAGE_RENDER_INVALID_ARGUMENT;
    *output = NULL;
    if (!desc || !desc->window || desc->family < MAAGE_RENDER_FAMILY_AUTO || desc->family > MAAGE_RENDER_FAMILY_SOFTWARE)
        return MAAGE_RENDER_INVALID_ARGUMENT;
    switch (desc->family) {
    case MAAGE_RENDER_FAMILY_AUTO: candidates = order; count = sizeof(order) / sizeof(order[0]); break;
    case MAAGE_RENDER_FAMILY_METAL: single = MAAGE_RENDER_METAL; break;
    case MAAGE_RENDER_FAMILY_VULKAN: single = MAAGE_RENDER_VULKAN; break;
    case MAAGE_RENDER_FAMILY_OPENGL: candidates = opengl; count = sizeof(opengl) / sizeof(opengl[0]); break;
    case MAAGE_RENDER_FAMILY_SOFTWARE: single = MAAGE_RENDER_SOFTWARE; break;
    default: return MAAGE_RENDER_INVALID_ARGUMENT;
    }
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
        backend = candidates[index];
        api = maage_render_api(backend);
        if (!api || api->version != MAAGE_RENDER_CAPABILITIES_VERSION) {
            result = MAAGE_RENDER_UNSUPPORTED;
            continue;
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
    if (count == 1 && !maage_render_backend_compiled(single))
        maage_log_error("maage.render", "Renderer implementation '%s' is not implemented in this build", maage_render_backend_name(single));
    else maage_log_error("maage.render", "No usable implementation for renderer family '%s'", maage_render_family_names[desc->family]);
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
maage_render_result maage_renderer_present_pixels(maage_renderer *renderer, const maage_pixel_buffer *pixels)
{
    maage_render_result result;
    maage_window_state state;
    if (!renderer || !pixels || !pixels->pixels || pixels->width <= 0 || pixels->height <= 0 ||
        (size_t)pixels->width > (size_t)-1 / 4 || pixels->stride < (size_t)pixels->width * 4 ||
        pixels->stride > (size_t)-1 / (size_t)pixels->height) return MAAGE_RENDER_INVALID_ARGUMENT;
    if (!(renderer->capabilities.features & MAAGE_RENDER_CAP_PIXEL_PRESENT)) return MAAGE_RENDER_UNSUPPORTED;
    if (maage_window_get_state(renderer->window, &state) != MAAGE_WINDOW_OK) return MAAGE_RENDER_WINDOW_ERROR;
    if (state.framebuffer_width != pixels->width || state.framebuffer_height != pixels->height) return MAAGE_RENDER_SKIPPED;
    renderer->pixels = pixels;
    result = maage_renderer_present(renderer);
    renderer->pixels = NULL;
    return result;
}
