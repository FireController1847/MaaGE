#include "render_internal.h"
#include "cmakecfg.h"
#include "maage/log.h"
#include <stdlib.h>
#include <string.h>
#include <float.h>

maage_status maage_render_desc_init(maage_render_desc *desc) {
    if (!desc) return maage_status_make(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_CONTEXT_RENDER, MAAGE_OPERATION_INITIALIZE, 0);
    memset(desc, 0, sizeof(*desc));
    desc->family = MAAGE_RENDER_FAMILY_AUTO;
    return maage_status_make(MAAGE_STATUS_SUCCESS, 0, MAAGE_OPERATION_INITIALIZE, 0);
}

static const maage_render_backend_api *maage_render_api(maage_render_backend backend) {
    switch (backend) {
#if defined(MAAGE_RENDER_HAS_METAL)
    case MAAGE_RENDER_METAL:
        return &maage_render_metal_api;
#endif
#if defined(MAAGE_RENDER_HAS_VULKAN)
    case MAAGE_RENDER_VULKAN:
        return &maage_render_vulkan_api;
#endif
#if defined(MAAGE_RENDER_HAS_OPENGL)
    case MAAGE_RENDER_OPENGL:
        return &maage_render_opengl_api;
#endif
#if defined(MAAGE_RENDER_HAS_OPENGL_LEGACY)
    case MAAGE_RENDER_OPENGL_LEGACY:
        return &maage_render_opengl_legacy_api;
#endif
#if defined(MAAGE_RENDER_HAS_SOFTWARE)
    case MAAGE_RENDER_SOFTWARE:
        return &maage_render_software_api;
#endif
    default:
        return NULL;
    }
}
const char *maage_render_backend_name(maage_render_backend backend) {
    switch (backend) {
    case MAAGE_RENDER_AUTO:
        return "auto";
    case MAAGE_RENDER_METAL:
        return "metal";
    case MAAGE_RENDER_VULKAN:
        return "vulkan";
    case MAAGE_RENDER_OPENGL:
        return "opengl";
    case MAAGE_RENDER_OPENGL_LEGACY:
        return "opengl-legacy";
    case MAAGE_RENDER_SOFTWARE:
        return "software";
    default:
        return "unknown";
    }
}
int maage_render_backend_compiled(maage_render_backend backend) {
    const maage_render_backend_api *api = maage_render_api(backend);
    return api && api->version == MAAGE_RENDER_CAPABILITIES_VERSION;
}
static const char *const maage_render_family_names[] = {
    "auto", "metal", "vulkan", "opengl", "software"};
