#include "world_internal.h"
#include "maage/entities.h"

#define MAAGE_LEAF_EDGE 9
#define MAAGE_LEAF_CELLS 729
#define MAAGE_LEAF_CENTER 364

static maage_status maage_leaves_noop(void) {
    return maage_status_make(MAAGE_STATUS_NOOP, 0, MAAGE_OPERATION_TICK, 0);
}

maage_status maage_leaves_changed(maage_world *world, int x, int y, int z,
                                  const maage_block_state_definition *before, const maage_block_state_definition *after) {
    const maage_block_registry *blocks = maage_world_registry(world);
    const maage_block_state_definition *leaf;
    maage_block_state_id marked;
    maage_status result;
    int radius, dx, dy, dz, nx, ny, nz;
    if (before->block_id == after->block_id) return maage_leaves_noop();
    if (before->block_id == MAAGE_BLOCK_LOG)
        radius = 4;
    else if (before->block_id == MAAGE_BLOCK_LEAVES)
        radius = 1;
    else
        return maage_leaves_noop();
    /* The mutation already owns an edit batch. Flag-only recursive mutations
     * never trigger removal propagation, so work is bounded to this cube. */
    for (dx = -radius; dx <= radius; ++dx)
        for (dz = -radius; dz <= radius; ++dz) {
            nx = x + dx;
            nz = z + dz;
            if (!maage_world_chunk_loaded(world, maage_world_chunk_coordinate(nx), maage_world_chunk_coordinate(nz))) continue;
            for (dy = -radius; dy <= radius; ++dy) {
                ny = y + dy;
                if (ny < 0 || ny >= MAAGE_WORLD_HEIGHT) continue;
                leaf = maage_block_registry_state(blocks, maage_world_get(world, nx, ny, nz));
                if (!leaf) return maage_status_make(MAAGE_STATUS_INVARIANT_VIOLATION, MAAGE_CONTEXT_WORLD, MAAGE_OPERATION_MUTATE, 0);
                if (leaf->block_id != MAAGE_BLOCK_LEAVES ||
                    (leaf->metadata & (MAAGE_LEAF_PERSISTENT | MAAGE_LEAF_CHECK_DECAY))) continue;
                result = maage_block_leaves_state(leaf->metadata | MAAGE_LEAF_CHECK_DECAY, &marked);
                if (!maage_status_failed(result)) result = maage_world_set(world, nx, ny, nz, leaf->id, marked);
                if (maage_status_failed(result)) return result;
            }
        }
    return maage_status_make(MAAGE_STATUS_SUCCESS, 0, MAAGE_OPERATION_MUTATE, 0);
}

