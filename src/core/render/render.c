#include "render_internal.h"
#include "cmakecfg.h"
#include "maage/log.h"
#include <stdlib.h>
#include <string.h>
#include <float.h>
#include <limits.h>

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
        renderer->device_name[0] = 0;
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
    while (renderer->meshes) maage_render_mesh_destroy(renderer->meshes);
    while (renderer->textures) maage_render_texture_destroy(renderer->textures);
    renderer->api->destroy(renderer);
    maage_window_release_surface(renderer->window);
    free(renderer);
}
maage_render_backend maage_renderer_get_backend(const maage_renderer *renderer)
{
    return renderer ? renderer->api->kind : MAAGE_RENDER_AUTO;
}
const char *maage_renderer_device_name(const maage_renderer *renderer)
{
    return renderer && renderer->device_name[0] ? renderer->device_name : NULL;
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
maage_render_result maage_renderer_prepare_frame(maage_renderer *renderer)
{
    maage_window_state state;
    maage_render_result result;
    if (!renderer) return MAAGE_RENDER_INVALID_ARGUMENT;
    if (renderer->failure != MAAGE_RENDER_OK) return renderer->failure;
    if (!renderer->api->prepare_frame) return MAAGE_RENDER_UNSUPPORTED;
    if (maage_window_get_state(renderer->window, &state) != MAAGE_WINDOW_OK)
        return MAAGE_RENDER_WINDOW_ERROR;
    if (state.visible) return MAAGE_RENDER_INVALID_ARGUMENT;
    if (state.framebuffer_width <= 0 || state.framebuffer_height <= 0) return MAAGE_RENDER_SKIPPED;
    result = renderer->api->prepare_frame(renderer, &state);
    if (result != MAAGE_RENDER_OK && result != MAAGE_RENDER_SKIPPED) renderer->failure = result;
    return result;
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
static void maage_render_texture_release(maage_render_texture *texture)
{
    if (--texture->references != 0) return;
    texture->renderer->api->destroy_texture(texture);
    if (texture->previous) texture->previous->next = texture->next;
    else texture->renderer->textures = texture->next;
    if (texture->next) texture->next->previous = texture->previous;
    free(texture);
}
maage_render_result maage_renderer_create_texture(maage_renderer *renderer,
    const maage_pixel_buffer *atlas, maage_render_texture **output)
{
    maage_render_texture *texture;
    maage_render_result result;
    if (!output) return MAAGE_RENDER_INVALID_ARGUMENT;
    *output = NULL;
    if (!renderer || !atlas || !atlas->pixels || atlas->width <= 0 ||
        atlas->height <= 0 || (size_t)atlas->width > (size_t)-1 / 4 ||
        atlas->stride < (size_t)atlas->width * 4 ||
        atlas->stride > (size_t)-1 / (size_t)atlas->height)
        return MAAGE_RENDER_INVALID_ARGUMENT;
    if (renderer->failure != MAAGE_RENDER_OK) return renderer->failure;
    if (!renderer->api->create_texture || !renderer->api->destroy_texture)
        return MAAGE_RENDER_UNSUPPORTED;
    texture = (maage_render_texture *)calloc(1, sizeof(*texture));
    if (!texture) return MAAGE_RENDER_OUT_OF_MEMORY;
    texture->renderer = renderer;
    texture->references = 1;
    texture->owned = 1;
    result = renderer->api->create_texture(renderer, texture, atlas);
    if (result != MAAGE_RENDER_OK) {
        renderer->api->destroy_texture(texture);
        free(texture);
        return result;
    }
    texture->next = renderer->textures;
    if (texture->next) texture->next->previous = texture;
    renderer->textures = texture;
    *output = texture;
    return MAAGE_RENDER_OK;
}
void maage_render_texture_destroy(maage_render_texture *texture)
{
    if (!texture || !texture->owned) return;
    texture->owned = 0;
    maage_render_texture_release(texture);
}
maage_render_result maage_renderer_upload_geometry(maage_renderer *renderer,
    const maage_mesh_data *data, maage_render_texture *texture,
    maage_render_mesh **output)
{
    maage_render_mesh *mesh;
    maage_render_result result;
    size_t index;
    if (!output) return MAAGE_RENDER_INVALID_ARGUMENT;
    *output = NULL;
    if (!renderer || !data || !texture || texture->renderer != renderer ||
        !texture->owned || !data->vertices || !data->indices ||
        !data->vertex_count || !data->index_count || data->index_count % 3 ||
        data->vertex_count > UINT_MAX ||
        data->vertex_count > (size_t)-1 / sizeof(*data->vertices) ||
        data->index_count > (size_t)-1 / sizeof(*data->indices))
        return MAAGE_RENDER_INVALID_ARGUMENT;
    if (renderer->failure != MAAGE_RENDER_OK) return renderer->failure;
    if (!renderer->api->upload_geometry) return MAAGE_RENDER_UNSUPPORTED;
    for (index = 0; index < data->index_count; ++index)
        if (data->indices[index] >= data->vertex_count) return MAAGE_RENDER_INVALID_ARGUMENT;
    mesh = (maage_render_mesh *)calloc(1, sizeof(*mesh));
    if (!mesh) return MAAGE_RENDER_OUT_OF_MEMORY;
    mesh->renderer = renderer;
    mesh->texture = texture;
    result = renderer->api->upload_geometry(renderer, mesh, data);
    if (result != MAAGE_RENDER_OK) {
        renderer->api->destroy_mesh(mesh);
        free(mesh);
        return result;
    }
    ++texture->references;
    mesh->next = renderer->meshes;
    if (mesh->next) mesh->next->previous = mesh;
    renderer->meshes = mesh;
    *output = mesh;
    return MAAGE_RENDER_OK;
}
maage_render_result maage_renderer_upload_mesh(maage_renderer *renderer,
    const maage_mesh_data *data, const maage_pixel_buffer *atlas,
    maage_render_mesh **output)
{
    maage_render_texture *texture;
    maage_render_result result;
    if (!output) return MAAGE_RENDER_INVALID_ARGUMENT;
    *output = NULL;
    result = maage_renderer_create_texture(renderer, atlas, &texture);
    if (result != MAAGE_RENDER_OK) return result;
    result = maage_renderer_upload_geometry(renderer, data, texture, output);
    maage_render_texture_destroy(texture);
    return result;
}
void maage_render_mesh_destroy(maage_render_mesh *mesh)
{
    if (!mesh) return;
    mesh->renderer->api->destroy_mesh(mesh);
    if (mesh->previous) mesh->previous->next = mesh->next;
    else mesh->renderer->meshes = mesh->next;
    if (mesh->next) mesh->next->previous = mesh->previous;
    maage_render_texture_release(mesh->texture);
    free(mesh);
}
maage_render_result maage_renderer_draw_frame(maage_renderer *renderer,
    const maage_render_mesh *const *meshes, size_t count,
    const float view_projection[16], const maage_render_ui_data *ui)
{
    maage_window_state state;
    maage_render_result result;
    size_t mesh_index;
    int index;
    if (!renderer || (count && !meshes) || !view_projection)
        return MAAGE_RENDER_INVALID_ARGUMENT;
    for (mesh_index = 0; mesh_index < count; ++mesh_index)
        if (!meshes[mesh_index] || meshes[mesh_index]->renderer != renderer)
            return MAAGE_RENDER_INVALID_ARGUMENT;
    for (index = 0; index < 16; ++index)
        if (!(view_projection[index] >= -FLT_MAX && view_projection[index] <= FLT_MAX))
            return MAAGE_RENDER_INVALID_ARGUMENT;
    if (ui && ui->index_count) {
        if (!ui->texture || ui->texture->renderer != renderer || !ui->texture->owned ||
            !ui->vertices || !ui->indices || !ui->vertex_count || ui->index_count % 3 ||
            ui->vertex_count > UINT_MAX ||
            ui->vertex_count > (size_t)-1 / sizeof(*ui->vertices) ||
            ui->index_count > (size_t)-1 / sizeof(*ui->indices) ||
            !(ui->logical_width > 0 && ui->logical_width <= FLT_MAX) ||
            !(ui->logical_height > 0 && ui->logical_height <= FLT_MAX))
            return MAAGE_RENDER_INVALID_ARGUMENT;
        for (mesh_index = 0; mesh_index < ui->index_count; ++mesh_index)
            if (ui->indices[mesh_index] >= ui->vertex_count)
                return MAAGE_RENDER_INVALID_ARGUMENT;
        if (!(renderer->capabilities.features & MAAGE_RENDER_CAP_UI) ||
            !renderer->api->draw_frame) return MAAGE_RENDER_UNSUPPORTED;
    }
    if (renderer->failure != MAAGE_RENDER_OK) return renderer->failure;
    if (!renderer->api->draw_frame && !renderer->api->draw_meshes)
        return MAAGE_RENDER_UNSUPPORTED;
    if (maage_window_get_state(renderer->window, &state) != MAAGE_WINDOW_OK)
        return MAAGE_RENDER_WINDOW_ERROR;
    if (!state.visible || state.framebuffer_width <= 0 || state.framebuffer_height <= 0)
        return MAAGE_RENDER_SKIPPED;
    result = renderer->api->draw_frame ?
        renderer->api->draw_frame(renderer, meshes, count, &state, view_projection, ui) :
        renderer->api->draw_meshes(renderer, meshes, count, &state, view_projection);
    if (result != MAAGE_RENDER_OK && result != MAAGE_RENDER_SKIPPED) renderer->failure = result;
    return result;
}
maage_render_result maage_renderer_draw_meshes(maage_renderer *renderer,
    const maage_render_mesh *const *meshes, size_t count,
    const float view_projection[16])
{
    return maage_renderer_draw_frame(renderer, meshes, count, view_projection, NULL);
}
maage_render_result maage_renderer_draw_mesh(maage_renderer *renderer,
    const maage_render_mesh *mesh, const float view_projection[16])
{
    return maage_renderer_draw_meshes(renderer, &mesh, 1, view_projection);
}
void maage_renderer_resize(maage_renderer *renderer)
{
    if (renderer) renderer->refresh = 1;
}