maage_status maage_render_desc_parse_args(int argc, char *const argv[], maage_render_desc *desc) {
    const char *family = "auto";
    int index, kind;
    if (!desc) return MAAGE_STATUS_VALUE(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_CONTEXT_RENDER, MAAGE_OPERATION_PARSE, 0);
    for (index = 1; argv && index < argc; ++index) {
        if (!argv[index]) continue;
        if (!strncmp(argv[index], "--renderer=", sizeof("--renderer=") - 1)) family = argv[index] + sizeof("--renderer=") - 1;
        else if (!strcmp(argv[index], "--renderer")) {
            maage_log_error("maage.render", "--renderer requires an =value");
            return MAAGE_STATUS_VALUE(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_CONTEXT_RENDER, MAAGE_OPERATION_PARSE, 0);
        } else if (!strncmp(argv[index], "--renderer-version", sizeof("--renderer-version") - 1)) {
            maage_log_error("maage.render", "--renderer-version is no longer supported");
            return MAAGE_STATUS_VALUE(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_CONTEXT_RENDER, MAAGE_OPERATION_PARSE, 0);
        }
    }
    for (kind = MAAGE_RENDER_FAMILY_AUTO; kind <= MAAGE_RENDER_FAMILY_SOFTWARE; ++kind)
        if (!strcmp(family, maage_render_family_names[kind])) break;
    if (kind > MAAGE_RENDER_FAMILY_SOFTWARE) {
        maage_log_error("maage.render", "Unknown renderer '%s'; use auto, metal, vulkan, opengl, or software", family);
        return MAAGE_STATUS_VALUE(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_CONTEXT_RENDER, MAAGE_OPERATION_PARSE, 0);
    }
    desc->family = (maage_render_family) kind;
    return MAAGE_STATUS_VALUE(MAAGE_STATUS_SUCCESS, MAAGE_CONTEXT_RENDER, MAAGE_OPERATION_PARSE, 0);
}
maage_status maage_renderer_create(const maage_render_desc *desc, maage_renderer **output, maage_status_trace *trace) {
    static const maage_render_backend order[] = {
#if MAAGE_PLATFORM_MACOS
        MAAGE_RENDER_METAL,
#if defined(MAAGE_RENDER_HAS_VULKAN)
        MAAGE_RENDER_VULKAN,
#endif
#elif MAAGE_PLATFORM_WINDOWS
        MAAGE_RENDER_VULKAN,
#else
        MAAGE_RENDER_VULKAN,
#endif
        MAAGE_RENDER_OPENGL, MAAGE_RENDER_OPENGL_LEGACY, MAAGE_RENDER_SOFTWARE};
    static const maage_render_backend opengl[] = {MAAGE_RENDER_OPENGL, MAAGE_RENDER_OPENGL_LEGACY};
    maage_renderer *renderer;
    maage_status cleanup, result = MAAGE_STATUS_VALUE(MAAGE_STATUS_SYSTEM_NOT_SUPPORTED, MAAGE_CONTEXT_RENDER, MAAGE_OPERATION_INITIALIZE, 0);
    maage_status first_failure = 0;
    const maage_render_backend_api *api;
    size_t index, count = 1;
    maage_render_backend backend, single = MAAGE_RENDER_AUTO;
    const maage_render_backend *candidates = &single;
    int recovered = 0;
    if (!output) return MAAGE_STATUS_PROPAGATE(trace, MAAGE_STATUS_VALUE(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_CONTEXT_RENDER, MAAGE_OPERATION_INITIALIZE, 0), MAAGE_CONTEXT_RENDER, MAAGE_OPERATION_INITIALIZE);
    *output = NULL;
    if (!desc || !desc->window || desc->family < MAAGE_RENDER_FAMILY_AUTO || desc->family > MAAGE_RENDER_FAMILY_SOFTWARE) return MAAGE_STATUS_PROPAGATE(trace, MAAGE_STATUS_VALUE(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_CONTEXT_RENDER, MAAGE_OPERATION_INITIALIZE, 0), MAAGE_CONTEXT_RENDER, MAAGE_OPERATION_INITIALIZE);
    switch (desc->family) {
    case MAAGE_RENDER_FAMILY_AUTO:
        candidates = order;
        count = sizeof(order) / sizeof(order[0]);
        break;
    case MAAGE_RENDER_FAMILY_METAL:
        single = MAAGE_RENDER_METAL;
        break;
    case MAAGE_RENDER_FAMILY_VULKAN:
        single = MAAGE_RENDER_VULKAN;
        break;
    case MAAGE_RENDER_FAMILY_OPENGL:
        candidates = opengl;
        count = sizeof(opengl) / sizeof(opengl[0]);
        break;
    case MAAGE_RENDER_FAMILY_SOFTWARE:
        single = MAAGE_RENDER_SOFTWARE;
        break;
    default:
        return MAAGE_STATUS_PROPAGATE(trace, MAAGE_STATUS_VALUE(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_CONTEXT_RENDER, MAAGE_OPERATION_INITIALIZE, 0), MAAGE_CONTEXT_RENDER, MAAGE_OPERATION_INITIALIZE);
    }
    renderer = (maage_renderer *) calloc(1, sizeof(*renderer));
    if (!renderer) return MAAGE_STATUS_PROPAGATE(trace, MAAGE_STATUS_VALUE(MAAGE_STATUS_ALLOCATION_FAILED, MAAGE_CONTEXT_RENDER, MAAGE_OPERATION_INITIALIZE, 0), MAAGE_CONTEXT_RENDER, MAAGE_OPERATION_INITIALIZE);
    renderer->window = desc->window;
    renderer->clear[3] = 1.0f;
    renderer->sky_light = 1.0f;
    renderer->refresh = 1;
    result = maage_window_acquire_surface(desc->window, &renderer->native);
    if (maage_status_failed(result)) {
        MAAGE_STATUS_TRACE(trace, result, MAAGE_CONTEXT_WINDOW, MAAGE_OPERATION_ACQUIRE);
        free(renderer);
        return MAAGE_STATUS_PROPAGATE(trace, result, MAAGE_CONTEXT_RENDER, MAAGE_OPERATION_INITIALIZE);
    }
    for (index = 0; index < count; ++index) {
        backend = candidates[index];
        api = maage_render_api(backend);
        if (!api || api->version != MAAGE_RENDER_CAPABILITIES_VERSION) {
            result = MAAGE_STATUS_VALUE(MAAGE_STATUS_NOT_IMPLEMENTED, MAAGE_CONTEXT_RENDER, MAAGE_OPERATION_INITIALIZE, 0);
            maage_log_verbose("maage.render", "Renderer '%s' is not compiled with the current interface.", maage_render_backend_name(backend));
            continue;
        }
        renderer->api = api;
        renderer->device_name[0] = 0;
        memset(&renderer->capabilities, 0, sizeof(renderer->capabilities));
        renderer->capabilities.version = MAAGE_RENDER_CAPABILITIES_VERSION;
        renderer->capabilities.features = MAAGE_RENDER_CAP_CLEAR | MAAGE_RENDER_CAP_PRESENT;
        renderer->capabilities.max_frames_in_flight = 1;
        renderer->operation_trace = trace;
        renderer->trace_operation = MAAGE_OPERATION_INITIALIZE;
        result = api->create(renderer);
        renderer->operation_trace = NULL;
        if (maage_status_code(result) == MAAGE_STATUS_SUCCESS && (renderer->capabilities.features & desc->required_features) != desc->required_features) {
            result = MAAGE_STATUS_VALUE(MAAGE_STATUS_NOT_IMPLEMENTED, MAAGE_CONTEXT_RENDER, MAAGE_OPERATION_INITIALIZE, 0);
            maage_log_status(MAAGE_LOG_WARNING, "maage.render", result, "Renderer '%s' lacks required capabilities 0x%08lX (available 0x%08lX).", maage_render_backend_name(backend), desc->required_features & ~renderer->capabilities.features, renderer->capabilities.features);
        } else if (maage_status_code(result) != MAAGE_STATUS_SUCCESS) maage_log_status(MAAGE_LOG_WARNING, "maage.render", result, "Renderer '%s' initialization failed.", maage_render_backend_name(backend));
        if (maage_status_code(result) == MAAGE_STATUS_SUCCESS) {
            maage_log_info("maage.render", "Selected renderer: %s", maage_render_backend_name(backend));
            *output = renderer;
            if (recovered) {
                maage_status_trace_recover(trace, result, MAAGE_CONTEXT_RENDER, MAAGE_OPERATION_INITIALIZE, __FILE__, __LINE__);
                result = maage_status_make(MAAGE_STATUS_PARTIAL, MAAGE_CONTEXT_RENDER, MAAGE_OPERATION_INITIALIZE, 0);
            }
            return MAAGE_STATUS_PROPAGATE(trace, result, MAAGE_CONTEXT_RENDER, MAAGE_OPERATION_INITIALIZE);
        }
        recovered = 1;
        if (!first_failure) first_failure = result;
        MAAGE_STATUS_TRACE(trace, result, MAAGE_CONTEXT_RENDER, MAAGE_OPERATION_INITIALIZE);
        renderer->operation_trace = trace;
        renderer->trace_operation = MAAGE_OPERATION_DESTROY;
        cleanup = api->destroy(renderer);
        renderer->operation_trace = NULL;
        MAAGE_STATUS_PROPAGATE(trace, cleanup, MAAGE_CONTEXT_RENDER, MAAGE_OPERATION_DESTROY);
        renderer->platform = NULL;
        renderer->api = NULL;
        renderer->refresh = 1;
        if (maage_status_is_fatal(result)) {
            first_failure = result;
            break;
        }
    }
    maage_window_release_surface(renderer->window);
    free(renderer);
    if (first_failure) result = first_failure;
    return MAAGE_STATUS_PROPAGATE(trace, result, MAAGE_CONTEXT_RENDER, MAAGE_OPERATION_INITIALIZE);
}
maage_status maage_renderer_destroy(maage_renderer *renderer, maage_status_trace *trace) {
    maage_status result, released;
    if (!renderer) return maage_status_make(MAAGE_STATUS_NOOP, 0, MAAGE_OPERATION_DESTROY, 0);
    while (renderer->meshes)
        maage_render_mesh_destroy(renderer->meshes);
    while (renderer->textures)
        maage_render_texture_destroy(renderer->textures);
    renderer->operation_trace = trace;
    renderer->trace_operation = MAAGE_OPERATION_DESTROY;
    result = renderer->api->destroy(renderer);
    renderer->operation_trace = NULL;
    released = maage_window_release_surface(renderer->window);
    if (!maage_status_failed(result)) result = released;
    free(renderer->ordered);
    free(renderer);
    return MAAGE_STATUS_PROPAGATE(trace, maage_status_failed(result) ? result : maage_status_make(MAAGE_STATUS_SUCCESS, 0, MAAGE_OPERATION_DESTROY, 0), MAAGE_CONTEXT_RENDER, MAAGE_OPERATION_DESTROY);
}
maage_status maage_renderer_set_vsync(maage_renderer *renderer, int enabled) {
    if (!renderer || (enabled != 0 && enabled != 1)) return MAAGE_STATUS_VALUE(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_CONTEXT_RENDER, MAAGE_OPERATION_SET, 0);
    if (!renderer->api->set_vsync) return MAAGE_STATUS_VALUE(MAAGE_STATUS_NOT_IMPLEMENTED, MAAGE_CONTEXT_RENDER, MAAGE_OPERATION_SET, 0);
    return renderer->api->set_vsync(renderer, enabled);
}
maage_render_backend maage_renderer_get_backend(const maage_renderer *renderer) {
    return renderer ? renderer->api->kind : MAAGE_RENDER_AUTO;
}
const char *maage_renderer_device_name(const maage_renderer *renderer) {
    return renderer && renderer->device_name[0] ? renderer->device_name : NULL;
}
maage_status maage_renderer_get_capabilities(const maage_renderer *renderer, maage_render_capabilities *capabilities) {
    if (capabilities) memset(capabilities, 0, sizeof(*capabilities));
    if (!renderer || !capabilities) return MAAGE_STATUS_VALUE(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_CONTEXT_RENDER, MAAGE_OPERATION_QUERY, 0);
    *capabilities = renderer->capabilities;
    return MAAGE_STATUS_VALUE(MAAGE_STATUS_SUCCESS, MAAGE_CONTEXT_RENDER, MAAGE_OPERATION_QUERY, 0);
}
maage_status maage_renderer_set_clear_color(maage_renderer *renderer, float red, float green, float blue, float alpha) {
    if (!renderer || !(red >= 0 && red <= 1 && green >= 0 && green <= 1 && blue >= 0 && blue <= 1 && alpha >= 0 && alpha <= 1)) return MAAGE_STATUS_VALUE(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_CONTEXT_RENDER, MAAGE_OPERATION_SET, 0);
    renderer->clear[0] = red;
    renderer->clear[1] = green;
    renderer->clear[2] = blue;
    renderer->clear[3] = alpha;
    return MAAGE_STATUS_VALUE(MAAGE_STATUS_SUCCESS, MAAGE_CONTEXT_RENDER, MAAGE_OPERATION_SET, 0);
}
maage_status maage_renderer_set_scene(maage_renderer *renderer, float sky_light, const maage_render_ui_data *background, const maage_render_ui_data *overlay) {
    if (!renderer || !(sky_light >= 0 && sky_light <= 1)) return MAAGE_STATUS_VALUE(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_CONTEXT_RENDER, MAAGE_OPERATION_SET, 0);
    if (!(renderer->capabilities.features & MAAGE_RENDER_CAP_SCENE) || !renderer->api->frame) return MAAGE_STATUS_VALUE(MAAGE_STATUS_NOT_IMPLEMENTED, MAAGE_CONTEXT_RENDER, MAAGE_OPERATION_SET, 0);
    renderer->sky_light = sky_light;
    renderer->background = background;
    renderer->overlay = overlay;
    return MAAGE_STATUS_VALUE(MAAGE_STATUS_SUCCESS, MAAGE_CONTEXT_RENDER, MAAGE_OPERATION_SET, 0);
}
maage_status maage_renderer_set_fog(maage_renderer *renderer, float red, float green, float blue, float density) {
    if (!renderer || !(red >= 0 && red <= 1 && green >= 0 && green <= 1 && blue >= 0 && blue <= 1 && density >= 0 && density <= FLT_MAX)) return maage_status_make(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_CONTEXT_RENDER, MAAGE_OPERATION_SET, 0);
    if (density > 0 && !(renderer->capabilities.features & MAAGE_RENDER_CAP_FOG)) return maage_status_make(MAAGE_STATUS_NOT_IMPLEMENTED, MAAGE_CONTEXT_RENDER, MAAGE_OPERATION_SET, 0);
    renderer->fog[0] = red;
    renderer->fog[1] = green;
    renderer->fog[2] = blue;
    renderer->fog[3] = density;
    return maage_status_make(MAAGE_STATUS_SUCCESS, 0, MAAGE_OPERATION_SET, 0);
}
maage_status maage_renderer_set_foreground(maage_renderer *renderer, const maage_render_mesh *mesh, const float view_projection[16], float sky_light, float block_light) {
    int index;
    if (!renderer || !(sky_light >= 0 && sky_light <= 1 && block_light >= 0 && block_light <= 1) || (mesh && (mesh->renderer != renderer || !view_projection))) return maage_status_make(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_CONTEXT_RENDER, MAAGE_OPERATION_SET, 0);
    if (!(renderer->capabilities.features & MAAGE_RENDER_CAP_TEXTURED_MESH) || !(renderer->capabilities.features & MAAGE_RENDER_CAP_SCENE)) return maage_status_make(MAAGE_STATUS_NOT_IMPLEMENTED, MAAGE_CONTEXT_RENDER, MAAGE_OPERATION_SET, 0);
    if (mesh) {
        for (index = 0; index < 16; ++index)
            if (!(view_projection[index] >= -FLT_MAX && view_projection[index] <= FLT_MAX))
                return maage_status_make(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_CONTEXT_RENDER, MAAGE_OPERATION_SET, 2);
        memcpy(renderer->foreground_matrix, view_projection, sizeof(renderer->foreground_matrix));
    }
    renderer->foreground = mesh;
    renderer->foreground_light[0] = sky_light;
    renderer->foreground_light[1] = block_light;
    return maage_status_make(MAAGE_STATUS_SUCCESS, 0, MAAGE_OPERATION_SET, 0);
}
maage_status maage_renderer_set_instances(maage_renderer *renderer, const maage_render_instance *instances, size_t count) {
    size_t i, axis;
    if (!renderer || count > (size_t) - 1 / sizeof(*instances) || (count && !instances)) return maage_status_make(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_CONTEXT_RENDER, MAAGE_OPERATION_SET, 0);
    if (count && (renderer->capabilities.features & MAAGE_RENDER_CAP_SCENE) == 0) return maage_status_make(MAAGE_STATUS_NOT_IMPLEMENTED, MAAGE_CONTEXT_RENDER, MAAGE_OPERATION_SET, 0);
    for (i = 0; i < count; ++i) {
        if (!instances[i].mesh || instances[i].mesh->renderer != renderer ||
            !(instances[i].sky_light >= 0 && instances[i].sky_light <= 1) ||
            !(instances[i].block_light >= 0 && instances[i].block_light <= 1))
            return maage_status_make(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_CONTEXT_RENDER, MAAGE_OPERATION_SET, 0);
        for (axis = 0; axis < 16; ++axis)
            if (!(instances[i].transform[axis] >= -FLT_MAX && instances[i].transform[axis] <= FLT_MAX))
                return maage_status_make(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_CONTEXT_RENDER, MAAGE_OPERATION_SET, 0);
    }
    renderer->instances = instances;
    renderer->instance_count = count;
    return maage_status_make(MAAGE_STATUS_SUCCESS, 0, MAAGE_OPERATION_SET, 0);
}

