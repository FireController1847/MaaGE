#include "environment.h"
#include "maage/image.h"
#include "maage/minecraft_assets.h"
#include "maage/storage.h"
#include "maage/log.h"
#include "maage/time.h"
#include <math.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>

#define MAAGE_ENVIRONMENT_PARTICLES 256U
#define MAAGE_ENVIRONMENT_QUADS (MAAGE_ENVIRONMENT_PARTICLES + 64U)
typedef struct maage_client_environment_resource {
    const char *name;
    int x, y, width, height;
} maage_client_environment_resource;
static const maage_client_environment_resource maage_client_environment_resources[6] = {
    {"sun", 0, 64, 32, 32},
    {"moon_phases", 0, 0, 128, 64},
    {"rain", 128, 0, 64, 256},
    {"snow", 192, 0, 64, 256},
    {"underwater", 32, 64, 16, 16},
    {"particles", 0, 96, 128, 128}};
struct maage_client_environment {
    maage_renderer *renderer;
    maage_render_texture *texture;
    maage_render_ui_vertex sky_vertices[12], weather_vertices[MAAGE_ENVIRONMENT_QUADS * 4];
    float sky_depth[12];
    unsigned int sky_indices[24], weather_indices[MAAGE_ENVIRONMENT_QUADS * 6];
    maage_render_ui_data sky, weather;
    maage_status load_status;
    unsigned int failed_resources;
    unsigned char available[6];
    struct {
        double position[3], age, lifetime;
        int x, y, z, smoke, active;
    } particles[MAAGE_ENVIRONMENT_PARTICLES];
    unsigned int particle_cursor;
    unsigned long particle_random, particle_tick_low, particle_tick_high, particle_session, particle_world;
    double particle_time;
    float water_brightness;
    int atlas_height, *fluid_overlays;
    size_t fluid_count;
};
static void maage_client_environment_quad(maage_render_ui_data *batch, float x, float y, float width, float height, float u, float v, float uv_width, float uv_height, float alpha) {
    static const unsigned int order[6] = {0, 1, 2, 0, 2, 3};
    maage_render_ui_vertex *vertices = (maage_render_ui_vertex *) batch->vertices;
    unsigned int *indices = (unsigned int *) batch->indices;
    size_t first = batch->vertex_count, corner;
    for (corner = 0; corner < 4; ++corner) {
        vertices[first + corner].position[0] = x + (corner == 1 || corner == 2 ? width : 0);
        vertices[first + corner].position[1] = y + (corner >= 2 ? height : 0);
        vertices[first + corner].uv[0] = u + (corner == 1 || corner == 2 ? uv_width : 0);
        vertices[first + corner].uv[1] = v + (corner >= 2 ? uv_height : 0);
        vertices[first + corner].color[0] = vertices[first + corner].color[1] = vertices[first + corner].color[2] = 1;
        vertices[first + corner].color[3] = alpha;
    }
    for (corner = 0; corner < 6; ++corner)
        indices[batch->index_count + corner] = (unsigned int) first + order[corner];
    batch->vertex_count += 4;
    batch->index_count += 6;
}
maage_status maage_client_environment_create(maage_renderer *renderer, const char *application_data, const maage_client_resources *resources, maage_client_environment **output, maage_status_trace *trace) {
    const maage_client_environment_resource *resource;
    maage_client_environment *view;
    maage_image atlas, source;
    maage_pixel_buffer pixels;
    const maage_presentation_registry *presentation;
    const maage_presentation_bindings *bindings;
    const maage_fluid_registry *fluids;
    const maage_fluid_definition *fluid;
    const maage_texture_definition *overlay;
    const char *name;
    char path[MAAGE_PATH_CAPACITY];
    size_t fluid_index;
    int overlay_y, source_x, source_y;
    maage_status status;
    maage_status uploaded, result;
    maage_status_trace source_trace, recovered_trace;
    int i, y, ok = 0;
    if (output) *output = NULL;
    if (!output || !renderer || !resources || !resources->catalog) return maage_status_make(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_CONTEXT_RESOURCE, MAAGE_OPERATION_INITIALIZE, !renderer ? 0 : 2);
    result = maage_status_make(MAAGE_STATUS_ALLOCATION_FAILED, MAAGE_CONTEXT_RESOURCE, MAAGE_OPERATION_INITIALIZE, 0);
    view = (maage_client_environment *) calloc(1, sizeof(*view));
    if (!view) return MAAGE_STATUS_PROPAGATE(trace, result, MAAGE_CONTEXT_CLIENT, MAAGE_OPERATION_INITIALIZE);
    view->load_status = maage_status_make(MAAGE_STATUS_SUCCESS, 0, MAAGE_OPERATION_LOAD_TEXTURE, 0);
    memset(&atlas, 0, sizeof(atlas));
    memset(&source, 0, sizeof(source));
    presentation = maage_content_presentation(resources->catalog);
    bindings = maage_presentation_get_bindings(presentation);
    fluids = maage_content_fluids(resources->catalog);
    view->fluid_count = maage_fluid_count(fluids);
    if (!bindings || view->fluid_count > (size_t) (INT_MAX - 256) / 16 || view->fluid_count >= (size_t) - 1 / sizeof(*view->fluid_overlays)) {
        result = maage_status_make(MAAGE_STATUS_BUFFER_OVERFLOW, MAAGE_CONTEXT_RESOURCE, MAAGE_OPERATION_ATLAS, 0);
        goto done;
    }
    view->fluid_overlays = (int *) calloc(view->fluid_count + 1, sizeof(*view->fluid_overlays));
    if (!view->fluid_overlays) goto done;
    atlas.width = 256;
    atlas.height = view->atlas_height = 256 + (int) view->fluid_count * 16;
    atlas.stride = 1024;
    if ((size_t) atlas.height > (size_t) - 1 / atlas.stride) {
        result = maage_status_make(MAAGE_STATUS_BUFFER_OVERFLOW, MAAGE_CONTEXT_RESOURCE, MAAGE_OPERATION_ATLAS, 0);
        goto done;
    }
    atlas.pixels = (unsigned char *) calloc((size_t) atlas.height, atlas.stride);
    if (!atlas.pixels) goto done;
    view->water_brightness = 1;
    for (i = 0; i < 6; ++i) {
        if (i == 4) continue;
        maage_status_trace_init(&source_trace);
        MAAGE_STATUS_TRACE(&source_trace, 0, MAAGE_CONTEXT_CLIENT, MAAGE_OPERATION_INITIALIZE);
        MAAGE_STATUS_TRACE(&source_trace, 0, MAAGE_CONTEXT_RESOURCE, MAAGE_OPERATION_ATLAS);
        resource = &maage_client_environment_resources[i];
        name = maage_presentation_source(presentation, bindings->environment[i == 5 ? 4 : i]);
        if (!name) {
            result = maage_status_make(MAAGE_STATUS_NOT_FOUND, MAAGE_CONTEXT_RESOURCE, MAAGE_OPERATION_RESOLVE, 0);
            goto done;
        }
        status = maage_minecraft_asset_path(application_data, name, path, sizeof(path), &source_trace);
        if (!maage_status_failed(status)) status = maage_image_load_png(path, &source, &source_trace);
        if (!maage_status_failed(status) && (source.width != resource->width || source.height != resource->height)) status = maage_status_make(MAAGE_STATUS_MALFORMED_INPUT, MAAGE_CONTEXT_RESOURCE, MAAGE_OPERATION_LOAD_TEXTURE, 0);
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
            memcpy(atlas.pixels + (size_t) (resource->y + y) * atlas.stride + (size_t) resource->x * 4, source.pixels + (size_t) y * source.stride, (size_t) source.width * 4);
        maage_image_destroy(&source);
    }
    /* Overlay resources were loaded once with the registered texture catalog.
     * Copy their normalized tiles beside the scene sprites; update only uses
     * numeric fluid IDs and never performs I/O or allocation. */
    overlay_y = 256;
    for (fluid_index = 1; fluid_index <= view->fluid_count; ++fluid_index) {
        fluid = maage_fluid_get(fluids, (maage_fluid_id) fluid_index);
        if (!fluid || !fluid->has_overlay_texture) continue;
        overlay = maage_presentation_texture(presentation, fluid->overlay_texture);
        if (!overlay || overlay->source >= resources->source_count || maage_status_failed(resources->source_results[overlay->source])) continue;
        source_x = (int) (resources->uv[fluid->overlay_texture][0] * resources->image.width + 0.5f);
        source_y = (int) (resources->uv[fluid->overlay_texture][1] * resources->image.height + 0.5f);
        for (y = 0; y < 16; ++y)
            memcpy(atlas.pixels + (size_t) (overlay_y + y) * atlas.stride, resources->image.pixels + (size_t) (source_y + y) * resources->image.stride + (size_t) source_x * 4, 64);
        view->fluid_overlays[fluid_index] = overlay_y;
        overlay_y += 16;
    }
    memset(atlas.pixels + 255 * atlas.stride + 127 * 4, 255, 4);
    pixels.width = atlas.width;
    pixels.height = atlas.height;
    pixels.stride = atlas.stride;
    pixels.pixels = atlas.pixels;
    uploaded = maage_renderer_create_texture(renderer, &pixels, &view->texture, trace);
    result = uploaded;
    if (maage_status_code(uploaded) != MAAGE_STATUS_SUCCESS) goto done;
    view->renderer = renderer;
    view->sky.texture = view->weather.texture = view->texture;
    view->sky.blend = MAAGE_RENDER_BLEND_ADDITIVE;
    view->sky.vertices = view->sky_vertices;
    view->sky.indices = view->sky_indices;
    view->sky.projective_depth = view->sky_depth;
    view->weather.vertices = view->weather_vertices;
    view->weather.indices = view->weather_indices;
    ok = 1;
done:
    maage_image_destroy(&source);
    maage_image_destroy(&atlas);
    if (!ok) {
        maage_client_environment_destroy(view);
        return MAAGE_STATUS_PROPAGATE(trace, result, MAAGE_CONTEXT_RESOURCE, MAAGE_OPERATION_INITIALIZE);
    }
    *output = view;
    if (trace && view->failed_resources) *trace = recovered_trace;
    return MAAGE_STATUS_PROPAGATE(trace, maage_status_make(view->failed_resources ? MAAGE_STATUS_PARTIAL : MAAGE_STATUS_SUCCESS, MAAGE_CONTEXT_RESOURCE, MAAGE_OPERATION_INITIALIZE, 0), MAAGE_CONTEXT_CLIENT, MAAGE_OPERATION_INITIALIZE);
}
maage_status maage_client_environment_resource_status(const maage_client_environment *view, unsigned int *failed_resources) {
    if (failed_resources) *failed_resources = view ? view->failed_resources : 0;
    return view ? view->load_status : maage_status_make(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_CONTEXT_RESOURCE, MAAGE_OPERATION_INITIALIZE, 0);
}
maage_status maage_client_environment_destroy(maage_client_environment *view) {
    if (!view) return maage_status_make(MAAGE_STATUS_NOOP, 0, MAAGE_OPERATION_DESTROY, 0);
    if (view->renderer) {
        maage_renderer_set_scene(view->renderer, 1, NULL, NULL);
        maage_renderer_set_fog(view->renderer, 0, 0, 0, 0);
    }
    maage_render_texture_destroy(view->texture);
    free(view->fluid_overlays);
    free(view);
    return maage_status_make(MAAGE_STATUS_SUCCESS, 0, MAAGE_OPERATION_DESTROY, 0);
}
static void maage_client_environment_body(maage_client_environment *view, const double direction[3], const float view_projection[16], float width, float height, int moon, unsigned int phase, float alpha) {
    const maage_client_environment_resource *resource = &maage_client_environment_resources[moon ? 1 : 0];
    /* Project the orbit's actual plane, including its orientation. Clip before
     * division so a sprite crossing the camera plane stays well-defined. */
    double points[4][5], clipped[6][5], position[3], tangent[3];
    double half = moon ? 20.0 : 30.0, fraction, horizontal, vertical;
    double u, v, uv_width, uv_height;
    int corner, previous, count = 0, component;
    size_t first = view->sky.vertex_count;
    maage_render_ui_vertex *vertex;
    tangent[0] = -direction[1];
    tangent[1] = direction[0];
    tangent[2] = 0;
    uv_width = (double) resource->width / (moon ? 4 : 1) / 256;
    uv_height = (double) resource->height / (moon ? 2 : 1) / view->atlas_height;
    u = (double) resource->x / 256 + (moon ? (phase % 4) * uv_width : 0);
    v = (double) resource->y / view->atlas_height + (moon ? (phase / 4) * uv_height : 0);
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
                clipped[count][component] = points[previous][component] + fraction * (points[corner][component] - points[previous][component]);
            ++count;
        }
        if (points[corner][2] >= 0.1) {
            memcpy(clipped[count], points[corner], sizeof(points[corner]));
            ++count;
        }
    }
    if (count < 3) return;
    for (corner = 0; corner < count; ++corner) {
        view->sky_depth[view->sky.vertex_count] = (float) clipped[corner][2];
        vertex = &view->sky_vertices[view->sky.vertex_count++];
        vertex->position[0] = width * 0.5f * (1 + (float) (clipped[corner][0] / clipped[corner][2]));
        vertex->position[1] = height * 0.5f * (1 - (float) (clipped[corner][1] / clipped[corner][2]));
        vertex->uv[0] = (float) clipped[corner][3];
        vertex->uv[1] = (float) clipped[corner][4];
        vertex->color[0] = vertex->color[1] = vertex->color[2] = 1;
        vertex->color[3] = alpha;
    }
    for (corner = 1; corner < count - 1; ++corner) {
        view->sky_indices[view->sky.index_count++] = (unsigned int) first;
        view->sky_indices[view->sky.index_count++] = (unsigned int) first + corner;
        view->sky_indices[view->sky.index_count++] = (unsigned int) first + corner + 1;
    }
}
static void maage_client_sky_color(double temperature, double brightness, double rain, double thunder, float color[3]) {
    double hue, saturation, part, gray, weight;
    int sector;
    temperature /= 3;
    if (temperature < -1) temperature = -1;
    if (temperature > 1) temperature = 1;
    hue = (0.6222222222 - temperature * 0.05) * 6;
    saturation = 0.5 + temperature * 0.1;
    sector = (int) hue;
    part = hue - sector;
    color[0] = (float) (sector == 3 ? 1 - saturation : 1 - saturation * (1 - part));
    color[1] = (float) (sector == 3 ? 1 - saturation * part : 1 - saturation);
    color[2] = 1;
    color[0] *= (float) brightness;
    color[1] *= (float) brightness;
    color[2] *= (float) brightness;
    gray = (color[0] * 0.3 + color[1] * 0.59 + color[2] * 0.11) * 0.6;
    weight = rain * 0.75;
    color[0] = (float) (color[0] * (1 - weight) + gray * weight);
    color[1] = (float) (color[1] * (1 - weight) + gray * weight);
    color[2] = (float) (color[2] * (1 - weight) + gray * weight);
    gray = (color[0] * 0.3 + color[1] * 0.59 + color[2] * 0.11) * 0.2;
    weight = thunder * rain * 0.75;
    color[0] = (float) (color[0] * (1 - weight) + gray * weight);
    color[1] = (float) (color[1] * (1 - weight) + gray * weight);
    color[2] = (float) (color[2] * (1 - weight) + gray * weight);
}
/* Tile within the atlas rather than wrapping across unrelated resources. At
 * most 25 clipped quads reproduce the four repeats and orientation-linked UVs. */
