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
    maage_render_ui_vertex sky_vertices[8], weather_vertices[MAAGE_ENVIRONMENT_QUADS * 4];
    unsigned int sky_indices[12], weather_indices[MAAGE_ENVIRONMENT_QUADS * 6];
    maage_render_ui_data sky, weather;
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
maage_client_environment *maage_client_environment_create(maage_renderer *renderer,
    const char *application_data)
{
    const maage_client_environment_resource *resource;
    maage_client_environment *view;
    maage_image atlas, source;
    maage_pixel_buffer pixels;
    char name[128], path[MAAGE_PATH_CAPACITY];
    int i, y, ok = 0;
    view = (maage_client_environment *)calloc(1, sizeof(*view));
    if (!view) return NULL;
    memset(&atlas, 0, sizeof(atlas)); memset(&source, 0, sizeof(source));
    atlas.width = atlas.height = 256; atlas.stride = 1024;
    atlas.pixels = (unsigned char *)calloc(256, atlas.stride);
    if (!atlas.pixels) goto done;
    for (i = 0; i < 4; ++i) {
        resource = &maage_client_environment_resources[i];
        strcpy(name, "minecraft/textures/environment/"); strcat(name, resource->name); strcat(name, ".png");
        if (!maage_minecraft_asset_path(application_data, name, path, sizeof(path))) {
            maage_log_error("maage.client.environment", "Cannot resolve environmental resource path: %s.", name); goto done;
        }
        if (!maage_image_load_png(path, &source)) {
            maage_log_error("maage.client.environment", "Missing or unreadable environmental PNG: %s.", path); goto done;
        }
        if (source.width != resource->width || source.height != resource->height) {
            maage_log_error("maage.client.environment", "Unexpected dimensions for %s: %dx%d; expected %dx%d.",
                name, source.width, source.height, resource->width, resource->height); goto done;
        }
        for (y = 0; y < source.height; ++y)
            memcpy(atlas.pixels + (size_t)(resource->y + y) * atlas.stride + (size_t)resource->x * 4,
                source.pixels + (size_t)y * source.stride, (size_t)source.width * 4);
        maage_image_destroy(&source);
    }
    memset(atlas.pixels + 255 * atlas.stride + 127 * 4, 255, 4);
    pixels.width = atlas.width; pixels.height = atlas.height; pixels.stride = atlas.stride; pixels.pixels = atlas.pixels;
    if (maage_renderer_create_texture(renderer, &pixels, &view->texture) != MAAGE_RENDER_OK) goto done;
    view->renderer = renderer;
    view->sky.texture = view->weather.texture = view->texture;
    view->sky.vertices = view->sky_vertices; view->sky.indices = view->sky_indices;
    view->weather.vertices = view->weather_vertices; view->weather.indices = view->weather_indices;
    ok = 1;
done:
    maage_image_destroy(&source); maage_image_destroy(&atlas);
    if (!ok) { maage_client_environment_destroy(view); return NULL; }
    return view;
}
void maage_client_environment_destroy(maage_client_environment *view)
{
    if (!view) return;
    if (view->renderer) maage_renderer_set_scene(view->renderer, 1, NULL, NULL);
    maage_render_texture_destroy(view->texture); free(view);
}
static void maage_client_environment_body(maage_client_environment *view, const double direction[3],
    const double forward[3], const double right[3], const double up[3], float focal,
    float width, float height, int moon, unsigned int phase, float alpha)
{
    const maage_client_environment_resource *resource = &maage_client_environment_resources[moon ? 1 : 0];
    double depth = direction[0] * forward[0] + direction[1] * forward[1] + direction[2] * forward[2];
    float x, y, size, u, v, uv_width, uv_height;
    if (depth <= 0.02) return;
    x = width * 0.5f + focal * (float)(direction[0] * right[0] + direction[1] * right[1] + direction[2] * right[2]) / (float)depth;
    y = height * 0.5f - focal * (float)(direction[0] * up[0] + direction[1] * up[1] + direction[2] * up[2]) / (float)depth;
    size = focal * (moon ? 0.16f : 0.19f) / (float)depth;
    if (x + size < 0 || x - size > width || y + size < 0 || y - size > height) return;
    uv_width = (float)resource->width / (moon ? 4 : 1) / 256;
    uv_height = (float)resource->height / (moon ? 2 : 1) / 256;
    u = (float)resource->x / 256 + (moon ? (float)(phase % 4) * uv_width : 0);
    v = (float)resource->y / 256 + (moon ? (float)(phase / 4) * uv_height : 0);
    maage_client_environment_quad(&view->sky, x - size * 0.5f, y - size * 0.5f, size, size,
        u, v, uv_width, uv_height, alpha);
}
maage_render_result maage_client_environment_update(maage_client_environment *view,
    maage_renderer *renderer, const maage_environment_state *state, const maage_world *world,
    const maage_player *player, double yaw, double pitch, float width, float height,
    double vertical_fov)
{
    const maage_client_environment_resource *resource;
    maage_world_observation observation;
    double forward[3], right[3], up[3], sun[3], angle, phase, sunlight, rain;
    float focal, x, y, size, alpha;
    unsigned int i, hash;
    int cold = 0, precipitation = 0;
    maage_render_result result;
    if (!view || !renderer || !state || !world || !player || !(width > 0 && height > 0) ||
        !(vertical_fov > 0 && vertical_fov < 3.14)) return MAAGE_RENDER_INVALID_ARGUMENT;
    view->sky.vertex_count = view->sky.index_count = view->weather.vertex_count = view->weather.index_count = 0;
    view->sky.logical_width = view->weather.logical_width = width;
    view->sky.logical_height = view->weather.logical_height = height;
    sunlight = maage_environment_sunlight(state); rain = state->rain_intensity;
    result = maage_renderer_set_clear_color(renderer, (float)(0.02 + 0.51 * sunlight * (1 - rain * 0.4)),
        (float)(0.025 + 0.735 * sunlight * (1 - rain * 0.4)), (float)(0.06 + 0.94 * sunlight * (1 - rain * 0.3)), 1);
    if (result != MAAGE_RENDER_OK) return result;
    forward[0] = sin(yaw) * cos(pitch); forward[1] = -sin(pitch); forward[2] = -cos(yaw) * cos(pitch);
    right[0] = cos(yaw); right[1] = 0; right[2] = sin(yaw);
    up[0] = sin(yaw) * sin(pitch); up[1] = cos(pitch); up[2] = -cos(yaw) * sin(pitch);
    angle = (double)state->day_tick / MAAGE_WORLD_DAY_TICKS * 6.283185307179586;
    sun[0] = cos(angle); sun[1] = sin(angle); sun[2] = 0;
    focal = height * 0.5f / (float)tan(vertical_fov * 0.5);
    maage_client_environment_body(view, sun, forward, right, up, focal, width, height, 0, 0, (float)(1 - rain));
    for (i = 0; i < 3; ++i) sun[i] = -sun[i];
    maage_client_environment_body(view, sun, forward, right, up, focal, width, height, 1,
        (unsigned int)(state->day_low & 7), (float)(1 - rain));
    if (maage_world_observe(world, (int)floor(player->position[0]),
        (int)floor(player->position[1] + MAAGE_PLAYER_EYE_HEIGHT), (int)floor(player->position[2]), &observation)) {
        cold = observation.temperature < 0.15; precipitation = observation.sky_exposed && observation.rainfall > 0;
    }
    phase = (double)(state->ticks_low % 120000UL) * 0.05;
    resource = &maage_client_environment_resources[cold ? 3 : 2];
    if (precipitation && rain > 0) for (i = 0; i < 48; ++i) {
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
