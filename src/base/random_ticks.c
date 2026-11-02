#include "base_internal.h"
#include "maage/entities.h"
#include <string.h>
static maage_status maage_random_noop(void) {
    return maage_status_make(MAAGE_STATUS_NOOP, 0, MAAGE_OPERATION_TICK, 0);
}
/* Unlike visual get(), these queries never mistake missing columns for air. */
static maage_status maage_random_block(const maage_world *world, int x, int y, int z, const maage_block_state_definition **state, const maage_block_definition **block) {
    const maage_block_registry *registry = maage_world_registry(world);
    if (x <= -MAAGE_WORLD_LIMIT || x >= MAAGE_WORLD_LIMIT ||
        z <= -MAAGE_WORLD_LIMIT || z >= MAAGE_WORLD_LIMIT ||
        !maage_world_chunk_loaded(world, maage_world_chunk_coordinate(x), maage_world_chunk_coordinate(z)))
        return maage_random_noop();
    *state = maage_block_registry_state(registry, maage_world_get(world, x, y, z));
    *block = *state ? maage_block_registry_block(registry, (*state)->block_id) : NULL;
    return maage_status_make(*block ? MAAGE_STATUS_SUCCESS : MAAGE_STATUS_INVARIANT_VIOLATION, MAAGE_CONTEXT_WORLD, MAAGE_OPERATION_TICK, 0);
}

static int maage_random_light_value(unsigned char light, const maage_random_tick_context *context) {
    int sky = (int) (light >> 4) - (int) context->sky_subtracted;
    int block = light & 15;
    return sky > block ? sky : block;
}

/* Capacity and stack validity are preflighted before the block mutation. */

static maage_status maage_random_grass_light(const maage_world *world, int x, int y, int z, const maage_block_definition *cover, const maage_random_tick_context *context, int *value) {
    static const int offsets[5][3] = {{0, 1, 0}, {1, 0, 0}, {-1, 0, 0}, {0, 0, 1}, {0, 0, -1}};
    unsigned char light;
    unsigned int side;
    int brightness;
    maage_status result;
    if (!cover->use_neighbor_brightness) {
        result = maage_world_sample_light(world, x, y, z, &light);
        if (maage_status_code(result) == MAAGE_STATUS_SUCCESS) *value = maage_random_light_value(light, context);
        return result;
    }
    *value = 0;
    for (side = 0; side < 5; ++side) {
        result = maage_world_sample_light(world, x + offsets[side][0], y + offsets[side][1], z + offsets[side][2], &light);
        if (maage_status_code(result) != MAAGE_STATUS_SUCCESS) return result;
        brightness = maage_random_light_value(light, context);
        if (brightness > *value) *value = brightness;
    }
    return maage_status_make(MAAGE_STATUS_SUCCESS, 0, MAAGE_OPERATION_QUERY, 0);
}

maage_status maage_random_tick_grass(maage_world *world, int x, int y, int z, const maage_block_state_definition *state, maage_random_tick_context *context) {
    const maage_base_ids *ids = maage_base_state_references(maage_world_registry(world), state);
    const maage_block_state_definition *cover, *destination;
    const maage_block_definition *block;
    unsigned int attempt;
    int brightness, nx, ny, nz;
    maage_status result, outcome = maage_random_noop();
    result = maage_random_block(world, x, y + 1, z, &cover, &block);
    if (maage_status_code(result) != MAAGE_STATUS_SUCCESS) return result;
    result = maage_random_grass_light(world, x, y + 1, z, block, context, &brightness);
    if (maage_status_code(result) != MAAGE_STATUS_SUCCESS) return result;
    if (brightness < 4 && block->light_opacity > 2) return maage_world_set(world, x, y, z, state->id, ids->state_dirt);
    if (brightness < 9) return outcome;
    for (attempt = 0; attempt < 4; ++attempt) {
        nx = x + (int) maage_random_tick_next(context, 3) - 1;
        ny = y + (int) maage_random_tick_next(context, 5) - 3;
        nz = z + (int) maage_random_tick_next(context, 3) - 1;
        if (ny < 0 || ny >= MAAGE_WORLD_HEIGHT) continue;
        result = maage_random_block(world, nx, ny, nz, &destination, &block);
        if (maage_status_failed(result)) return result;
        if (maage_status_code(result) != MAAGE_STATUS_SUCCESS || destination->block_id != ids->block_dirt || destination->metadata != 0) continue;
        result = maage_random_block(world, nx, ny + 1, nz, &cover, &block);
        if (maage_status_failed(result)) return result;
        if (maage_status_code(result) != MAAGE_STATUS_SUCCESS || block->light_opacity > 2) continue;
        result = maage_random_grass_light(world, nx, ny + 1, nz, block, context, &brightness);
        if (maage_status_failed(result)) return result;
        if (maage_status_code(result) != MAAGE_STATUS_SUCCESS || brightness < 4) continue;
        result = maage_world_set(world, nx, ny, nz, destination->id, ids->state_grass);
        if (maage_status_failed(result)) return result;
        outcome = result;
    }
    return outcome;
}