static void maage_client_environment_water(maage_client_environment *view, float width, float height, double yaw, double pitch, float brightness, int overlay_y) {
    maage_client_environment_resource tile = {NULL, 0, 0, 16, 16};
    const maage_client_environment_resource *resource = &tile;
    double u = -yaw * 180.0 / (3.141592653589793 * 64.0), v = pitch * 180.0 / (3.141592653589793 * 64.0);
    float tile_width = width * 0.25f, tile_height = height * 0.25f;
    float left, top, right, bottom, x, y, uv_x, uv_y;
    size_t first, vertex;
    int row, column;
    tile.y = overlay_y;
    u -= floor(u);
    v -= floor(v);
    for (row = -1; row <= 4; ++row)
        for (column = -1; column <= 4; ++column) {
            x = (float) (column + u) * tile_width;
            y = (float) (row - v) * tile_height;
            left = x < 0 ? 0 : x;
            top = y < 0 ? 0 : y;
            right = x + tile_width > width ? width : x + tile_width;
            bottom = y + tile_height > height ? height : y + tile_height;
            if (right <= left || bottom <= top) continue;
            uv_x = (resource->x + (1 - (left - x) / tile_width) * resource->width) / 256.0f;
            uv_y = (resource->y + (top - y) / tile_height * resource->height) / view->atlas_height;
            first = view->weather.vertex_count;
            maage_client_environment_quad(&view->weather, left, top, right - left, bottom - top, uv_x, uv_y, -(right - left) / tile_width * resource->width / 256.0f, (bottom - top) / tile_height * resource->height / view->atlas_height, 0.5f);
            for (vertex = first; vertex < view->weather.vertex_count; ++vertex)
                view->weather_vertices[vertex].color[0] = view->weather_vertices[vertex].color[1] = view->weather_vertices[vertex].color[2] = brightness;
        }
}

