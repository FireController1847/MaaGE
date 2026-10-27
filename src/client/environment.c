#include "environment.h"
#include "maage/image.h"
#include "maage/minecraft_assets.h"
#include "maage/storage.h"
#include "maage/log.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

#define MAAGE_ENVIRONMENT_QUADS 64
typedef struct maage_client_environment_resource {
    const char *name;
    int x, y, width, height;
} maage_client_environment_resource;
static const maage_client_environment_resource maage_client_environment_resources[4] = {
    {"sun", 0, 64, 32, 32},
    {"moon_phases", 0, 0, 128, 64},
    {"rain", 128, 0, 64, 256},
    {"snow", 192, 0, 64, 256}
};
struct maage_client_environment {
    maage_renderer *renderer;
    maage_render_texture *texture;
    maage_render_ui_vertex sky_vertices[12], weather_vertices[MAAGE_ENVIRONMENT_QUADS * 4];
    float sky_depth[12];
    unsigned int sky_indices[24], weather_indices[MAAGE_ENVIRONMENT_QUADS * 6];
    maage_render_ui_data sky, weather;
    maage_status load_status;
    unsigned int failed_resources;
    unsigned char available[4];
};
static void maage_client_environment_quad(maage_render_ui_data *batch, float x, float y,
    float width, float height, float u, float v, float uv_width, float uv_height, float alpha)
{
    static const unsigned int order[6] = {0, 1, 2, 0, 2, 3};
    maage_render_ui_vertex *vertices = (maage_render_ui_vertex *)batch->vertices;
    unsigned int *indices = (unsigned int *)batch->indices;
    size_t first = batch->vertex_count, corner;
    for (corner = 0; corner < 4; ++corner) {
        vertices[first + corner].position[0] = x + (corner == 1 || corner == 2 ? width : 0);
        vertices[first + corner].position[1] = y + (corner >= 2 ? height : 0);
        vertices[first + corner].uv[0] = u + (corner == 1 || corner == 2 ? uv_width : 0);
        vertices[first + corner].uv[1] = v + (corner >= 2 ? uv_height : 0);
        vertices[first + corner].color[0] = vertices[first + corner].color[1] = vertices[first + corner].color[2] = 1;
        vertices[first + corner].color[3] = alpha;
    }
    for (corner = 0; corner < 6; ++corner) indices[batch->index_count + corner] = (unsigned int)first + order[corner];
    batch->vertex_count += 4; batch->index_count += 6;
}
maage_status maage_client_environment_create(maage_renderer *renderer,
    const char *application_data, maage_client_environment **output, maage_status_trace *trace)
{
    const maage_client_environment_resource *resource;
    maage_client_environment *view;
    maage_image atlas, source;
    maage_pixel_buffer pixels;
    char name[128], path[MAAGE_PATH_CAPACITY];
    maage_status status;
    maage_status uploaded, result;
    maage_status_trace source_trace, recovered_trace;
    int i, y, ok = 0;
    if (output) *output = NULL;
    if (!output || !renderer) return maage_status_make(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_CONTEXT_RESOURCE, MAAGE_OPERATION_INITIALIZE, !renderer ? 0 : 2);
    result = maage_status_make(MAAGE_STATUS_ALLOCATION_FAILED, MAAGE_CONTEXT_RESOURCE, MAAGE_OPERATION_INITIALIZE, 0);
    view = (maage_client_environment *)calloc(1, sizeof(*view));
    if (!view) return MAAGE_STATUS_PROPAGATE(trace, result, MAAGE_CONTEXT_CLIENT, MAAGE_OPERATION_INITIALIZE);
    view->load_status = maage_status_make(MAAGE_STATUS_SUCCESS, 0, MAAGE_OPERATION_LOAD_TEXTURE, 0);
    memset(&atlas, 0, sizeof(atlas)); memset(&source, 0, sizeof(source));
    atlas.width = atlas.height = 256; atlas.stride = 1024;
    atlas.pixels = (unsigned char *)calloc(256, atlas.stride);
    if (!atlas.pixels) goto done;
    for (i = 0; i < 4; ++i) {
        maage_status_trace_init(&source_trace);
        MAAGE_STATUS_TRACE(&source_trace, 0, MAAGE_CONTEXT_CLIENT, MAAGE_OPERATION_INITIALIZE);
        MAAGE_STATUS_TRACE(&source_trace, 0, MAAGE_CONTEXT_RESOURCE, MAAGE_OPERATION_ATLAS);
        resource = &maage_client_environment_resources[i];
        strcpy(name, "minecraft/textures/environment/"); strcat(name, resource->name); strcat(name, ".png");
        status = maage_minecraft_asset_path(application_data, name, path, sizeof(path), &source_trace);
        if (!maage_status_failed(status)) status = maage_image_load_png(path, &source, &source_trace);
        if (!maage_status_failed(status) && (source.width != resource->width || source.height != resource->height))
            status = maage_status_make(MAAGE_STATUS_MALFORMED_INPUT, MAAGE_CONTEXT_RESOURCE, MAAGE_OPERATION_LOAD_TEXTURE, 0);
        if (maage_status_failed(status)) {
            if (!view->failed_resources) view->load_status = status;
            ++view->failed_resources;
            MAAGE_STATUS_TRACE(&source_trace, status, MAAGE_CONTEXT_RESOURCE, MAAGE_OPERATION_LOAD_TEXTURE);
            maage_status_trace_recover(&source_trace, maage_status_make(MAAGE_STATUS_NOOP, 0, MAAGE_OPERATION_PRESENT, 0), MAAGE_CONTEXT_RESOURCE, MAAGE_OPERATION_PRESENT, __FILE__, __LINE__);
            if (view->failed_resources == 1) recovered_trace = source_trace;
            maage_log_trace_status(MAAGE_LOG_WARNING, "maage.client.environment", status, &source_trace, "%s; omitting this optional visual.", name);
            maage_image_destroy(&source);
            continue;
        }
        view->available[i] = 1;
        for (y = 0; y < source.height; ++y)
            memcpy(atlas.pixels + (size_t)(resource->y + y) * atlas.stride + (size_t)resource->x * 4,
                source.pixels + (size_t)y * source.stride, (size_t)source.width * 4);
        maage_image_destroy(&source);
    }
    memset(atlas.pixels + 255 * atlas.stride + 127 * 4, 255, 4);
    pixels.width = atlas.width; pixels.height = atlas.height; pixels.stride = atlas.stride; pixels.pixels = atlas.pixels;
    uploaded = maage_renderer_create_texture(renderer, &pixels, &view->texture, trace);
    result = uploaded;
    if (maage_status_code(uploaded) != MAAGE_STATUS_SUCCESS) goto done;
    view->renderer = renderer;
    view->sky.texture = view->weather.texture = view->texture;
    view->sky.blend = MAAGE_RENDER_BLEND_ADDITIVE;
    view->sky.vertices = view->sky_vertices; view->sky.indices = view->sky_indices;
    view->sky.projective_depth = view->sky_depth;
    view->weather.vertices = view->weather_vertices; view->weather.indices = view->weather_indices;
    ok = 1;
done:
    maage_image_destroy(&source); maage_image_destroy(&atlas);
    if (!ok) {
        maage_client_environment_destroy(view);
        return MAAGE_STATUS_PROPAGATE(trace, result, MAAGE_CONTEXT_RESOURCE, MAAGE_OPERATION_INITIALIZE);
    }
    *output = view;
    if (trace && view->failed_resources) *trace = recovered_trace;
    return MAAGE_STATUS_PROPAGATE(trace, maage_status_make(view->failed_resources ? MAAGE_STATUS_PARTIAL : MAAGE_STATUS_SUCCESS,
        MAAGE_CONTEXT_RESOURCE, MAAGE_OPERATION_INITIALIZE, 0), MAAGE_CONTEXT_CLIENT, MAAGE_OPERATION_INITIALIZE);
}
maage_status maage_client_environment_resource_status(const maage_client_environment *view,
    unsigned int *failed_resources)
{
    if (failed_resources) *failed_resources = view ? view->failed_resources : 0;
    return view ? view->load_status : maage_status_make(MAAGE_STATUS_INVALID_ARGUMENT,
        MAAGE_CONTEXT_RESOURCE, MAAGE_OPERATION_INITIALIZE, 0);
}
maage_status maage_client_environment_destroy(maage_client_environment *view)
{
    if (!view) return maage_status_make(MAAGE_STATUS_NOOP, 0, MAAGE_OPERATION_DESTROY, 0);
    if (view->renderer) maage_renderer_set_scene(view->renderer, 1, NULL, NULL);
    maage_render_texture_destroy(view->texture); free(view);
    return maage_status_make(MAAGE_STATUS_SUCCESS, 0, MAAGE_OPERATION_DESTROY, 0);
}
static void maage_client_environment_body(maage_client_environment *view, const double direction[3],
    const float view_projection[16],
    float width, float height, int moon, unsigned int phase, float alpha)
{
    const maage_client_environment_resource *resource = &maage_client_environment_resources[moon ? 1 : 0];
    /* Project the orbit's actual plane, including its orientation. Clip before
     * division so a sprite crossing the camera plane stays well-defined. */
    double points[4][5], clipped[6][5], position[3], tangent[3];
    double half = moon ? 20.0 : 30.0, fraction, horizontal, vertical;
    double u, v, uv_width, uv_height;
    int corner, previous, count = 0, component;
    size_t first = view->sky.vertex_count;
    maage_render_ui_vertex *vertex;
    tangent[0] = -direction[1]; tangent[1] = direction[0]; tangent[2] = 0;
    uv_width = (double)resource->width / (moon ? 4 : 1) / 256;
    uv_height = (double)resource->height / (moon ? 2 : 1) / 256;
    u = (double)resource->x / 256 + (moon ? (phase % 4) * uv_width : 0);
    v = (double)resource->y / 256 + (moon ? (phase / 4) * uv_height : 0);
    for (corner = 0; corner < 4; ++corner) {
        horizontal = corner == 1 || corner == 2 ? 1 : -1;
        vertical = corner >= 2 ? 1 : -1;
        for (component = 0; component < 3; ++component)
            position[component] = direction[component] * 100 + tangent[component] * vertical * half;
        position[2] += horizontal * half;
        /* Homogeneous directions (w=0) share the world's current projection
         * and camera rotation, including bob, without eye-position parallax. */
        points[corner][0] = points[corner][1] = points[corner][2] = 0;
        for (component = 0; component < 3; ++component) {
            points[corner][0] += view_projection[component * 4] * position[component];
            points[corner][1] += view_projection[component * 4 + 1] * position[component];
            points[corner][2] += view_projection[component * 4 + 3] * position[component];
        }
        points[corner][3] = u + ((moon ? -horizontal : horizontal) + 1) * 0.5 * uv_width;
        points[corner][4] = v + ((moon ? -vertical : vertical) + 1) * 0.5 * uv_height;
    }
    for (corner = 0, previous = 3; corner < 4; previous = corner++) {
        if ((points[corner][2] >= 0.1) != (points[previous][2] >= 0.1)) {
            fraction = (0.1 - points[previous][2]) / (points[corner][2] - points[previous][2]);
            for (component = 0; component < 5; ++component)
                clipped[count][component] = points[previous][component] +
                    fraction * (points[corner][component] - points[previous][component]);
            ++count;
        }
        if (points[corner][2] >= 0.1) {
            memcpy(clipped[count], points[corner], sizeof(points[corner])); ++count;
        }
    }
    if (count < 3) return;
    for (corner = 0; corner < count; ++corner) {
        view->sky_depth[view->sky.vertex_count] = (float)clipped[corner][2];
        vertex = &view->sky_vertices[view->sky.vertex_count++];
        vertex->position[0] = width * 0.5f * (1 + (float)(clipped[corner][0] / clipped[corner][2]));
        vertex->position[1] = height * 0.5f * (1 - (float)(clipped[corner][1] / clipped[corner][2]));
        vertex->uv[0] = (float)clipped[corner][3]; vertex->uv[1] = (float)clipped[corner][4];
        vertex->color[0] = vertex->color[1] = vertex->color[2] = 1;
        vertex->color[3] = alpha;
    }
    for (corner = 1; corner < count - 1; ++corner) {
        view->sky_indices[view->sky.index_count++] = (unsigned int)first;
        view->sky_indices[view->sky.index_count++] = (unsigned int)first + corner;
        view->sky_indices[view->sky.index_count++] = (unsigned int)first + corner + 1;
    }
}
static void maage_client_sky_color(double temperature, double brightness,
    double rain, double thunder, float color[3])
{
    double hue, saturation, part, gray, weight;
    int sector;
    temperature /= 3;
    if (temperature < -1) temperature = -1;
    if (temperature > 1) temperature = 1;
    hue = (0.6222222222 - temperature * 0.05) * 6;
    saturation = 0.5 + temperature * 0.1;
    sector = (int)hue; part = hue - sector;
    color[0] = (float)(sector == 3 ? 1 - saturation : 1 - saturation * (1 - part));
    color[1] = (float)(sector == 3 ? 1 - saturation * part : 1 - saturation);
    color[2] = 1;
    color[0] *= (float)brightness; color[1] *= (float)brightness; color[2] *= (float)brightness;
    gray = (color[0] * 0.3 + color[1] * 0.59 + color[2] * 0.11) * 0.6;
    weight = rain * 0.75;
    color[0] = (float)(color[0] * (1 - weight) + gray * weight);
    color[1] = (float)(color[1] * (1 - weight) + gray * weight);
    color[2] = (float)(color[2] * (1 - weight) + gray * weight);
    gray = (color[0] * 0.3 + color[1] * 0.59 + color[2] * 0.11) * 0.2;
    weight = thunder * rain * 0.75;
    color[0] = (float)(color[0] * (1 - weight) + gray * weight);
    color[1] = (float)(color[1] * (1 - weight) + gray * weight);
    color[2] = (float)(color[2] * (1 - weight) + gray * weight);
}
maage_status maage_client_environment_update(maage_client_environment *view,
    maage_renderer *renderer, const maage_session_snapshot *snapshot,
    float width, float height, const float view_projection[16])
{
    const maage_client_environment_resource *resource;
    const maage_environment_state *state;
    double sun[3], angle, phase, sunlight, rain, sky_brightness;
    float x, y, size, alpha;
    float sky_color[3];
    unsigned int i, hash;
    int cold = 0, precipitation = 0;
    maage_status result;
    if (!view || !renderer || !snapshot || !view_projection ||
        snapshot->protocol_version != MAAGE_SESSION_PROTOCOL_VERSION) return MAAGE_STATUS_VALUE(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_CONTEXT_CLIENT, MAAGE_OPERATION_UPDATE, 0);
    if (width <= 0 || height <= 0) return maage_status_make(MAAGE_STATUS_NOOP, 0, MAAGE_OPERATION_UPDATE, 0);
    if (!(width > 0 && height > 0)) return maage_status_make(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_CONTEXT_CLIENT, MAAGE_OPERATION_UPDATE, 0);
    state = &snapshot->environment;
    view->sky.vertex_count = view->sky.index_count = view->weather.vertex_count = view->weather.index_count = 0;
    view->sky.logical_width = view->weather.logical_width = width;
    view->sky.logical_height = view->weather.logical_height = height;
    sunlight = maage_environment_sunlight(state); rain = state->rain_intensity;
    result = maage_renderer_set_animation_tick(renderer, (float)(state->ticks_low % 3648UL));
    if (maage_status_failed(result)) return result;
    angle = maage_environment_celestial_angle(state) * 6.283185307179586;
    sky_brightness = cos(angle) * 2 + 0.5;
    if (sky_brightness < 0) sky_brightness = 0;
    if (sky_brightness > 1) sky_brightness = 1;
    maage_client_sky_color(snapshot->has_observation ? snapshot->observation.temperature : 0.8,
        sky_brightness, rain, state->thunder_intensity, sky_color);
    result = maage_renderer_set_clear_color(renderer, sky_color[0], sky_color[1], sky_color[2], 1);
    if (maage_status_code(result) != MAAGE_STATUS_SUCCESS) return result;
    sun[0] = -sin(angle); sun[1] = cos(angle); sun[2] = 0;
    if (view->available[0]) maage_client_environment_body(view, sun, view_projection, width, height, 0, 0, (float)(1 - rain));
    for (i = 0; i < 3; ++i) sun[i] = -sun[i];
    if (view->available[1]) maage_client_environment_body(view, sun, view_projection, width, height, 1,
        (unsigned int)(state->day_low & 7), (float)(1 - rain));
    /* Server sampled the player's sky/climate. No client World access. */
    if (snapshot->has_observation) {
        cold = snapshot->observation.temperature < 0.15;
        precipitation = snapshot->observation.sky_exposed && snapshot->observation.rainfall > 0;
    }
    phase = (double)(state->ticks_low % 120000UL) * 0.05;
    resource = &maage_client_environment_resources[cold ? 3 : 2];
    if (precipitation && rain > 0 && view->available[cold ? 3 : 2]) for (i = 0; i < 48; ++i) {
        hash = (i * 1664525U + 1013904223U) ^ (i * 7919U);
        x = (float)(hash % 1009U) / 1009 * width;
        y = (float)fmod((double)(hash % 997U) / 997 * (height + 24) + phase * (cold ? 12 : 220), height + 24) - 24;
        size = cold ? 3 : 2; alpha = (float)rain * (cold ? 0.7f : 0.42f);
        if (cold) x += (float)sin(phase + i) * 4;
        maage_client_environment_quad(&view->weather, x, y, size, cold ? size : 18,
            (float)resource->x / 256, (float)resource->y / 256,
            (float)resource->width / 256, (float)resource->height / 256, alpha);
    }
    if (precipitation && state->thunder_intensity > 0.5 && state->ticks_low % 173UL < 2)
        maage_client_environment_quad(&view->weather, 0, 0, width, height,
            127.5f / 256, 255.5f / 256, 0, 0, 0.25f);
    return maage_renderer_set_scene(renderer, (float)sunlight, &view->sky, &view->weather);
}