maage_status maage_random_tick_farmland(maage_world *world, int x, int y, int z, const maage_block_state_definition *state, maage_random_tick_context *context) {
    const maage_base_ids *ids = maage_base_state_references(maage_world_registry(world), state);
    const maage_block_state_definition *neighbor;
    const maage_block_definition *block;
    maage_environment_state environment;
    maage_world_observation observation;
    int dx, dz, dy, valid, hydrated = 0, unavailable = 0, exposed = 1;
    unsigned int moisture;
    maage_status result;
    (void) context;
    if (state->metadata > 7) return maage_status_make(MAAGE_STATUS_INVARIANT_VIOLATION, MAAGE_CONTEXT_WORLD, MAAGE_OPERATION_TICK, 0);
    result = maage_world_can_survive(world, x, y, z, state->id, &valid);
    if (maage_status_code(result) != MAAGE_STATUS_SUCCESS) return result;
    if (!valid) return maage_world_check_support(world, x, y, z);
    /* Water at soil height or one cell above hydrates the radius-four square.
     * A known water source suffices even if another part is unavailable. */
    for (dz = -4; dz <= 4 && !hydrated; ++dz)
        for (dx = -4; dx <= 4 && !hydrated; ++dx)
            for (dy = 0; dy <= 1; ++dy) {
                result = maage_random_block(world, x + dx, y + dy, z + dz, &neighbor, &block);
                if (maage_status_failed(result)) return result;
                if (maage_status_code(result) != MAAGE_STATUS_SUCCESS) unavailable = 1;
                else if (neighbor->fluid_kind == ids->fluid_water) {
                    hydrated = 1;
                    break;
                }
            }
    if (!hydrated && y < MAAGE_WORLD_HEIGHT - 1) {
        maage_world_environment(world, &environment);
        if (environment.rain_intensity > 0.2) {
            if (!maage_world_observe(world, x, y + 1, z, &observation)) return maage_random_noop();
            if (observation.rainfall > 0 && observation.temperature >= 0.15) {
                /* The ordinary heightmap includes nonblocking plants. Rain
                 * passes through them, but solid material and fluids shelter. */
                for (dy = y + 1; dy < observation.precipitation_height; ++dy) {
                    result = maage_random_block(world, x, dy, z, &neighbor, &block);
                    if (maage_status_code(result) != MAAGE_STATUS_SUCCESS) return result;
                    if (block->material_solid || neighbor->fluid_kind) {
                        exposed = 0;
                        break;
                    }
                }
                hydrated = exposed;
            }
        }
    }
    if (!hydrated && unavailable) return maage_random_noop();
    moisture = hydrated ? 7U : (state->metadata ? state->metadata - 1U : 0U);
    if (!hydrated && !state->metadata) {
        result = maage_random_block(world, x, y + 1, z, &neighbor, &block);
        if (maage_status_code(result) != MAAGE_STATUS_SUCCESS) return result;
        if (neighbor->block_id != ids->block_wheat) return maage_world_set(world, x, y, z, state->id, ids->state_dirt);
    }
    return moisture == state->metadata ? maage_random_noop() : maage_world_set(world, x, y, z, state->id, ids->farmland[moisture]);
}