static unsigned long maage_client_particle_random(maage_client_environment *view, unsigned long bound) {
    view->particle_random = (view->particle_random * 1664525UL + 1013904223UL) & 0xffffffffUL;
    return (view->particle_random >> 8) % bound;
}
/* Display sampling is bounded and independent of Server simulation. Only the
 * presentation replica is read; paused/duplicate snapshots never emit twice.
 * This visual pool limits particles, never the number of simulated furnaces. */
static void maage_client_environment_particles(maage_client_environment *view, const maage_session_snapshot *snapshot, const maage_world *visual, const double camera[3], const float matrix[16], float width, float height) {
    const maage_block_registry *blocks = maage_world_registry(visual);
    const maage_registry_bindings *ids = maage_block_registry_bindings(blocks);
    const maage_block_state_definition *state;
    maage_world_target hit;
    maage_status result;
    double now = maage_time_monotonic_seconds(), dt = now - view->particle_time;
    double direction[3], projected[3], distance, size, fade, offset;
    float u, v;
    unsigned int i, axis, pair, index, frame;
    int x, y, z;
    size_t first;
    if (!view->available[5]) return;
    if (view->particle_session != snapshot->session_epoch || view->particle_world != snapshot->world_epoch || snapshot->environment.ticks_high < view->particle_tick_high || (snapshot->environment.ticks_high == view->particle_tick_high && snapshot->environment.ticks_low < view->particle_tick_low)) {
        memset(view->particles, 0, sizeof(view->particles));
        view->particle_session = snapshot->session_epoch;
        view->particle_world = snapshot->world_epoch;
        view->particle_random = 0x7427UL;
        dt = 0;
    }
    view->particle_time = now;
    if (dt < 0 || snapshot->paused) dt = 0;
    if (dt > 0.2) dt = 0.2;
    if (!snapshot->paused && (view->particle_tick_low != snapshot->environment.ticks_low || view->particle_tick_high != snapshot->environment.ticks_high)) {
        view->particle_tick_low = snapshot->environment.ticks_low;
        view->particle_tick_high = snapshot->environment.ticks_high;
        for (i = 0; i < 1000U; ++i) {
            x = (int) floor(snapshot->player.player.position[0]) + (int) maage_client_particle_random(view, 16);
            x -= (int) maage_client_particle_random(view, 16);
            y = (int) floor(snapshot->player.player.position[1]) + (int) maage_client_particle_random(view, 16);
            y -= (int) maage_client_particle_random(view, 16);
            z = (int) floor(snapshot->player.player.position[2]) + (int) maage_client_particle_random(view, 16);
            z -= (int) maage_client_particle_random(view, 16);
            if (y < 0 || y >= MAAGE_WORLD_HEIGHT || !maage_world_chunk_loaded(visual, maage_world_chunk_coordinate(x), maage_world_chunk_coordinate(z))) continue;
            state = maage_block_registry_state(blocks, maage_world_get(visual, x, y, z));
            if (!state || state->presentation_effect != 1) continue;
            offset = maage_client_particle_random(view, 65536UL) / 65536.0 * 0.6 - 0.3;
            for (pair = 0; pair < 2; ++pair) {
                index = view->particle_cursor++ % MAAGE_ENVIRONMENT_PARTICLES;
                view->particles[index].x = x;
                view->particles[index].y = y;
                view->particles[index].z = z;
                view->particles[index].position[0] = x + 0.5;
                view->particles[index].position[1] = y + maage_client_particle_random(view, 65536UL) / 65536.0 * 0.375;
                view->particles[index].position[2] = z + 0.5;
                if (state->metadata & 1U) {
                    view->particles[index].position[0] += state->metadata == 1 ? 0.52 : -0.52;
                    view->particles[index].position[2] += offset;
                } else {
                    view->particles[index].position[2] += state->metadata == 2 ? 0.52 : -0.52;
                    view->particles[index].position[0] += offset;
                }
                view->particles[index].age = 0;
                view->particles[index].lifetime = 0.4 + maage_client_particle_random(view, 65536UL) / 65536.0 * 1.2;
                view->particles[index].smoke = pair == 0;
                view->particles[index].active = 1;
            }
        }
    }
    for (i = 0; i < MAAGE_ENVIRONMENT_PARTICLES; ++i) {
        if (!view->particles[i].active) continue;
        state = maage_block_registry_state(blocks, maage_world_get(visual, view->particles[i].x, view->particles[i].y, view->particles[i].z));
        view->particles[i].age += dt;
        if (!state || state->presentation_effect != 1 || view->particles[i].age >= view->particles[i].lifetime || !maage_world_chunk_loaded(visual, maage_world_chunk_coordinate(view->particles[i].x), maage_world_chunk_coordinate(view->particles[i].z))) {
            view->particles[i].active = 0;
            continue;
        }
        view->particles[i].position[1] += dt * (view->particles[i].smoke ? 0.4 : 0.05);
        for (axis = 0; axis < 3; ++axis)
            direction[axis] = view->particles[i].position[axis] - camera[axis];
        distance = sqrt(direction[0] * direction[0] + direction[1] * direction[1] + direction[2] * direction[2]);
        if (distance < 0.05 || distance > 24) continue;
        result = maage_world_raycast(visual, camera, direction, distance - 0.02, &hit);
        if (maage_status_failed(result) || maage_status_code(result) == MAAGE_STATUS_SUCCESS) continue;
        projected[0] = matrix[12];
        projected[1] = matrix[13];
        projected[2] = matrix[15];
        for (axis = 0; axis < 3; ++axis) {
            projected[0] += matrix[axis * 4] * view->particles[i].position[axis];
            projected[1] += matrix[axis * 4 + 1] * view->particles[i].position[axis];
            projected[2] += matrix[axis * 4 + 3] * view->particles[i].position[axis];
        }
        if (projected[2] < 0.1) continue;
        fade = view->particles[i].age / view->particles[i].lifetime;
        size = width * sqrt(matrix[0] * matrix[0] + matrix[4] * matrix[4] + matrix[8] * matrix[8]) * (view->particles[i].smoke ? 0.1 * (1 + fade) : 0.1 * (1 - fade * fade * 0.5)) / projected[2];
        frame = view->particles[i].smoke ? 7U - (unsigned int) (fade * 8) : 48U;
        u = (float) (frame % 16U) * 8 / 256;
        v = (96.0f + (float) (frame / 16U) * 8) / view->atlas_height;
        first = view->weather.vertex_count;
        maage_client_environment_quad(&view->weather, (float) (width * 0.5 * (1 + projected[0] / projected[2]) - size * 0.5), (float) (height * 0.5 * (1 - projected[1] / projected[2]) - size * 0.5), (float) size, (float) size, u, v, 8.0f / 256, 8.0f / view->atlas_height, 1);
        if (view->particles[i].smoke)
            for (axis = 0; axis < 4; ++axis)
                view->weather_vertices[first + axis].color[0] = view->weather_vertices[first + axis].color[1] = view->weather_vertices[first + axis].color[2] = 0.3f;
    }
}

