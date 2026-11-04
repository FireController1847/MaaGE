#include "world_internal.h"
#include <stdlib.h>
#include <string.h>

static maage_status support_result(unsigned char code) {
    return maage_status_make(code, MAAGE_CONTEXT_WORLD, MAAGE_OPERATION_UPDATE, 0);
}
maage_status maage_chunk_reserve_change(maage_chunk *chunk) {
    maage_block_change *grown;
    size_t capacity;
    if (chunk->change_count < chunk->change_capacity) return support_result(MAAGE_STATUS_NOOP);
    capacity = chunk->change_capacity ? chunk->change_capacity * 2 : 8;
    if (capacity <= chunk->change_capacity || capacity > (size_t) - 1 / sizeof(*grown)) return support_result(MAAGE_STATUS_ALLOCATION_FAILED);
    grown = (maage_block_change *) realloc(chunk->changes, capacity * sizeof(*grown));
    if (!grown) return support_result(MAAGE_STATUS_ALLOCATION_FAILED);
    chunk->changes = grown;
    chunk->change_capacity = capacity;
    return support_result(MAAGE_STATUS_SUCCESS);
}
static maage_status support_rule(const maage_world *world, int x, int y, int z, const maage_block_state_definition *state, maage_block_support_rule *rule) {
    const maage_block_definition *block = maage_block_registry_block(maage_world_registry(world), state->block_id);
    maage_status result;
    memset(rule, 0, sizeof(*rule));
    rule->mode = MAAGE_SUPPORT_ALL;
    if (!block->survives) return support_result(MAAGE_STATUS_SUCCESS);
    result = block->survives(world, x, y, z, state, rule);
    if (maage_status_code(result) != MAAGE_STATUS_SUCCESS) return result;
    if ((rule->faces & ~63U) || (rule->faces && !rule->test) || (rule->mode != MAAGE_SUPPORT_ALL && rule->mode != MAAGE_SUPPORT_ANY)) return support_result(MAAGE_STATUS_INVARIANT_VIOLATION);
    return result;
}
static maage_status support_evaluate(const maage_world *world, int x, int y, int z, const maage_block_state_definition *state, const maage_block_support_rule *rule, int *valid) {
    static const int offsets[MAAGE_FACE_COUNT][3] = {{-1, 0, 0}, {1, 0, 0}, {0, -1, 0}, {0, 1, 0}, {0, 0, -1}, {0, 0, 1}};
    const maage_block_state_definition *neighbor;
    maage_status result;
    unsigned int face;
    int nx, ny, nz, accepted, waiting = 0;
    *valid = !rule->faces || rule->mode == MAAGE_SUPPORT_ALL;
    for (face = 0; face < MAAGE_FACE_COUNT; ++face) {
        if (!(rule->faces & (1U << face))) continue;
        nx = x + offsets[face][0];
        ny = y + offsets[face][1];
        nz = z + offsets[face][2];
        /* Out-of-world cells use the ordinary World query. Missing horizontal
         * columns are unknown and never cause a load or a false predicate. */
        if (ny >= 0 && ny < MAAGE_WORLD_HEIGHT && nx > -MAAGE_WORLD_LIMIT && nx < MAAGE_WORLD_LIMIT && nz > -MAAGE_WORLD_LIMIT && nz < MAAGE_WORLD_LIMIT && !maage_world_chunk_loaded(world, maage_world_chunk_coordinate(nx), maage_world_chunk_coordinate(nz))) {
            waiting = 1;
            continue;
        }
        neighbor = maage_block_registry_state(maage_world_registry(world), maage_world_get(world, nx, ny, nz));
        if (!neighbor) return support_result(MAAGE_STATUS_INVARIANT_VIOLATION);
        accepted = 0;
        result = rule->test(rule->context, world, x, y, z, state, face, neighbor, &accepted);
        if (maage_status_failed(result) || maage_status_is_warning(result)) return result;
        if (maage_status_code(result) == MAAGE_STATUS_NOOP) {
            waiting = 1;
            continue;
        }
        if ((rule->mode == MAAGE_SUPPORT_ALL && !accepted) || (rule->mode == MAAGE_SUPPORT_ANY && accepted)) {
            *valid = accepted;
            return support_result(MAAGE_STATUS_SUCCESS);
        }
    }
    return support_result(waiting ? MAAGE_STATUS_NOOP : MAAGE_STATUS_SUCCESS);
}
maage_status maage_world_can_survive(const maage_world *world, int x, int y, int z, maage_block_state_id state_id, int *valid) {
    const maage_block_state_definition *state;
    maage_block_support_rule rule;
    maage_status result;
    if (!world || !valid) return support_result(MAAGE_STATUS_INVALID_ARGUMENT);
    *valid = 0;
    state = maage_block_registry_state(maage_world_registry(world), state_id);
    if (!state) return support_result(MAAGE_STATUS_INVALID_ARGUMENT);
    if (y < 0 || y >= MAAGE_WORLD_HEIGHT) return support_result(MAAGE_STATUS_SUCCESS);
    result = support_rule(world, x, y, z, state, &rule);
    return maage_status_code(result) == MAAGE_STATUS_SUCCESS ? support_evaluate(world, x, y, z, state, &rule, valid) : result;
}
maage_status maage_world_support_changed(maage_world *world, int x, int y, int z, unsigned int face, int *deferred) {
    const maage_block_registry *blocks = maage_world_registry(world);
    const maage_block_state_definition *state = maage_block_registry_state(blocks, maage_world_get(world, x, y, z));
    const maage_block_definition *block = state ? maage_block_registry_block(blocks, state->block_id) : NULL;
    maage_block_support_rule rule;
    maage_status result;
    int valid;
    if (!block) return support_result(MAAGE_STATUS_INVARIANT_VIOLATION);
    if (!block->survives) return support_result(MAAGE_STATUS_NOOP);
    result = support_rule(world, x, y, z, state, &rule);
    if (!maage_status_failed(result) && maage_status_code(result) != MAAGE_STATUS_SUCCESS) *deferred = 1;
    if (maage_status_code(result) != MAAGE_STATUS_SUCCESS) return result;
    if (face < MAAGE_FACE_COUNT && !(rule.faces & (1U << face))) return support_result(MAAGE_STATUS_NOOP);
    result = support_evaluate(world, x, y, z, state, &rule, &valid);
    if (!maage_status_failed(result) && maage_status_code(result) != MAAGE_STATUS_SUCCESS) *deferred = 1;
    if (maage_status_code(result) != MAAGE_STATUS_SUCCESS || valid) return result;
    return block->support_lost(world, x, y, z, state);
}
