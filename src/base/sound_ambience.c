#include "sounds_internal.h"
#include "maage/session_protocol.h"
#include <math.h>
#include <string.h>

typedef struct base_ambient_state {
    unsigned long random, cave_ticks;
    unsigned int rain_counter;
    int initialized;
} base_ambient_state;
static unsigned long base_ambient_random(base_ambient_state *state, unsigned long bound) {
    state->random = (state->random * 1664525UL + 1013904223UL) & 0xffffffffUL;
    return (state->random >> 8) % bound;
}
static double base_ambient_span(base_ambient_state *state, double minimum, double maximum) {
    return minimum + (maximum - minimum) * base_ambient_random(state, 16777216UL) / 16777216.0;
}
static maage_status base_ambient_play(const maage_sound_ambient_sink *sink, maage_sound_id sound, unsigned int action, const double position[3], double gain, double pitch, maage_status_trace *trace) {
    return sink->play(sink->context, sound, action, 0, position, gain, pitch, trace);
}
static maage_status base_ambient_update(const void *context, void *storage, const maage_sound_scene_query *query, const maage_sound_ambient_sink *sink, maage_status_trace *trace) {
    const maage_base_sound_data *data = (const maage_base_sound_data *) context;
    base_ambient_state *state = (base_ambient_state *) storage;
    const maage_base_ids *ids = data->ids;
    const maage_world *visual = query->visual;
    const maage_block_registry *registry;
    const maage_block_state_definition *cell, *above;
    const maage_block_definition *block;
    maage_world_observation observation, local;
    double water_position[3], lava_position[3], rain_position[3], position[3], dx, dy, dz, rain;
    unsigned long water_count = 0, lava_count = 0, rain_count = 0, sample;
    int px, py, pz, x, y, z, cx, cz, i, samples, rain_above;
    maage_status result, outcome = maage_status_make(MAAGE_STATUS_SUCCESS, 0, MAAGE_OPERATION_SNAPSHOT, 0);
    if (!query->has_world || !visual || !query->environment) return maage_status_make(MAAGE_STATUS_NOOP, 0, MAAGE_OPERATION_SNAPSHOT, 0);
    if (!state->initialized) {
        state->random = 0x7f4a7c15UL ^ ((unsigned long) (fmod(query->now, 4294967.296) * 1000.0));
        state->cave_ticks = base_ambient_random(state, 12000);
        state->initialized = 1;
    }
    registry = maage_world_registry(visual);
    px = (int) floor(query->listener[0]);
    py = (int) floor(query->listener[1] - MAAGE_PLAYER_EYE_HEIGHT);
    pz = (int) floor(query->listener[2]);
    /* Bounded local display trials sample the replica without scanning loaded
     * columns. Reservoirs preserve equal selection probability within each
     * content-specific opportunity; no runtime resource names are consulted. */
    for (i = 0; i < 1000; ++i) {
        x = px + (int) base_ambient_random(state, 16) - (int) base_ambient_random(state, 16);
        y = py + (int) base_ambient_random(state, 16) - (int) base_ambient_random(state, 16);
        z = pz + (int) base_ambient_random(state, 16) - (int) base_ambient_random(state, 16);
        if (y < 0 || y >= MAAGE_WORLD_HEIGHT || !maage_world_chunk_loaded(visual, maage_world_chunk_coordinate(x), maage_world_chunk_coordinate(z))) continue;
        cell = maage_block_registry_state(registry, maage_world_get(visual, x, y, z));
        if (!cell) continue;
        if (cell->fluid_kind == ids->fluid_water && cell->fluid_level > 0 && cell->fluid_level < 8) {
            if (base_ambient_random(state, ++water_count) == 0) {
                water_position[0] = x + 0.5;
                water_position[1] = y + 0.5;
                water_position[2] = z + 0.5;
            }
        } else if (cell->fluid_kind == ids->fluid_lava) {
            above = maage_block_registry_state(registry, maage_world_get(visual, x, y + 1, z));
            if (above && above->block_id == ids->block_air && base_ambient_random(state, ++lava_count) == 0) {
                lava_position[0] = x;
                lava_position[1] = y;
                lava_position[2] = z;
            }
        }
    }
    rain = query->environment->rain_intensity;
    samples = (int) (100.0 * rain * rain);
    memset(&local, 0, sizeof(local));
    (void) maage_world_observe(visual, px, py, pz, &local);
    for (i = 0; i < samples; ++i) {
        x = px + (int) base_ambient_random(state, 10) - (int) base_ambient_random(state, 10);
        z = pz + (int) base_ambient_random(state, 10) - (int) base_ambient_random(state, 10);
        if (!maage_world_observe(visual, x, py, z, &observation)) continue;
        y = observation.precipitation_height;
        if (y < 1 || y >= MAAGE_WORLD_HEIGHT || y < py - 10 || y > py + 10) continue;
        if (!maage_world_observe(visual, x, y, z, &observation) || observation.rainfall <= 0 || observation.temperature < 0.15) continue;
        cell = maage_block_registry_state(registry, maage_world_get(visual, x, y - 1, z));
        block = cell ? maage_block_registry_block(registry, cell->block_id) : NULL;
        if (!block || cell->block_id == ids->block_air || cell->fluid_kind == ids->fluid_lava) continue;
        if (base_ambient_random(state, ++rain_count) != 0) continue;
        rain_position[0] = x + base_ambient_random(state, 65536) / 65536.0;
        rain_position[1] = y + 0.1 - block->selection_shape.minimum[1];
        rain_position[2] = z + base_ambient_random(state, 65536) / 65536.0;
    }
    if (rain_count && rain > 0 && base_ambient_random(state, 3) < state->rain_counter++) {
        state->rain_counter = 0;
        rain_above = rain_position[1] > query->listener[1] - MAAGE_PLAYER_EYE_HEIGHT + 1 && local.precipitation_height > py;
        result = base_ambient_play(sink, data->rain, MAAGE_SOUND_RAIN, rain_position, rain_above ? 0.1 : 0.2, rain_above ? 0.5 : 1, trace);
        if (maage_status_failed(result)) return result;
        if (maage_status_is_warning(result)) outcome = result;
    }
    for (sample = 0; sample < water_count; ++sample)
        if (base_ambient_random(state, 64) == 0) {
            result = base_ambient_play(sink, data->water, MAAGE_SOUND_WATER, water_position, base_ambient_span(state, 0.75, 1), base_ambient_span(state, 0.5, 1.5), trace);
            if (maage_status_failed(result)) return result;
            if (maage_status_is_warning(result)) outcome = result;
        }
    for (sample = 0; sample < lava_count; ++sample) {
        if (base_ambient_random(state, 100) == 0) {
            result = base_ambient_play(sink, data->lava_pop, MAAGE_SOUND_LAVA, lava_position, base_ambient_span(state, 0.2, 0.4), base_ambient_span(state, 0.9, 1.05), trace);
            if (maage_status_failed(result)) return result;
            if (maage_status_is_warning(result)) outcome = result;
        }
        if (base_ambient_random(state, 200) == 0) {
            result = base_ambient_play(sink, data->lava, MAAGE_SOUND_LAVA, lava_position, base_ambient_span(state, 0.2, 0.4), base_ambient_span(state, 0.9, 1.05), trace);
            if (maage_status_failed(result)) return result;
            if (maage_status_is_warning(result)) outcome = result;
        }
    }
    if (state->cave_ticks) {
        --state->cave_ticks;
        return outcome;
    }
    /* Air must be numerically dark and two to eight blocks from the listener.
     * Initial and successful-opportunity cooldowns belong to this Base policy. */
    for (cx = -1; cx <= 1; ++cx)
        for (cz = -1; cz <= 1; ++cz) {
            x = (maage_world_chunk_coordinate(px) + cx) * 16 + (int) base_ambient_random(state, 16);
            y = (int) base_ambient_random(state, 256);
            z = (maage_world_chunk_coordinate(pz) + cz) * 16 + (int) base_ambient_random(state, 16);
            position[0] = x + 0.5;
            position[1] = y + 0.5;
            position[2] = z + 0.5;
            dx = position[0] - query->listener[0];
            dy = position[1] - query->listener[1];
            dz = position[2] - query->listener[2];
            if (dx * dx + dy * dy + dz * dz <= 4 || dx * dx + dy * dy + dz * dz > 64) continue;
            if (maage_world_get(visual, x, y, z) != ids->state_air || !maage_world_observe(visual, x, y, z, &observation) || maage_status_code(maage_world_present_light(visual, x, y, z, &observation.light)) != MAAGE_STATUS_SUCCESS || (observation.light >> 4) != 0 || (observation.light & 15) > base_ambient_random(state, 8)) continue;
            state->cave_ticks = 6000UL + base_ambient_random(state, 12000);
            result = base_ambient_play(sink, data->cave, MAAGE_SOUND_CAVE_AMBIENT, position, 1, base_ambient_span(state, 0.8, 1), trace);
            return maage_status_failed(result) || maage_status_is_warning(result) ? result : outcome;
        }
    return outcome;
}
maage_status maage_base_sound_ambience_register(maage_content_context *context, const maage_base_sound_data *data) {
    maage_sound_ambient_definition ambient;
    memset(&ambient, 0, sizeof(ambient));
    ambient.state_bytes = sizeof(base_ambient_state);
    ambient.interval = 0.05;
    ambient.update = base_ambient_update;
    ambient.context = data;
    return maage_sound_ambient_register(context->sounds, "minecraft:local_ambience", &ambient);
}