maage_status maage_client_environment_update(maage_client_environment *view, maage_renderer *renderer, const maage_session_snapshot *snapshot, float width, float height, const float view_projection[16], const maage_world *visual, const double camera[3], double yaw, double pitch, int first_person, int radius) {
    const maage_client_environment_resource *resource;
    const maage_environment_state *state;
    const maage_fluid_definition *fluid;
    double sun[3], angle, phase, sunlight, rain, sky_brightness, visibility, camera_dx, camera_dz;
    float x, y, size, alpha;
    float sky_color[3], brightness, sky_level, block_level, range_weight;
    unsigned char light = 0;
    unsigned int i, hash;
    int cold = 0, precipitation = 0;
    maage_status result;
    if (!view || !renderer || !snapshot || !view_projection || !visual || !camera || snapshot->protocol_version != MAAGE_SESSION_PROTOCOL_VERSION) return MAAGE_STATUS_VALUE(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_CONTEXT_CLIENT, MAAGE_OPERATION_UPDATE, 0);
    if (width <= 0 || height <= 0) return maage_status_make(MAAGE_STATUS_NOOP, 0, MAAGE_OPERATION_UPDATE, 0);
    if (!(width > 0 && height > 0)) return maage_status_make(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_CONTEXT_CLIENT, MAAGE_OPERATION_UPDATE, 0);
    state = &snapshot->environment;
    view->sky.vertex_count = view->sky.index_count = view->weather.vertex_count = view->weather.index_count = 0;
    view->sky.logical_width = view->weather.logical_width = width;
    view->sky.logical_height = view->weather.logical_height = height;
    view->weather.after_foreground = 0;
    sunlight = maage_environment_sunlight(state);
    rain = state->rain_intensity;
    result = maage_renderer_set_animation_tick(renderer, (float) (state->ticks_low % 3648UL));
    if (maage_status_failed(result)) return result;
    fluid = maage_fluid_get(maage_block_registry_fluids(maage_world_registry(visual)), maage_world_fluid_at(visual, camera));
    if (fluid && fluid->fog_density > 0) {
        result = maage_world_present_light(visual, (int) floor(camera[0]), (int) floor(camera[1]), (int) floor(camera[2]), &light);
        if (maage_status_failed(result)) return result;
        if (maage_status_code(result) == MAAGE_STATUS_SUCCESS || maage_status_code(result) == MAAGE_STATUS_PARTIAL) {
            sky_level = (float) ((light >> 4) * sunlight / 15.0);
            block_level = (float) (light & 15U) / 15.0f;
            brightness = sky_level > block_level ? sky_level : block_level;
            view->water_brightness = brightness / (4 - 3 * brightness);
        }
        range_weight = radius / 16.0f;
        if (range_weight < 0) range_weight = 0;
        if (range_weight > 1) range_weight = 1;
        brightness = fluid->fog_light_scaled ? view->water_brightness * (1 - range_weight) + range_weight : 1;
        result = maage_renderer_set_fog(renderer, fluid->fog_color[0] * brightness, fluid->fog_color[1] * brightness, fluid->fog_color[2] * brightness, fluid->fog_density);
        if (maage_status_failed(result)) return result;
        result = maage_renderer_set_clear_color(renderer, fluid->fog_color[0] * brightness, fluid->fog_color[1] * brightness, fluid->fog_color[2] * brightness, 1);
        if (maage_status_failed(result)) return result;
        view->weather.after_foreground = 1;
        if (first_person && fluid->id <= view->fluid_count && view->fluid_overlays[fluid->id]) maage_client_environment_water(view, width, height, yaw, pitch, fluid->fog_light_scaled ? view->water_brightness : 1, view->fluid_overlays[fluid->id]);
        maage_client_environment_particles(view, snapshot, visual, camera, view_projection, width, height);
        return maage_renderer_set_scene(renderer, (float) sunlight, &view->sky, &view->weather);
    }
    angle = maage_environment_celestial_angle(state) * 6.283185307179586;
    sky_brightness = cos(angle) * 2 + 0.5;
    if (sky_brightness < 0) sky_brightness = 0;
    if (sky_brightness > 1) sky_brightness = 1;
    maage_client_sky_color(snapshot->has_observation ? snapshot->observation.temperature : 0.8, sky_brightness, rain, state->thunder_intensity, sky_color);
    result = maage_renderer_set_clear_color(renderer, sky_color[0], sky_color[1], sky_color[2], 1);
    if (maage_status_code(result) != MAAGE_STATUS_SUCCESS) return result;
    /* The draw-selection square extends at least radius whole chunks around
     * the player, regardless of position inside its center chunk. Use that stable
     * inscribed range and the actual camera offset, including third person. */
    camera_dx = fabs(camera[0] - snapshot->player.player.position[0]);
    camera_dz = fabs(camera[2] - snapshot->player.player.position[2]);
    visibility = (double) radius * MAAGE_CHUNK_EDGE - (camera_dx > camera_dz ? camera_dx : camera_dz);
    if (!(visibility > 0)) return maage_status_make(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_CONTEXT_CLIENT, MAAGE_OPERATION_UPDATE, 0);
    result = maage_renderer_set_distance_fog(renderer, sky_color[0], sky_color[1], sky_color[2], (float) (visibility * 0.75), (float) (visibility * 0.95));
    if (maage_status_failed(result)) return result;
    sun[0] = -sin(angle);
    sun[1] = cos(angle);
    sun[2] = 0;
    if (view->available[0]) maage_client_environment_body(view, sun, view_projection, width, height, 0, 0, (float) (1 - rain));
    for (i = 0; i < 3; ++i)
        sun[i] = -sun[i];
    if (view->available[1]) maage_client_environment_body(view, sun, view_projection, width, height, 1, (unsigned int) (state->day_low & 7), (float) (1 - rain));
    /* Server sampled the player's sky/climate. No client World access. */
    if (snapshot->has_observation) {
        cold = snapshot->observation.temperature < 0.15;
        precipitation = snapshot->observation.sky_exposed && snapshot->observation.rainfall > 0;
    }
    phase = (double) (state->ticks_low % 120000UL) * 0.05;
    resource = &maage_client_environment_resources[cold ? 3 : 2];
    if (precipitation && rain > 0 && view->available[cold ? 3 : 2])
        for (i = 0; i < 48; ++i) {
            hash = (i * 1664525U + 1013904223U) ^ (i * 7919U);
            x = (float) (hash % 1009U) / 1009 * width;
            y = (float) fmod((double) (hash % 997U) / 997 * (height + 24) + phase * (cold ? 12 : 220), height + 24) - 24;
            size = cold ? 3 : 2;
            alpha = (float) rain * (cold ? 0.7f : 0.42f);
            if (cold) x += (float) sin(phase + i) * 4;
            maage_client_environment_quad(&view->weather, x, y, size, cold ? size : 18, (float) resource->x / 256, (float) resource->y / view->atlas_height, (float) resource->width / 256, (float) resource->height / view->atlas_height, alpha);
        }
    maage_client_environment_particles(view, snapshot, visual, camera, view_projection, width, height);
    return maage_renderer_set_scene(renderer, (float) sunlight, &view->sky, &view->weather);
}
