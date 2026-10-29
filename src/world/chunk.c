#include "world_internal.h"
#include <stdlib.h>

int maage_world_chunk_coordinate(int coordinate) {
    int quotient = coordinate / MAAGE_CHUNK_EDGE;
    if (coordinate % MAAGE_CHUNK_EDGE < 0) --quotient;
    return quotient;
}
int maage_world_local_coordinate(int coordinate) {
    int local = coordinate % MAAGE_CHUNK_EDGE;
    return local < 0 ? local + MAAGE_CHUNK_EDGE : local;
}
size_t maage_chunk_cell(int x, int y, int z) {
    return (size_t) (((y % 16) * 16 + z) * 16 + x);
}
maage_status maage_chunk_create(int x, int z, maage_chunk **output) {
    maage_chunk *chunk;
    if (!output) return maage_status_make(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_CONTEXT_WORLD, MAAGE_OPERATION_CREATE, 2);
    *output = NULL;
    chunk = (maage_chunk *) calloc(1, sizeof(*chunk));
    if (chunk) {
        chunk->x = x;
        chunk->z = z;
    }
    *output = chunk;
    return maage_status_make(chunk ? MAAGE_STATUS_SUCCESS : MAAGE_STATUS_ALLOCATION_FAILED, MAAGE_CONTEXT_WORLD, MAAGE_OPERATION_CREATE, 0);
}
maage_status maage_chunk_destroy(maage_chunk *chunk) {
    int section;
    if (!chunk) return maage_status_make(MAAGE_STATUS_NOOP, 0, MAAGE_OPERATION_DESTROY, 0);
    for (section = 0; section < 16; ++section)
        free(chunk->sections[section]);
    {
        size_t i;
        for (i = 0; i < chunk->entity_count; ++i)
            free(chunk->entities[i].data);
    }
    free(chunk->entities);
    free(chunk->updates);
    free(chunk->update_index);
    free(chunk->direct_sky);
    free(chunk);
    return maage_status_make(MAAGE_STATUS_SUCCESS, 0, MAAGE_OPERATION_DESTROY, 0);
}
maage_block_state_id maage_chunk_get(const maage_chunk *chunk, int x, int y, int z) {
    if (!chunk || x < 0 || x >= 16 || z < 0 || z >= 16 || y < 0 || y >= 256 || !chunk->sections[y / 16]) return MAAGE_STATE_AIR;
    return chunk->sections[y / 16]->states[maage_chunk_cell(x, y, z)];
}
maage_status maage_chunk_set(maage_chunk *chunk, int x, int y, int z, maage_block_state_id state) {
    maage_section *section;
    size_t cell;
    if (!chunk || x < 0 || x >= 16 || z < 0 || z >= 16 || y < 0 || y >= 256) return maage_status_make(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_CONTEXT_WORLD, MAAGE_OPERATION_SET, 0);
    section = chunk->sections[y / 16];
    if (!section && state == MAAGE_STATE_AIR) return maage_status_make(MAAGE_STATUS_NOOP, 0, MAAGE_OPERATION_SET, 0);
    if (!section) {
        section = (maage_section *) calloc(1, sizeof(*section));
        if (!section) return maage_status_make(MAAGE_STATUS_ALLOCATION_FAILED, MAAGE_CONTEXT_WORLD, MAAGE_OPERATION_SET, 0);
        if (chunk->direct_sky)
            for (cell = 0; cell < 4096; ++cell)
                section->light[cell] = (unsigned char) (chunk->direct_sky[(size_t) (y / 16) * 4096 + cell] << 4);
        chunk->sections[y / 16] = section;
    }
    if (section->states[maage_chunk_cell(x, y, z)] == state) return maage_status_make(MAAGE_STATUS_NOOP, 0, MAAGE_OPERATION_SET, 0);
    section->states[maage_chunk_cell(x, y, z)] = state;
    return maage_status_make(MAAGE_STATUS_SUCCESS, 0, MAAGE_OPERATION_SET, 0);
}
unsigned char maage_chunk_light(const maage_chunk *chunk, int x, int y, int z) {
    if (!chunk || x < 0 || x >= 16 || z < 0 || z >= 16 || y < 0) return 0;
    if (y >= 256) return 0xf0;
    if (chunk->sections[y / 16]) return chunk->sections[y / 16]->light[maage_chunk_cell(x, y, z)];
    if (chunk->direct_sky) return (unsigned char) (chunk->direct_sky[(size_t) (y * 16 + z) * 16 + x] << 4);
    return y >= chunk->heightmap[z * 16 + x] ? 0xf0 : 0;
}

maage_status maage_chunk_random_ticks_initialize(maage_chunk *chunk, const maage_block_registry *blocks, int *leaf_rechecks) {
    const maage_block_state_definition *state;
    const maage_block_definition *block;
    maage_block_state_id pending;
    maage_status result;
    int section, cell;
    if (leaf_rechecks) *leaf_rechecks = 0;
    if (!chunk || !blocks || !leaf_rechecks) return maage_status_make(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_CONTEXT_WORLD, MAAGE_OPERATION_INITIALIZE, 0);
    for (section = 0; section < 16; ++section) {
        chunk->random_tick_counts[section] = 0;
        if (!chunk->sections[section]) continue;
        for (cell = 0; cell < MAAGE_SECTION_VOLUME; ++cell) {
            state = maage_block_registry_state(blocks, chunk->sections[section]->states[cell]);
            block = state ? maage_block_registry_block(blocks, state->block_id) : NULL;
            if (!block) return maage_status_make(MAAGE_STATUS_INVARIANT_VIOLATION, MAAGE_CONTEXT_WORLD, MAAGE_OPERATION_INITIALIZE, 0);
            /* Detached load/generation output only. Rechecking natural leaves
             * catches support removals that occurred while this column was
             * absent, without retaining events or generating distant terrain. */
            if (state->block_id == MAAGE_BLOCK_LEAVES && !(state->metadata & (MAAGE_LEAF_PERSISTENT | MAAGE_LEAF_CHECK_DECAY))) {
                result = maage_block_leaves_state(state->metadata | MAAGE_LEAF_CHECK_DECAY, &pending);
                if (maage_status_failed(result)) return result;
                if (!maage_block_registry_state(blocks, pending)) return maage_status_make(MAAGE_STATUS_INVARIANT_VIOLATION, MAAGE_CONTEXT_WORLD, MAAGE_OPERATION_INITIALIZE, 0);
                chunk->sections[section]->states[cell] = pending;
                *leaf_rechecks = 1;
            }
            if (state->random_ticks) ++chunk->random_tick_counts[section];
        }
    }
    return maage_status_make(MAAGE_STATUS_SUCCESS, 0, MAAGE_OPERATION_INITIALIZE, 0);
}

unsigned char maage_chunk_presentation_light(const maage_chunk *chunk, const maage_block_registry *blocks, int x, int y, int z) {
    const maage_block_state_definition *state;
    const maage_block_definition *block;
    if (y >= 256) return 240;
    if (y < 0) return 0;
    if (chunk->direct_sky) return maage_chunk_light(chunk, x, y, z);
    state = maage_block_registry_state(blocks, maage_chunk_get(chunk, x, y, z));
    block = state ? maage_block_registry_block(blocks, state->block_id) : NULL;
    /* This is never stored in numerical light buffers. Roofed terrain has a
     * modest sky approximation; calculated cave darkness is left untouched. */
    return (unsigned char) ((y >= chunk->heightmap[z * 16 + x] ? 240 : 64) | (block ? block->emitted_light : 0));
}