maage_status maage_random_tick_wheat(maage_world *world, int x, int y, int z, const maage_block_state_definition *state, maage_random_tick_context *context) {
    const maage_base_ids *ids = maage_base_state_references(maage_world_registry(world), state);
    const maage_block_state_definition *neighbor;
    const maage_block_definition *block;
    unsigned char light;
    int dx, dz, valid, across_x = 0, across_z = 0, diagonal = 0;
    double speed = 1, contribution;
    maage_status result;
    if (state->metadata > 7) return maage_status_make(MAAGE_STATUS_INVARIANT_VIOLATION, MAAGE_CONTEXT_WORLD, MAAGE_OPERATION_TICK, 0);
    result = maage_world_can_survive(world, x, y, z, state->id, &valid);
    if (maage_status_code(result) != MAAGE_STATUS_SUCCESS) return result;
    if (!valid) return maage_world_check_support(world, x, y, z);
    if (state->metadata == 7) return maage_random_noop();
    result = maage_world_sample_light(world, x, y + 1, z, &light);
    if (maage_status_code(result) != MAAGE_STATUS_SUCCESS) return result;
    if (maage_random_light_value(light, context) < 9) return maage_random_noop();
    /* Center soil contributes fully; the other eight contribute a quarter.
     * Crossed rows or diagonal wheat halve the resulting opportunity rate. */
    for (dz = -1; dz <= 1; ++dz)
        for (dx = -1; dx <= 1; ++dx) {
            result = maage_random_block(world, x + dx, y - 1, z + dz, &neighbor, &block);
            if (maage_status_code(result) != MAAGE_STATUS_SUCCESS) return result;
            if (neighbor->block_id == ids->block_farmland) {
                contribution = neighbor->metadata ? 3.0 : 1.0;
                speed += dx || dz ? contribution * 0.25 : contribution;
            } else if (!dx && !dz) return maage_random_noop();
            if (!dx && !dz) continue;
            result = maage_random_block(world, x + dx, y, z + dz, &neighbor, &block);
            if (maage_status_code(result) != MAAGE_STATUS_SUCCESS) return result;
            if (neighbor->block_id != ids->block_wheat) continue;
            if (dx && dz) diagonal = 1;
            else if (dx) across_x = 1;
            else across_z = 1;
        }
    if (diagonal || (across_x && across_z)) speed *= 0.5;
    if (maage_random_tick_next(context, (unsigned int) (25.0 / speed) + 1U)) return maage_random_noop();
    return maage_world_set(world, x, y, z, state->id, ids->wheat[state->metadata + 1U]);
}

maage_status maage_random_tick_sapling(maage_world *world, int x, int y, int z, const maage_block_state_definition *state, maage_random_tick_context *context) {
    const maage_base_ids *ids = maage_base_state_references(maage_world_registry(world), state);
    const maage_block_state_definition *neighbor;
    const maage_block_definition *block;
    maage_block_state_id staged, reset[4];
    maage_block_plan plan;
    unsigned char light;
    unsigned int species = state->metadata & 7U;
    unsigned long random;
    int dx, dz, ox, oz, match, attempted, large = 0;
    maage_status result;
    if (species > 2 || (state->metadata & ~15U)) return maage_status_make(MAAGE_STATUS_INVARIANT_VIOLATION, MAAGE_CONTEXT_WORLD, MAAGE_OPERATION_TICK, 0);
    result = maage_world_check_support(world, x, y, z);
    if (maage_status_failed(result) || maage_status_code(result) == MAAGE_STATUS_NOOP || maage_world_get(world, x, y, z) != state->id) return result;
    result = maage_world_sample_light(world, x, y + 1, z, &light);
    if (maage_status_code(result) != MAAGE_STATUS_SUCCESS) return result;
    if (maage_random_light_value(light, context) < 9 || maage_random_tick_next(context, 7)) return maage_random_noop();
    if (!(state->metadata & 8U)) {
        result = maage_block_sapling_state(maage_world_registry(world), ids, state->metadata | 8U, &staged);
        return maage_status_failed(result) ? result : maage_world_set(world, x, y, z, state->id, staged);
    }
    if (species == 1) {
        /* A missing neighbor must not turn a possible 2x2 into a single tree. */
        for (dz = -1; dz <= 1; ++dz)
            for (dx = -1; dx <= 1; ++dx) {
                result = maage_random_block(world, x + dx, y, z + dz, &neighbor, &block);
                if (maage_status_code(result) != MAAGE_STATUS_SUCCESS) return result;
            }
        for (oz = 0; oz >= -1 && !large; --oz)
            for (ox = 0; ox >= -1 && !large; --ox) {
                match = 1;
                for (dz = 0; dz < 2; ++dz)
                    for (dx = 0; dx < 2; ++dx) {
                        neighbor = maage_block_registry_state(maage_world_registry(world), maage_world_get(world, x + ox + dx, y, z + oz + dz));
                        if (!neighbor || neighbor->block_id != ids->block_sapling || (neighbor->metadata & 7U) != species) match = 0;
                    }
                if (match) {
                    x += ox;
                    z += oz;
                    large = 1;
                }
            }
    } else if (!species) large = maage_random_tick_next(context, 10) == 0;
    random = ((1UL + maage_random_tick_next(context, 65535U)) << 16);
    random |= maage_random_tick_next(context, 65536U);
    result = maage_generation_grow_tree(ids, world, x, y, z, species, large, random, &attempted);
    if (maage_status_code(result) != MAAGE_STATUS_NOOP || !attempted) return result;
    /* A known obstruction restarts the two-stage cycle. Operational failure
     * and unavailable terrain keep every original sapling/stage untouched. */
    result = maage_block_sapling_state(maage_world_registry(world), ids, species, &staged);
    if (maage_status_failed(result)) return result;
    reset[0] = reset[1] = reset[2] = reset[3] = staged;
    plan.x = x;
    plan.y = y;
    plan.z = z;
    plan.edge = species == 1 && large ? 2 : 1;
    plan.height = 1;
    plan.cells = reset;
    return maage_world_apply_plan(world, &plan);
}

