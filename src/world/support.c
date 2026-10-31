#include "world_internal.h"
#include "maage/entities.h"
#include <string.h>

static maage_status maage_support_status(unsigned char code) {
    return maage_status_make(code, MAAGE_CONTEXT_WORLD, MAAGE_OPERATION_UPDATE, 0);
}
static maage_status maage_support_block(const maage_world *world, int x, int y, int z, const maage_block_state_definition **state, const maage_block_definition **block) {
    const maage_block_registry *registry = maage_world_registry(world);
    if (x <= -MAAGE_WORLD_LIMIT || x >= MAAGE_WORLD_LIMIT || z <= -MAAGE_WORLD_LIMIT || z >= MAAGE_WORLD_LIMIT || !maage_world_chunk_loaded(world, maage_world_chunk_coordinate(x), maage_world_chunk_coordinate(z))) return maage_support_status(MAAGE_STATUS_NOOP);
    *state = maage_block_registry_state(registry, maage_world_get(world, x, y, z));
    *block = *state ? maage_block_registry_block(registry, (*state)->block_id) : NULL;
    return maage_support_status(*block ? MAAGE_STATUS_SUCCESS : MAAGE_STATUS_INVARIANT_VIOLATION);
}
static int maage_support_full(const maage_block_definition *block) {
    unsigned int axis;
    if (!block->solid || !block->opaque) return 0;
    for (axis = 0; axis < 3; ++axis)
        if (block->collision_shape.minimum[axis] != 0 || block->collision_shape.maximum[axis] != 1) return 0;
    return 1;
}
maage_status maage_support_rule(const maage_world *world, int x, int y, int z, const maage_block_state_definition *state, int *valid) {
    static const int torch_support[6][3] = {{0, -1, 0}, {-1, 0, 0}, {1, 0, 0}, {0, 0, -1}, {0, 0, 1}, {0, -1, 0}};
    static const int sides[4][2] = {{-1, 0}, {1, 0}, {0, -1}, {0, 1}};
    const maage_block_state_definition *below, *neighbor;
    const maage_block_definition *block;
    unsigned char light;
    unsigned int side;
    maage_status result;
    *valid = 0;
    if (y < 0 || y >= 256) return maage_support_status(MAAGE_STATUS_SUCCESS);
    if (state->block_id == MAAGE_BLOCK_TORCH) {
        if (state->metadata < 1 || state->metadata > 5) return maage_support_status(MAAGE_STATUS_INVARIANT_VIOLATION);
        result = maage_support_block(world, x + torch_support[state->metadata][0], y + torch_support[state->metadata][1], z + torch_support[state->metadata][2], &neighbor, &block);
        if (maage_status_code(result) == MAAGE_STATUS_SUCCESS) *valid = maage_support_full(block);
        return result;
    }
    result = maage_support_block(world, x, y - 1, z, &below, &block);
    if (maage_status_code(result) != MAAGE_STATUS_SUCCESS) return result;
    switch (state->block_id) {
    case MAAGE_BLOCK_TALL_GRASS:
    case MAAGE_BLOCK_DANDELION:
    case MAAGE_BLOCK_POPPY:
        *valid = below->block_id == MAAGE_BLOCK_DIRT || below->block_id == MAAGE_BLOCK_GRASS;
        break;
    case MAAGE_BLOCK_DEAD_BUSH:
        *valid = below->block_id == MAAGE_BLOCK_DIRT || below->block_id == MAAGE_BLOCK_SAND;
        break;
    case MAAGE_BLOCK_CACTUS:
        if (below->block_id != MAAGE_BLOCK_SAND && below->block_id != MAAGE_BLOCK_CACTUS) break;
        for (side = 0; side < 4; ++side) {
            result = maage_support_block(world, x + sides[side][0], y, z + sides[side][1], &neighbor, &block);
            if (maage_status_code(result) != MAAGE_STATUS_SUCCESS) return result;
            if (block->material_solid) return maage_support_status(MAAGE_STATUS_SUCCESS);
        }
        *valid = 1;
        break;
    case MAAGE_BLOCK_SNOW:
        *valid = below->block_id != MAAGE_BLOCK_ICE &&
                 (below->block_id == MAAGE_BLOCK_LEAVES || (below->block_id == MAAGE_BLOCK_SNOW && (below->metadata & 7U) == 7U) ||
                  (block->opaque && block->material_solid));
        break;
    case MAAGE_BLOCK_BROWN_MUSHROOM:
    case MAAGE_BLOCK_RED_MUSHROOM:
        if (!block->opaque) break;
        result = maage_world_sample_light(world, x, y, z, &light);
        if (maage_status_code(result) != MAAGE_STATUS_SUCCESS) return result;
        *valid = (light >> 4) < 13 && (light & 15) < 13;
        break;
    default:
        return maage_support_status(MAAGE_STATUS_INVARIANT_VIOLATION);
    }
    return maage_support_status(MAAGE_STATUS_SUCCESS);
}
maage_status maage_world_can_survive(const maage_world *world, int x, int y, int z, maage_block_state_id state_id, int *valid) {
    const maage_block_state_definition *state;
    const maage_block_definition *block;
    if (!world || !valid) return maage_support_status(MAAGE_STATUS_INVALID_ARGUMENT);
    *valid = 0;
    state = maage_block_registry_state(maage_world_registry(world), state_id);
    block = state ? maage_block_registry_block(maage_world_registry(world), state->block_id) : NULL;
    if (!block) return maage_support_status(MAAGE_STATUS_INVALID_ARGUMENT);
    if (y < 0 || y >= 256) return maage_support_status(MAAGE_STATUS_SUCCESS);
    if (block->survives) return block->survives(world, x, y, z, state, valid);
    *valid = 1;
    return maage_support_status(MAAGE_STATUS_SUCCESS);
}
static maage_status maage_support_schedule(maage_world *world, int x, int y, int z, unsigned int delay) {
    maage_environment_state clock;
    unsigned long low, high;
    if (y < 0 || y >= 256) return maage_support_status(MAAGE_STATUS_NOOP);
    maage_world_environment(world, &clock);
    low = clock.ticks_low;
    high = clock.ticks_high;
    if (delay) {
        low = (low + delay) & 4294967295UL;
        if (low < clock.ticks_low) high = (high + 1UL) & 4294967295UL;
    }
    return maage_world_schedule_update(world, x, y, z, MAAGE_BLOCK_UPDATE_SUPPORT, low, high);
}
maage_status maage_support_changed(maage_world *world, int x, int y, int z, maage_block_state_id replacement) {
    static const int offsets[7][3] = {{0, 0, 0}, {-1, 0, 0}, {1, 0, 0}, {0, -1, 0}, {0, 1, 0}, {0, 0, -1}, {0, 0, 1}};
    const maage_block_state_definition *state;
    const maage_block_definition *block;
    maage_status result;
    int nx, ny, nz;
    unsigned int side;
    for (side = 0; side < 7; ++side) {
        nx = x + offsets[side][0];
        ny = y + offsets[side][1];
        nz = z + offsets[side][2];
        if (ny < 0 || ny >= 256) continue;
        if (!side) {
            state = maage_block_registry_state(maage_world_registry(world), replacement);
            block = state ? maage_block_registry_block(maage_world_registry(world), state->block_id) : NULL;
            result = maage_support_status(block ? MAAGE_STATUS_SUCCESS : MAAGE_STATUS_INVARIANT_VIOLATION);
        } else
            result = maage_support_block(world, nx, ny, nz, &state, &block);
        if (maage_status_failed(result)) return result;
        if (maage_status_code(result) != MAAGE_STATUS_SUCCESS || (!block->survives && !block->falls)) continue;
        result = maage_support_schedule(world, nx, ny, nz, block->falls ? 2U : 0U);
        if (maage_status_failed(result)) return result;
    }
    return maage_support_status(MAAGE_STATUS_SUCCESS);
}
maage_status maage_support_update(maage_world *world, int x, int y, int z) {
    const maage_block_state_definition *state;
    const maage_block_definition *block;
    maage_item_drops drops;
    maage_entities *entities = maage_world_entities(world);
    const maage_item_registry *items = maage_world_item_registry(world);
    unsigned long random;
    unsigned int i;
    double position[3], velocity[3];
    int valid;
    maage_status result = maage_support_block(world, x, y, z, &state, &block);
    if (maage_status_code(result) != MAAGE_STATUS_SUCCESS) return result;
    if (block->falls) {
        const maage_block_state_definition *under;
        const maage_block_definition *support;
        result = maage_support_block(world, x, y - 1, z, &under, &support);
        if (maage_status_code(result) == MAAGE_STATUS_NOOP) return maage_support_schedule(world, x, y, z, 1);
        if (maage_status_failed(result)) return result;
        if (under->block_id != MAAGE_BLOCK_AIR && !under->fluid_kind) return maage_support_status(MAAGE_STATUS_NOOP);
        return maage_entities_fall(world, x, y, z, state->id);
    }
    if (!block->survives) return maage_support_status(MAAGE_STATUS_NOOP);
    result = maage_world_can_survive(world, x, y, z, state->id, &valid);
    if (maage_status_code(result) == MAAGE_STATUS_NOOP) return maage_support_schedule(world, x, y, z, 1);
    if (maage_status_failed(result) || valid) return result;
    memset(&drops, 0, sizeof(drops));
    random = maage_entities_random(entities);
    /* Snow's neighbor callback removes it without harvesting snowballs.
     * Support loss is environmental even when the triggering player is Creative. */
    if (state->block_id != MAAGE_BLOCK_SNOW) {
        result = maage_item_generate_drops(maage_world_registry(world), items, state->id, NULL, &random, &drops);
        if (maage_status_failed(result)) return result;
        result = maage_entities_reserve(entities, drops.count);
        if (maage_status_failed(result)) return result;
    }
    result = maage_world_set(world, x, y, z, state->id, MAAGE_STATE_AIR);
    if (maage_status_code(result) != MAAGE_STATUS_SUCCESS) return result;
    for (i = 0; i < drops.count; ++i) {
        random = (random * 1664525UL + 1013904223UL) & 4294967295UL;
        position[0] = x + 0.15 + (random & 65535UL) / 65535.0 * 0.7;
        random = (random * 1664525UL + 1013904223UL) & 4294967295UL;
        position[1] = y + 0.15 + (random & 65535UL) / 65535.0 * 0.7;
        random = (random * 1664525UL + 1013904223UL) & 4294967295UL;
        position[2] = z + 0.15 + (random & 65535UL) / 65535.0 * 0.7;
        velocity[0] = velocity[2] = 0;
        velocity[1] = 0.2;
        result = maage_entities_spawn(entities, items, &drops.stacks[i], position, velocity, 10);
        if (maage_status_failed(result)) return result;
    }
    return maage_entities_set_random(entities, random);
}
maage_status maage_chunk_support_initialize(maage_chunk *chunk, const maage_block_registry *blocks, unsigned long low, unsigned long high) {
    const maage_block_state_definition *state;
    const maage_block_definition *block;
    maage_scheduled_update update;
    unsigned int cell;
    int changed = 0;
    maage_status result;
    update.kind = MAAGE_BLOCK_UPDATE_SUPPORT;
    update.due_low = low;
    update.due_high = high;
    for (cell = 0; cell < 65536UL; ++cell) {
        if (!chunk->sections[cell / 4096]) {
            cell += 4095;
            continue;
        }
        state = maage_block_registry_state(blocks, maage_chunk_get(chunk, cell % 16, cell / 256, (cell / 16) % 16));
        block = state ? maage_block_registry_block(blocks, state->block_id) : NULL;
        if (!block) return maage_support_status(MAAGE_STATUS_INVARIANT_VIOLATION);
        if (!block->survives && !block->falls) continue;
        update.cell = cell;
        update.due_low = block->falls ? (low + 2UL) & 4294967295UL : low;
        update.due_high = (high + (update.due_low < low ? 1UL : 0UL)) & 4294967295UL;
        result = maage_chunk_schedule_update(chunk, &update);
        if (maage_status_failed(result)) return result;
        if (maage_status_code(result) == MAAGE_STATUS_SUCCESS) changed = 1;
    }
    return maage_support_status(changed ? MAAGE_STATUS_SUCCESS : MAAGE_STATUS_NOOP);
}
