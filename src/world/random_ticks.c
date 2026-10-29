#include "world_internal.h"
#include "maage/entities.h"
#include <string.h>

static unsigned long maage_random_tick_seed(maage_world_seed seed, unsigned long domain)
{
    unsigned long value;
    value = ((seed.low ^ domain) * 1664525UL + seed.high + 1013904223UL) & 0xffffffffUL;
    value ^= value >> 16;
    value = (value * 2246822519UL) & 0xffffffffUL;
    value ^= value >> 13;
    return value & 0xffffffffUL;
}
maage_status maage_random_tick_default(maage_world_seed seed, maage_random_tick_state *state)
{
    if (!state || seed.low > 0xffffffffUL || seed.high > 0xffffffffUL)
        return maage_status_make(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_CONTEXT_WORLD, MAAGE_OPERATION_INITIALIZE, 0);
    state->selection = maage_random_tick_seed(seed, 0x41c64e6dUL);
    state->random = maage_random_tick_seed(seed, 0x85ebca6bUL);
    if (!state->random) state->random = 1;
    return maage_status_make(MAAGE_STATUS_SUCCESS, 0, MAAGE_OPERATION_INITIALIZE, 0);
}
int maage_random_tick_state_valid(const maage_random_tick_state *state)
{
    /* Zero is a valid coordinate LCG state, but not a valid xorshift state. */
    return state && state->selection <= 0xffffffffUL && state->random && state->random <= 0xffffffffUL;
}

static maage_status maage_random_noop(void)
{
    return maage_status_make(MAAGE_STATUS_NOOP, 0, MAAGE_OPERATION_TICK, 0);
}

unsigned int maage_random_tick_next(maage_random_tick_context *context, unsigned int bound)
{
    unsigned long value, limit = 0xffffffffUL - 0xffffffffUL % bound;
    do {
        value = context->state.random;
        value ^= (value << 13) & 0xffffffffUL;
        value ^= value >> 17;
        value ^= (value << 5) & 0xffffffffUL;
        context->state.random = value & 0xffffffffUL;
        /* Xorshift's nonzero state domain has 2^32-1 values. */
        value = context->state.random - 1UL;
    } while (value >= limit);
    return (unsigned int)(value % bound);
}

/* Unlike visual get(), these queries never mistake missing columns for air. */
static maage_status maage_random_block(const maage_world *world, int x, int y, int z,
    const maage_block_state_definition **state, const maage_block_definition **block)
{
    const maage_block_registry *registry = maage_world_registry(world);
    if (x <= -MAAGE_WORLD_LIMIT || x >= MAAGE_WORLD_LIMIT ||
        z <= -MAAGE_WORLD_LIMIT || z >= MAAGE_WORLD_LIMIT ||
        !maage_world_chunk_loaded(world, maage_world_chunk_coordinate(x), maage_world_chunk_coordinate(z)))
        return maage_random_noop();
    *state = maage_block_registry_state(registry, maage_world_get(world, x, y, z));
    *block = *state ? maage_block_registry_block(registry, (*state)->block_id) : NULL;
    return maage_status_make(*block ? MAAGE_STATUS_SUCCESS : MAAGE_STATUS_INVARIANT_VIOLATION,
        MAAGE_CONTEXT_WORLD, MAAGE_OPERATION_TICK, 0);
}

static int maage_random_light_value(unsigned char light, const maage_random_tick_context *context)
{
    int sky = (int)(light >> 4) - (int)context->sky_subtracted;
    int block = light & 15;
    return sky > block ? sky : block;
}

/* Capacity and stack validity are preflighted before the block mutation. */
maage_status maage_random_tick_spawn_drop(maage_world *world, int x, int y, int z,
    const maage_item_stack *drop, maage_random_tick_context *context)
{
    double position[3], velocity[3] = {0, 0.2, 0};
    position[0] = x + 0.15 + maage_random_tick_next(context, 701) / 1000.0;
    position[1] = y + 0.15 + maage_random_tick_next(context, 701) / 1000.0;
    position[2] = z + 0.15 + maage_random_tick_next(context, 701) / 1000.0;
    return maage_entities_spawn(maage_world_entities(world), maage_world_item_registry(world),
        drop, position, velocity, 10);
}