maage_status maage_random_tick_cactus(maage_world *world, int x, int y, int z, const maage_block_state_definition *state, maage_random_tick_context *context) {
    const maage_base_ids *ids = maage_base_state_references(maage_world_registry(world), state);
    static const int sides[4][2] = {{-1, 0}, {1, 0}, {0, -1}, {0, 1}};
    const maage_block_state_definition *neighbor;
    const maage_block_definition *block;
    maage_item_stack drop;
    unsigned int height, side;
    int obstructed = 0;
    maage_status result;
    if (state->metadata > 15) return maage_status_make(MAAGE_STATUS_INVARIANT_VIOLATION, MAAGE_CONTEXT_WORLD, MAAGE_OPERATION_TICK, 0);
    if (y >= MAAGE_WORLD_HEIGHT - 1) return maage_random_noop();
    result = maage_random_block(world, x, y + 1, z, &neighbor, &block);
    if (maage_status_code(result) != MAAGE_STATUS_SUCCESS) return result;
    if (neighbor->block_id != ids->block_air) return maage_random_noop();
    for (height = 1; height < 3; ++height) {
        result = maage_random_block(world, x, y - (int) height, z, &neighbor, &block);
        if (maage_status_code(result) != MAAGE_STATUS_SUCCESS) return result;
        if (neighbor->block_id != ids->block_cactus) break;
    }
    if (height >= 3) return maage_random_noop();
    if (state->metadata < 15) return maage_world_set(world, x, y, z, state->id, ids->cactus[state->metadata + 1]);
    /* Preflight new-growth side support before publishing geometry. */
    for (side = 0; side < 4; ++side) {
        result = maage_random_block(world, x + sides[side][0], y + 1, z + sides[side][1], &neighbor, &block);
        if (maage_status_code(result) != MAAGE_STATUS_SUCCESS) return result;
        if (block->material_solid) obstructed = 1;
    }
    if (obstructed) {
        block = maage_block_registry_block(maage_world_registry(world), state->block_id);
        if (!block) return maage_status_make(MAAGE_STATUS_INVARIANT_VIOLATION, MAAGE_CONTEXT_WORLD, MAAGE_OPERATION_TICK, 0);
        memset(&drop, 0, sizeof(drop));
        drop.item_id = block->drop_item_id;
        drop.count = block->drop_count;
        if (!maage_item_stack_valid(maage_world_item_registry(world), &drop)) return maage_status_make(MAAGE_STATUS_INVARIANT_VIOLATION, MAAGE_CONTEXT_WORLD, MAAGE_OPERATION_TICK, 0);
        result = maage_entities_reserve(maage_world_entities(world), 1);
        if (maage_status_failed(result)) return result;
    } else {
        result = maage_world_set(world, x, y + 1, z, ids->state_air, ids->state_cactus);
        if (maage_status_failed(result)) return result;
    }
    result = maage_world_set(world, x, y, z, state->id, ids->state_cactus);
    if (maage_status_failed(result) || !obstructed) return result;
    return maage_random_tick_spawn_drop(world, x, y + 1, z, &drop, context);
}

maage_status maage_random_tick_bush(maage_world *world, int x, int y, int z, const maage_block_state_definition *state, maage_random_tick_context *context) {
    (void) state;
    (void) context;
    return maage_world_check_support(world, x, y, z);
}

