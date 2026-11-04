#include "base_internal.h"
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
static int maage_support_full(const maage_block_definition *block, const maage_block_state_definition *state) {
    unsigned int axis;
    maage_block_shape shape;
    if (!block->opaque || !maage_block_collision_shape(block, state, &shape)) return 0;
    for (axis = 0; axis < 3; ++axis)
        if (shape.minimum[axis] != 0 || shape.maximum[axis] != 1) return 0;
    return 1;
}
static maage_status base_survival(const maage_world *world, int x, int y, int z, const maage_block_state_definition *state, int *valid, unsigned int policy) {
    const maage_base_ids *ids = maage_base_state_references(maage_world_registry(world), state);
    static const int torch_support[6][3] = {{0, -1, 0}, {-1, 0, 0}, {1, 0, 0}, {0, 0, -1}, {0, 0, 1}, {0, -1, 0}};
    static const int sides[4][2] = {{-1, 0}, {1, 0}, {0, -1}, {0, 1}};
    const maage_block_state_definition *below, *neighbor;
    const maage_block_definition *block;
    unsigned char light;
    unsigned int side;
    maage_status result;
    *valid = 0;
    if (y < 0 || y >= 256) return maage_support_status(MAAGE_STATUS_SUCCESS);
    if (policy == 0) {
        result = maage_support_block(world, x, y + 1, z, &neighbor, &block);
        if (maage_status_code(result) == MAAGE_STATUS_SUCCESS) *valid = !block->material_solid;
        return result;
    }
    if (policy == 1) {
        if (maage_base_state_value(maage_world_registry(world), state, ids->property_attachment) < 1 || maage_base_state_value(maage_world_registry(world), state, ids->property_attachment) > 5) return maage_support_status(MAAGE_STATUS_INVARIANT_VIOLATION);
        result = maage_support_block(world, x + torch_support[maage_base_state_value(maage_world_registry(world), state, ids->property_attachment)][0], y + torch_support[maage_base_state_value(maage_world_registry(world), state, ids->property_attachment)][1], z + torch_support[maage_base_state_value(maage_world_registry(world), state, ids->property_attachment)][2], &neighbor, &block);
        if (maage_status_code(result) == MAAGE_STATUS_SUCCESS) *valid = maage_support_full(block, neighbor);
        return result;
    }
    result = maage_support_block(world, x, y - 1, z, &below, &block);
    if (maage_status_code(result) != MAAGE_STATUS_SUCCESS) return result;
    if (policy == 2) {
        maage_environment_state environment;
        *valid = maage_base_state_tag(ids, below, ids->tag_farmland);
        if (!*valid) return maage_support_status(MAAGE_STATUS_SUCCESS);
        result = maage_world_sample_light(world, x, y, z, &light);
        if (maage_status_code(result) != MAAGE_STATUS_SUCCESS) return result;
        maage_world_environment(world, &environment);
        *valid = (light >> 4) == 15 || (light & 15) >= 8 || (int) (light >> 4) - (int) maage_environment_sky_subtracted(&environment) >= 8;
    } else if (policy == 3) {
        *valid = maage_base_state_tag(ids, below, ids->tag_soil);
    } else if (policy == 4) {
        *valid = below->block_id == ids->block_dirt || maage_base_state_tag(ids, below, ids->tag_sand);
    } else if (policy == 5) {
        if (!maage_base_state_tag(ids, below, ids->tag_sand) && below->block_id != state->block_id) return maage_support_status(MAAGE_STATUS_SUCCESS);
        for (side = 0; side < 4; ++side) {
            result = maage_support_block(world, x + sides[side][0], y, z + sides[side][1], &neighbor, &block);
            if (maage_status_code(result) != MAAGE_STATUS_SUCCESS) return result;
            if (block->material_solid) return maage_support_status(MAAGE_STATUS_SUCCESS);
        }
        *valid = 1;
    } else if (policy == 6) {
        *valid = below->block_id != ids->block_ice &&
                 (maage_base_state_tag(ids, below, ids->tag_leaves) ||
                  (block->opaque && block->material_solid));
    } else if (policy == 7) {
        if (!block->opaque) return maage_support_status(MAAGE_STATUS_SUCCESS);
        result = maage_world_sample_light(world, x, y, z, &light);
        if (maage_status_code(result) != MAAGE_STATUS_SUCCESS) return result;
        *valid = (light >> 4) < 13 && (light & 15) < 13;
    } else {
        return maage_support_status(MAAGE_STATUS_INVARIANT_VIOLATION);
    }
    return maage_support_status(MAAGE_STATUS_SUCCESS);
}

maage_status maage_support_farmland(const maage_world *world, int x, int y, int z, const maage_block_state_definition *state, int *valid) {
    return base_survival(world, x, y, z, state, valid, 0);
}

maage_status maage_support_torch(const maage_world *world, int x, int y, int z, const maage_block_state_definition *state, int *valid) {
    return base_survival(world, x, y, z, state, valid, 1);
}

maage_status maage_support_wheat(const maage_world *world, int x, int y, int z, const maage_block_state_definition *state, int *valid) {
    return base_survival(world, x, y, z, state, valid, 2);
}

maage_status maage_support_plant(const maage_world *world, int x, int y, int z, const maage_block_state_definition *state, int *valid) {
    return base_survival(world, x, y, z, state, valid, 3);
}

maage_status maage_support_dead_bush(const maage_world *world, int x, int y, int z, const maage_block_state_definition *state, int *valid) {
    return base_survival(world, x, y, z, state, valid, 4);
}

maage_status maage_support_cactus(const maage_world *world, int x, int y, int z, const maage_block_state_definition *state, int *valid) {
    return base_survival(world, x, y, z, state, valid, 5);
}

maage_status maage_support_snow(const maage_world *world, int x, int y, int z, const maage_block_state_definition *state, int *valid) {
    return base_survival(world, x, y, z, state, valid, 6);
}

maage_status maage_support_mushroom(const maage_world *world, int x, int y, int z, const maage_block_state_definition *state, int *valid) {
    return base_survival(world, x, y, z, state, valid, 7);
}