maage_status maage_random_tick_leaves(maage_world *world, int x, int y, int z,
                                      const maage_block_state_definition *state, maage_random_tick_context *context) {
    static const int steps[6][3] = {{-1, 0, 0}, {1, 0, 0}, {0, -1, 0}, {0, 1, 0}, {0, 0, -1}, {0, 0, 1}};
    const maage_block_registry *blocks = maage_world_registry(world);
    const maage_block_state_definition *neighbor;
    signed char support[MAAGE_LEAF_CELLS];
    unsigned short queue[MAAGE_LEAF_CELLS];
    unsigned int head = 0, tail = 0, side;
    int dx, dy, dz, ix, iy, iz, nx, ny, nz, cell, next, depth;
    maage_block_state_id checked;
    maage_item_drops drops;
    unsigned long drop_random;
    size_t i;
    maage_status result, mutation;
    if ((state->metadata & MAAGE_LEAF_PERSISTENT) || !(state->metadata & MAAGE_LEAF_CHECK_DECAY))
        return maage_leaves_noop();
    /* Require the radius-five neighborhood before searching the radius-four
     * support cube. Outside vertical world bounds is known air. */
    if (x - 5 <= -MAAGE_WORLD_LIMIT || x + 5 >= MAAGE_WORLD_LIMIT ||
        z - 5 <= -MAAGE_WORLD_LIMIT || z + 5 >= MAAGE_WORLD_LIMIT) return maage_leaves_noop();
    for (iz = maage_world_chunk_coordinate(z - 5); iz <= maage_world_chunk_coordinate(z + 5); ++iz)
        for (ix = maage_world_chunk_coordinate(x - 5); ix <= maage_world_chunk_coordinate(x + 5); ++ix)
            if (!maage_world_chunk_loaded(world, ix, iz)) return maage_leaves_noop();
    for (dy = -4; dy <= 4; ++dy)
        for (dz = -4; dz <= 4; ++dz)
            for (dx = -4; dx <= 4; ++dx) {
                cell = ((dy + 4) * MAAGE_LEAF_EDGE + dz + 4) * MAAGE_LEAF_EDGE + dx + 4;
                neighbor = maage_block_registry_state(blocks, maage_world_get(world, x + dx, y + dy, z + dz));
                if (!neighbor) return maage_status_make(MAAGE_STATUS_INVARIANT_VIOLATION, MAAGE_CONTEXT_WORLD, MAAGE_OPERATION_TICK, 0);
                support[cell] = neighbor->block_id == MAAGE_BLOCK_LEAVES ? -2 : -1;
                if (neighbor->block_id == MAAGE_BLOCK_LOG) {
                    support[cell] = 0;
                    queue[tail++] = (unsigned short) cell;
                }
            }
    /* Multi-source BFS carries support only through a connected leaf path,
     * with at most four face-adjacent edges. */
    while (head < tail && support[MAAGE_LEAF_CENTER] < 0) {
        cell = queue[head++];
        depth = support[cell];
        if (depth == 4) continue;
        ix = cell % MAAGE_LEAF_EDGE;
        iz = (cell / MAAGE_LEAF_EDGE) % MAAGE_LEAF_EDGE;
        iy = cell / (MAAGE_LEAF_EDGE * MAAGE_LEAF_EDGE);
        for (side = 0; side < 6; ++side) {
            nx = ix + steps[side][0];
            ny = iy + steps[side][1];
            nz = iz + steps[side][2];
            if (nx < 0 || nx >= MAAGE_LEAF_EDGE || ny < 0 || ny >= MAAGE_LEAF_EDGE || nz < 0 || nz >= MAAGE_LEAF_EDGE) continue;
            next = (ny * MAAGE_LEAF_EDGE + nz) * MAAGE_LEAF_EDGE + nx;
            if (support[next] != -2) continue;
            support[next] = (signed char) (depth + 1);
            queue[tail++] = (unsigned short) next;
        }
    }
    if (support[MAAGE_LEAF_CENTER] >= 0) {
        result = maage_block_leaves_state(state->metadata & ~MAAGE_LEAF_CHECK_DECAY, &checked);
        return maage_status_failed(result) ? result : maage_world_set(world, x, y, z, state->id, checked);
    }
    /* Reuse the established ordinary-drop rules without consuming the player
     * item-entity RNG. Only this callback's persistent behavior stream advances. */
    drop_random = (unsigned long) maage_random_tick_next(context, UINT_MAX) + 1UL;
    result = maage_item_generate_drops(blocks, maage_world_item_registry(world), state->id, NULL, &drop_random, &drops);
    if (!maage_status_failed(result)) result = maage_entities_reserve(maage_world_entities(world), drops.count);
    if (maage_status_failed(result)) return result;
    mutation = maage_world_set(world, x, y, z, state->id, MAAGE_STATE_AIR);
    if (maage_status_code(mutation) != MAAGE_STATUS_SUCCESS &&
        (maage_status_code(mutation) == MAAGE_STATUS_INTERRUPTED ||
         !maage_world_chunk_loaded(world, maage_world_chunk_coordinate(x), maage_world_chunk_coordinate(z)) ||
         maage_world_get(world, x, y, z) != MAAGE_STATE_AIR)) return mutation;
    /* A post-mutation scheduling failure must not discard reserved quantities.
     * Preserve that original result after completing the infallible spawns. */
    for (i = 0; i < drops.count; ++i) {
        result = maage_random_tick_spawn_drop(world, x, y, z, &drops.stacks[i], context);
        if (maage_status_failed(result)) return result;
    }
    return mutation;
}