static maage_status maage_random_grass_light(const maage_world *world, int x, int y, int z,
    const maage_block_definition *cover, const maage_random_tick_context *context, int *value)
{
    static const int offsets[5][3] = {{0,1,0},{1,0,0},{-1,0,0},{0,0,1},{0,0,-1}};
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

maage_status maage_random_tick_grass(maage_world *world, int x, int y, int z,
    const maage_block_state_definition *state, maage_random_tick_context *context)
{
    const maage_block_state_definition *cover, *destination;
    const maage_block_definition *block;
    unsigned int attempt;
    int brightness, nx, ny, nz;
    maage_status result, outcome = maage_random_noop();
    result = maage_random_block(world, x, y + 1, z, &cover, &block);
    if (maage_status_code(result) != MAAGE_STATUS_SUCCESS) return result;
    result = maage_random_grass_light(world, x, y + 1, z, block, context, &brightness);
    if (maage_status_code(result) != MAAGE_STATUS_SUCCESS) return result;
    if (brightness < 4 && block->light_opacity > 2)
        return maage_world_set(world, x, y, z, state->id, MAAGE_STATE_DIRT);
    if (brightness < 9) return outcome;
    for (attempt = 0; attempt < 4; ++attempt) {
        nx = x + (int)maage_random_tick_next(context, 3) - 1;
        ny = y + (int)maage_random_tick_next(context, 5) - 3;
        nz = z + (int)maage_random_tick_next(context, 3) - 1;
        if (ny < 0 || ny >= MAAGE_WORLD_HEIGHT) continue;
        result = maage_random_block(world, nx, ny, nz, &destination, &block);
        if (maage_status_failed(result)) return result;
        if (maage_status_code(result) != MAAGE_STATUS_SUCCESS ||
            destination->block_id != MAAGE_BLOCK_DIRT || destination->metadata != 0) continue;
        result = maage_random_block(world, nx, ny + 1, nz, &cover, &block);
        if (maage_status_failed(result)) return result;
        if (maage_status_code(result) != MAAGE_STATUS_SUCCESS || block->light_opacity > 2) continue;
        result = maage_random_grass_light(world, nx, ny + 1, nz, block, context, &brightness);
        if (maage_status_failed(result)) return result;
        if (maage_status_code(result) != MAAGE_STATUS_SUCCESS || brightness < 4) continue;
        result = maage_world_set(world, nx, ny, nz, destination->id, MAAGE_STATE_GRASS);
        if (maage_status_failed(result)) return result;
        outcome = result;
    }
    return outcome;
}

maage_status maage_random_tick_cactus(maage_world *world, int x, int y, int z,
    const maage_block_state_definition *state, maage_random_tick_context *context)
{
    static const int sides[4][2] = {{-1,0},{1,0},{0,-1},{0,1}};
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
    if (neighbor->block_id != MAAGE_BLOCK_AIR) return maage_random_noop();
    for (height = 1; height < 3; ++height) {
        result = maage_random_block(world, x, y - (int)height, z, &neighbor, &block);
        if (maage_status_code(result) != MAAGE_STATUS_SUCCESS) return result;
        if (neighbor->block_id != MAAGE_BLOCK_CACTUS) break;
    }
    if (height >= 3) return maage_random_noop();
    if (state->metadata < 15)
        return maage_world_set(world, x, y, z, state->id, MAAGE_STATE_CACTUS_AGE_BASE + state->metadata);
    /* The reference checks the newly grown block's sides through its neighbor
     * callback. Preflight the same result, without transient published geometry. */
    for (side = 0; side < 4; ++side) {
        result = maage_random_block(world, x + sides[side][0], y + 1, z + sides[side][1], &neighbor, &block);
        if (maage_status_code(result) != MAAGE_STATUS_SUCCESS) return result;
        if (block->material_solid) obstructed = 1;
    }
    if (obstructed) {
        block = maage_block_registry_block(maage_world_registry(world), state->block_id);
        if (!block) return maage_status_make(MAAGE_STATUS_INVARIANT_VIOLATION, MAAGE_CONTEXT_WORLD, MAAGE_OPERATION_TICK, 0);
        memset(&drop, 0, sizeof(drop)); drop.item_id = block->drop_item_id; drop.count = block->drop_count;
        if (!maage_item_stack_valid(maage_world_item_registry(world), &drop))
            return maage_status_make(MAAGE_STATUS_INVARIANT_VIOLATION, MAAGE_CONTEXT_WORLD, MAAGE_OPERATION_TICK, 0);
        result = maage_entities_reserve(maage_world_entities(world), 1);
        if (maage_status_failed(result)) return result;
    } else {
        result = maage_world_set(world, x, y + 1, z, MAAGE_STATE_AIR, MAAGE_STATE_CACTUS);
        if (maage_status_failed(result)) return result;
    }
    result = maage_world_set(world, x, y, z, state->id, MAAGE_STATE_CACTUS);
    if (maage_status_failed(result) || !obstructed) return result;
    return maage_random_tick_spawn_drop(world, x, y + 1, z, &drop, context);
}

maage_status maage_random_tick_bush(maage_world *world, int x, int y, int z,
    const maage_block_state_definition *state, maage_random_tick_context *context)
{
    const maage_block_state_definition *support;
    const maage_block_definition *block;
    maage_item_stack drop;
    maage_status result;
    result = maage_random_block(world, x, y - 1, z, &support, &block);
    if (maage_status_code(result) != MAAGE_STATUS_SUCCESS) return result;
    if (state->block_id == MAAGE_BLOCK_DEAD_BUSH) {
        if (support->block_id == MAAGE_BLOCK_SAND || support->block_id == MAAGE_BLOCK_DIRT) return maage_random_noop();
    } else if (support->block_id == MAAGE_BLOCK_GRASS || support->block_id == MAAGE_BLOCK_DIRT) return maage_random_noop();
    /* BlockBush/BlockTallGrass in 1.7.10 check substrate here, without a light
     * threshold. Hardened clay and farmland are absent from the catalog. */
    memset(&drop, 0, sizeof(drop));
    if (state->block_id == MAAGE_BLOCK_TALL_GRASS) {
        if (!maage_random_tick_next(context, 8)) { drop.item_id = MAAGE_ITEM_SEEDS; drop.count = 1; }
    } else if (state->block_id != MAAGE_BLOCK_DEAD_BUSH) {
        block = maage_block_registry_block(maage_world_registry(world), state->block_id);
        if (!block) return maage_status_make(MAAGE_STATUS_INVARIANT_VIOLATION, MAAGE_CONTEXT_WORLD, MAAGE_OPERATION_TICK, 0);
        drop.item_id = block->drop_item_id; drop.count = block->drop_count;
    }
    if (drop.count) {
        if (!maage_item_stack_valid(maage_world_item_registry(world), &drop))
            return maage_status_make(MAAGE_STATUS_INVARIANT_VIOLATION, MAAGE_CONTEXT_WORLD, MAAGE_OPERATION_TICK, 0);
        result = maage_entities_reserve(maage_world_entities(world), 1);
        if (maage_status_failed(result)) return result;
    }
    result = maage_world_set(world, x, y, z, state->id, MAAGE_STATE_AIR);
    if (maage_status_failed(result) || !drop.count) return result;
    return maage_random_tick_spawn_drop(world, x, y, z, &drop, context);
}

static maage_status maage_random_mushroom_site(const maage_world *world,
    int x, int y, int z, int *valid)
{
    const maage_block_state_definition *state;
    const maage_block_definition *block;
    unsigned char light;
    maage_status result;
    *valid = 0;
    if (y <= 0 || y >= MAAGE_WORLD_HEIGHT) return maage_random_noop();
    result = maage_random_block(world, x, y, z, &state, &block);
    if (maage_status_code(result) != MAAGE_STATUS_SUCCESS || state->block_id != MAAGE_BLOCK_AIR) return result;
    result = maage_random_block(world, x, y - 1, z, &state, &block);
    if (maage_status_code(result) != MAAGE_STATUS_SUCCESS || !block->opaque) return result;
    result = maage_world_sample_light(world, x, y, z, &light);
    /* getFullBlockLightValue ignores daylight subtraction. The current catalog
     * has no mycelium or podzol, which would bypass this light requirement. */
    if (maage_status_code(result) == MAAGE_STATUS_SUCCESS)
        *valid = (light >> 4) < 13 && (light & 15) < 13;
    return result;
}

maage_status maage_random_tick_mushroom(maage_world *world, int x, int y, int z,
    const maage_block_state_definition *state, maage_random_tick_context *context)
{
    const maage_block_state_definition *neighbor;
    const maage_block_definition *block;
    int dx, dy, dz, count = 0, attempt, nx, ny, nz, valid;
    maage_status result;
    if (maage_random_tick_next(context, 25)) return maage_random_noop();
    /* The reference's bounded 9x3x9 density neighborhood, once per 25 callbacks. */
    for (dx = -4; dx <= 4; ++dx) for (dz = -4; dz <= 4; ++dz) for (dy = -1; dy <= 1; ++dy) {
        result = maage_random_block(world, x + dx, y + dy, z + dz, &neighbor, &block);
        if (maage_status_code(result) != MAAGE_STATUS_SUCCESS) return result;
        if (neighbor->block_id == state->block_id && ++count >= 5) return maage_random_noop();
    }
    for (attempt = 0; attempt < 5; ++attempt) {
        nx = x + (int)maage_random_tick_next(context, 3) - 1;
        ny = y + (int)maage_random_tick_next(context, 2);
        ny -= (int)maage_random_tick_next(context, 2);
        nz = z + (int)maage_random_tick_next(context, 3) - 1;
        result = maage_random_mushroom_site(world, nx, ny, nz, &valid);
        if (maage_status_failed(result)) return result;
        if (attempt == 4) return valid ? maage_world_set(world, nx, ny, nz, MAAGE_STATE_AIR,
            state->block_id == MAAGE_BLOCK_BROWN_MUSHROOM ? MAAGE_STATE_BROWN_MUSHROOM : MAAGE_STATE_RED_MUSHROOM) : maage_random_noop();
        if (valid) { x = nx; y = ny; z = nz; }
    }
    return maage_random_noop();
}

maage_status maage_random_tick_snow(maage_world *world, int x, int y, int z,
    const maage_block_state_definition *state, maage_random_tick_context *context)
{
    unsigned char light;
    maage_status result = maage_world_sample_light(world, x, y, z, &light);
    (void)context;
    if (maage_status_code(result) != MAAGE_STATUS_SUCCESS) return result;
    return (light & 15) > 11 ? maage_world_set(world, x, y, z, state->id, MAAGE_STATE_AIR) : maage_random_noop();
}

maage_status maage_random_tick_ice(maage_world *world, int x, int y, int z,
    const maage_block_state_definition *state, maage_random_tick_context *context)
{
    const maage_block_definition *block = maage_block_registry_block(maage_world_registry(world), state->block_id);
    unsigned char light;
    maage_status result = maage_world_sample_light(world, x, y, z, &light);
    (void)context;
    if (maage_status_code(result) != MAAGE_STATUS_SUCCESS) return result;
    if (!block) return maage_status_make(MAAGE_STATUS_INVARIANT_VIOLATION, MAAGE_CONTEXT_WORLD, MAAGE_OPERATION_TICK, 0);
    return (int)(light & 15) > 11 - (int)block->light_opacity ?
        maage_world_set(world, x, y, z, state->id, MAAGE_STATE_WATER) : maage_random_noop();
}