static maage_status maage_random_mushroom_site(const maage_base_ids *ids, const maage_world *world, int x, int y, int z, int *valid) {
    const maage_block_state_definition *state;
    const maage_block_definition *block;
    unsigned char light;
    maage_status result;
    *valid = 0;
    if (y <= 0 || y >= MAAGE_WORLD_HEIGHT) return maage_random_noop();
    result = maage_random_block(world, x, y, z, &state, &block);
    if (maage_status_code(result) != MAAGE_STATUS_SUCCESS || state->block_id != ids->block_air) return result;
    result = maage_random_block(world, x, y - 1, z, &state, &block);
    if (maage_status_code(result) != MAAGE_STATUS_SUCCESS || !block->opaque) return result;
    result = maage_world_sample_light(world, x, y, z, &light);
    /* Mushroom survival ignores daylight subtraction. The current catalog
     * has no mycelium or podzol, which would bypass this light requirement. */
    if (maage_status_code(result) == MAAGE_STATUS_SUCCESS) *valid = (light >> 4) < 13 && (light & 15) < 13;
    return result;
}

maage_status maage_random_tick_mushroom(maage_world *world, int x, int y, int z, const maage_block_state_definition *state, maage_random_tick_context *context) {
    const maage_base_ids *ids = maage_base_state_references(maage_world_registry(world), state);
    const maage_block_state_definition *neighbor;
    const maage_block_definition *block;
    int dx, dy, dz, count = 0, attempt, nx, ny, nz, valid;
    maage_status result;
    if (maage_random_tick_next(context, 25)) return maage_random_noop();
    /* Check the bounded 9x3x9 density neighborhood once per 25 callbacks. */
    for (dx = -4; dx <= 4; ++dx)
        for (dz = -4; dz <= 4; ++dz)
            for (dy = -1; dy <= 1; ++dy) {
                result = maage_random_block(world, x + dx, y + dy, z + dz, &neighbor, &block);
                if (maage_status_code(result) != MAAGE_STATUS_SUCCESS) return result;
                if (neighbor->block_id == state->block_id && ++count >= 5) return maage_random_noop();
            }
    for (attempt = 0; attempt < 5; ++attempt) {
        nx = x + (int) maage_random_tick_next(context, 3) - 1;
        ny = y + (int) maage_random_tick_next(context, 2);
        ny -= (int) maage_random_tick_next(context, 2);
        nz = z + (int) maage_random_tick_next(context, 3) - 1;
        result = maage_random_mushroom_site(ids, world, nx, ny, nz, &valid);
        if (maage_status_failed(result)) return result;
        if (attempt == 4) return valid ? maage_world_set(world, nx, ny, nz, ids->state_air, state->block_id == ids->block_brown_mushroom ? ids->state_brown_mushroom : ids->state_red_mushroom)
                                       : maage_random_noop();
        if (valid) {
            x = nx;
            y = ny;
            z = nz;
        }
    }
    return maage_random_noop();
}

maage_status maage_random_tick_snow(maage_world *world, int x, int y, int z, const maage_block_state_definition *state, maage_random_tick_context *context) {
    const maage_base_ids *ids = maage_base_state_references(maage_world_registry(world), state);
    unsigned char light;
    maage_status result = maage_world_sample_light(world, x, y, z, &light);
    (void) context;
    if (maage_status_code(result) != MAAGE_STATUS_SUCCESS) return result;
    return (light & 15) > 11 ? maage_world_set(world, x, y, z, state->id, ids->state_air) : maage_random_noop();
}

maage_status maage_random_tick_ice(maage_world *world, int x, int y, int z, const maage_block_state_definition *state, maage_random_tick_context *context) {
    const maage_base_ids *ids = maage_base_state_references(maage_world_registry(world), state);
    const maage_block_definition *block = maage_block_registry_block(maage_world_registry(world), state->block_id);
    unsigned char light;
    maage_status result = maage_world_sample_light(world, x, y, z, &light);
    (void) context;
    if (maage_status_code(result) != MAAGE_STATUS_SUCCESS) return result;
    if (!block) return maage_status_make(MAAGE_STATUS_INVARIANT_VIOLATION, MAAGE_CONTEXT_WORLD, MAAGE_OPERATION_TICK, 0);
    return (int) (light & 15) > 11 - (int) block->light_opacity ? maage_world_set(world, x, y, z, state->id, ids->state_water) : maage_random_noop();
}