maage_status maage_renderer_set_animation_tick(maage_renderer *renderer, float tick) {
    if (!renderer || !(tick >= 0 && tick <= FLT_MAX)) return maage_status_make(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_CONTEXT_RENDER, MAAGE_OPERATION_SET, 0);
    if (!(renderer->capabilities.features & MAAGE_RENDER_CAP_SCENE)) return maage_status_make(MAAGE_STATUS_NOT_IMPLEMENTED, MAAGE_CONTEXT_RENDER, MAAGE_OPERATION_SET, 0);
    renderer->animation_tick = tick;
    return maage_status_make(MAAGE_STATUS_SUCCESS, 0, MAAGE_OPERATION_SET, 0);
}
static maage_status maage_render_submit(maage_renderer *renderer, int preparing, const maage_render_mesh *const *meshes, size_t count, const float matrix[16], const maage_render_ui_data *ui, maage_status_trace *trace) {
    maage_window_state state;
    maage_render_frame_plan plan;
    maage_status result;
    if (!renderer) return MAAGE_STATUS_PROPAGATE(trace, maage_status_make(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_CONTEXT_RENDER, MAAGE_OPERATION_PRESENT, 0), MAAGE_CONTEXT_RENDER, MAAGE_OPERATION_PRESENT);
    if (maage_status_code(renderer->failure) != MAAGE_STATUS_SUCCESS) return MAAGE_STATUS_PROPAGATE(trace, renderer->failure, MAAGE_CONTEXT_RENDER, MAAGE_OPERATION_PRESENT);
    if (!renderer->api->frame && (preparing || matrix || count || ui)) return MAAGE_STATUS_PROPAGATE(trace, maage_status_make(MAAGE_STATUS_NOT_IMPLEMENTED, MAAGE_CONTEXT_RENDER, MAAGE_OPERATION_PRESENT, 0), MAAGE_CONTEXT_RENDER, MAAGE_OPERATION_PRESENT);
    result = maage_window_get_state(renderer->window, &state);
    if (maage_status_failed(result)) return MAAGE_STATUS_PROPAGATE(trace, result, MAAGE_CONTEXT_RENDER, MAAGE_OPERATION_PRESENT);
    if (preparing && state.visible) return MAAGE_STATUS_PROPAGATE(trace, maage_status_make(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_CONTEXT_RENDER, MAAGE_OPERATION_PRESENT, 0), MAAGE_CONTEXT_RENDER, MAAGE_OPERATION_PRESENT);
    if ((!preparing && !state.visible) || state.framebuffer_width <= 0 || state.framebuffer_height <= 0) return MAAGE_STATUS_PROPAGATE(trace, maage_status_make(MAAGE_STATUS_NOOP, 0, MAAGE_OPERATION_PRESENT, 0), MAAGE_CONTEXT_RENDER, MAAGE_OPERATION_PRESENT);
    if (renderer->api->frame) {
        result = maage_render_frame_plan_prepare(renderer, meshes, count, matrix, ui, preparing, &plan);
        if (maage_status_failed(result)) return MAAGE_STATUS_PROPAGATE(trace, result, MAAGE_CONTEXT_RENDER, MAAGE_OPERATION_PRESENT);
    }
    renderer->operation_trace = trace;
    renderer->trace_operation = MAAGE_OPERATION_PRESENT;
    result = renderer->api->frame ? renderer->api->frame(renderer, &state, &plan) : renderer->api->present(renderer, &state);
    renderer->operation_trace = NULL;
    if (maage_status_failed(result)) renderer->failure = result;
    return MAAGE_STATUS_PROPAGATE(trace, result, MAAGE_CONTEXT_RENDER, MAAGE_OPERATION_PRESENT);
}
maage_status maage_renderer_prepare_frame(maage_renderer *renderer, maage_status_trace *trace) {
    return maage_render_submit(renderer, 1, NULL, 0, NULL, NULL, trace);
}
maage_status maage_renderer_prepare_ui_frame(maage_renderer *renderer, const maage_render_ui_data *ui, maage_status_trace *trace) {
    return maage_render_submit(renderer, 1, NULL, 0, NULL, ui, trace);
}
maage_status maage_renderer_present(maage_renderer *renderer, maage_status_trace *trace) {
    return maage_render_submit(renderer, 0, NULL, 0, NULL, NULL, trace);
}
static void maage_render_texture_release(maage_render_texture *texture) {
    if (--texture->references != 0) return;
    texture->renderer->api->destroy_texture(texture);
    if (texture->previous) texture->previous->next = texture->next;
    else
        texture->renderer->textures = texture->next;
    if (texture->next) texture->next->previous = texture->previous;
    free(texture);
}
maage_status maage_renderer_create_texture(maage_renderer *renderer, const maage_pixel_buffer *atlas, maage_render_texture **output, maage_status_trace *trace) {
    maage_render_texture *texture;
    maage_status result;
    if (!output) return MAAGE_STATUS_PROPAGATE(trace, MAAGE_STATUS_VALUE(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_CONTEXT_RENDER, MAAGE_OPERATION_CREATE, 0), MAAGE_CONTEXT_RENDER, MAAGE_OPERATION_CREATE);
    *output = NULL;
    if (!renderer || !atlas || !atlas->pixels || atlas->width <= 0 ||
        atlas->height <= 0 || (size_t) atlas->width > (size_t) - 1 / 4 ||
        atlas->stride < (size_t) atlas->width * 4 ||
        atlas->stride > (size_t) - 1 / (size_t) atlas->height)
        return MAAGE_STATUS_PROPAGATE(trace, MAAGE_STATUS_VALUE(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_CONTEXT_RENDER, MAAGE_OPERATION_CREATE, 0), MAAGE_CONTEXT_RENDER, MAAGE_OPERATION_CREATE);
    if (maage_status_code(renderer->failure) != MAAGE_STATUS_SUCCESS) return MAAGE_STATUS_PROPAGATE(trace, renderer->failure, MAAGE_CONTEXT_RENDER, MAAGE_OPERATION_CREATE);
    if (!renderer->api->create_texture || !renderer->api->destroy_texture) return MAAGE_STATUS_PROPAGATE(trace, MAAGE_STATUS_VALUE(MAAGE_STATUS_NOT_IMPLEMENTED, MAAGE_CONTEXT_RENDER, MAAGE_OPERATION_CREATE, 0), MAAGE_CONTEXT_RENDER, MAAGE_OPERATION_CREATE);
    texture = (maage_render_texture *) calloc(1, sizeof(*texture));
    if (!texture) return MAAGE_STATUS_PROPAGATE(trace, MAAGE_STATUS_VALUE(MAAGE_STATUS_ALLOCATION_FAILED, MAAGE_CONTEXT_RENDER, MAAGE_OPERATION_CREATE, 0), MAAGE_CONTEXT_RENDER, MAAGE_OPERATION_CREATE);
    texture->renderer = renderer;
    texture->references = 1;
    texture->owned = 1;
    renderer->operation_trace = trace;
    renderer->trace_operation = MAAGE_OPERATION_CREATE;
    result = renderer->api->create_texture(renderer, texture, atlas);
    renderer->operation_trace = NULL;
    if (maage_status_code(result) != MAAGE_STATUS_SUCCESS) {
        renderer->api->destroy_texture(texture);
        free(texture);
        return MAAGE_STATUS_PROPAGATE(trace, result, MAAGE_CONTEXT_RENDER, MAAGE_OPERATION_CREATE);
    }
    texture->next = renderer->textures;
    if (texture->next) texture->next->previous = texture;
    renderer->textures = texture;
    *output = texture;
    return MAAGE_STATUS_PROPAGATE(trace, MAAGE_STATUS_VALUE(MAAGE_STATUS_SUCCESS, MAAGE_CONTEXT_RENDER, MAAGE_OPERATION_CREATE, 0), MAAGE_CONTEXT_RENDER, MAAGE_OPERATION_CREATE);
}
maage_status maage_render_texture_destroy(maage_render_texture *texture) {
    if (!texture || !texture->owned) return maage_status_make(MAAGE_STATUS_NOOP, 0, MAAGE_OPERATION_DESTROY, 0);
    texture->owned = 0;
    maage_render_texture_release(texture);
    return maage_status_make(MAAGE_STATUS_SUCCESS, 0, MAAGE_OPERATION_DESTROY, 0);
}
maage_status maage_renderer_upload_geometry(maage_renderer *renderer, const maage_mesh_data *data, maage_render_texture *texture, maage_render_mesh **output, maage_status_trace *trace) {
    maage_render_mesh *mesh;
    maage_status result;
    if (!output) return MAAGE_STATUS_PROPAGATE(trace, MAAGE_STATUS_VALUE(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_CONTEXT_RENDER, MAAGE_OPERATION_UPLOAD, 0), MAAGE_CONTEXT_RENDER, MAAGE_OPERATION_UPLOAD);
    *output = NULL;
    if (!renderer || !texture || texture->renderer != renderer || !texture->owned) return MAAGE_STATUS_PROPAGATE(trace, MAAGE_STATUS_VALUE(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_CONTEXT_RENDER, MAAGE_OPERATION_UPLOAD, 0), MAAGE_CONTEXT_RENDER, MAAGE_OPERATION_UPLOAD);
    if (maage_status_code(renderer->failure) != MAAGE_STATUS_SUCCESS) return MAAGE_STATUS_PROPAGATE(trace, renderer->failure, MAAGE_CONTEXT_RENDER, MAAGE_OPERATION_UPLOAD);
    if (!renderer->api->upload_geometry) return MAAGE_STATUS_PROPAGATE(trace, MAAGE_STATUS_VALUE(MAAGE_STATUS_NOT_IMPLEMENTED, MAAGE_CONTEXT_RENDER, MAAGE_OPERATION_UPLOAD, 0), MAAGE_CONTEXT_RENDER, MAAGE_OPERATION_UPLOAD);
    result = maage_render_mesh_validate(data);
    if (maage_status_failed(result)) return MAAGE_STATUS_PROPAGATE(trace, result, MAAGE_CONTEXT_RENDER, MAAGE_OPERATION_UPLOAD);
    mesh = (maage_render_mesh *) calloc(1, sizeof(*mesh));
    if (!mesh) return MAAGE_STATUS_PROPAGATE(trace, MAAGE_STATUS_VALUE(MAAGE_STATUS_ALLOCATION_FAILED, MAAGE_CONTEXT_RENDER, MAAGE_OPERATION_UPLOAD, 0), MAAGE_CONTEXT_RENDER, MAAGE_OPERATION_UPLOAD);
    mesh->renderer = renderer;
    mesh->texture = texture;
    renderer->operation_trace = trace;
    renderer->trace_operation = MAAGE_OPERATION_UPLOAD;
    result = renderer->api->upload_geometry(renderer, mesh, data);
    renderer->operation_trace = NULL;
    if (maage_status_code(result) != MAAGE_STATUS_SUCCESS) {
        renderer->api->destroy_mesh(mesh);
        maage_render_geometry_dispose(&mesh->geometry);
        free(mesh);
        return MAAGE_STATUS_PROPAGATE(trace, result, MAAGE_CONTEXT_RENDER, MAAGE_OPERATION_UPLOAD);
    }
    ++texture->references;
    mesh->next = renderer->meshes;
    if (mesh->next) mesh->next->previous = mesh;
    renderer->meshes = mesh;
    *output = mesh;
    return MAAGE_STATUS_PROPAGATE(trace, MAAGE_STATUS_VALUE(MAAGE_STATUS_SUCCESS, MAAGE_CONTEXT_RENDER, MAAGE_OPERATION_UPLOAD, 0), MAAGE_CONTEXT_RENDER, MAAGE_OPERATION_UPLOAD);
}
maage_status maage_renderer_upload_mesh(maage_renderer *renderer, const maage_mesh_data *data, const maage_pixel_buffer *atlas, maage_render_mesh **output, maage_status_trace *trace) {
    maage_render_texture *texture;
    maage_status result;
    if (!output) return MAAGE_STATUS_PROPAGATE(trace, MAAGE_STATUS_VALUE(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_CONTEXT_RENDER, MAAGE_OPERATION_UPLOAD, 0), MAAGE_CONTEXT_RENDER, MAAGE_OPERATION_UPLOAD);
    *output = NULL;
    result = maage_renderer_create_texture(renderer, atlas, &texture, trace);
    if (maage_status_code(result) != MAAGE_STATUS_SUCCESS) return MAAGE_STATUS_PROPAGATE(trace, result, MAAGE_CONTEXT_RENDER, MAAGE_OPERATION_UPLOAD);
    result = maage_renderer_upload_geometry(renderer, data, texture, output, trace);
    maage_render_texture_destroy(texture);
    return MAAGE_STATUS_PROPAGATE(trace, result, MAAGE_CONTEXT_RENDER, MAAGE_OPERATION_UPLOAD);
}
maage_status maage_render_mesh_destroy(maage_render_mesh *mesh) {
    if (!mesh) return maage_status_make(MAAGE_STATUS_NOOP, 0, MAAGE_OPERATION_DESTROY, 0);
    if (mesh->renderer->foreground == mesh) mesh->renderer->foreground = NULL;
    mesh->renderer->api->destroy_mesh(mesh);
    maage_render_geometry_dispose(&mesh->geometry);
    if (mesh->previous) mesh->previous->next = mesh->next;
    else
        mesh->renderer->meshes = mesh->next;
    if (mesh->next) mesh->next->previous = mesh->previous;
    maage_render_texture_release(mesh->texture);
    free(mesh);
    return maage_status_make(MAAGE_STATUS_SUCCESS, 0, MAAGE_OPERATION_DESTROY, 0);
}
maage_status maage_renderer_draw_frame(maage_renderer *renderer, const maage_render_mesh *const *meshes, size_t count, const float view_projection[16], const maage_render_ui_data *ui, maage_status_trace *trace) {
    if (!view_projection) return MAAGE_STATUS_PROPAGATE(trace, maage_status_make(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_CONTEXT_RENDER, MAAGE_OPERATION_PRESENT, 0), MAAGE_CONTEXT_RENDER, MAAGE_OPERATION_PRESENT);
    return maage_render_submit(renderer, 0, meshes, count, view_projection, ui, trace);
}
maage_status maage_renderer_draw_meshes(maage_renderer *renderer, const maage_render_mesh *const *meshes, size_t count, const float view_projection[16], maage_status_trace *trace) {
    return MAAGE_STATUS_PROPAGATE(trace, maage_renderer_draw_frame(renderer, meshes, count, view_projection, NULL, trace), MAAGE_CONTEXT_RENDER, MAAGE_OPERATION_PRESENT);
}
maage_status maage_renderer_draw_mesh(maage_renderer *renderer, const maage_render_mesh *mesh, const float view_projection[16], maage_status_trace *trace) {
    return MAAGE_STATUS_PROPAGATE(trace, maage_renderer_draw_meshes(renderer, &mesh, 1, view_projection, trace), MAAGE_CONTEXT_RENDER, MAAGE_OPERATION_PRESENT);
}
maage_status maage_renderer_resize(maage_renderer *renderer) {
    if (!renderer) return maage_status_make(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_CONTEXT_RENDER, MAAGE_OPERATION_RESIZE, 0);
    if (renderer->refresh) return maage_status_make(MAAGE_STATUS_NOOP, 0, MAAGE_OPERATION_RESIZE, 0);
    renderer->refresh = 1;
    return maage_status_make(MAAGE_STATUS_SUCCESS, 0, MAAGE_OPERATION_RESIZE, 0);
}
