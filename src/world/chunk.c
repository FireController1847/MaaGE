#include "world_internal.h"
#include <stdlib.h>
#include <string.h>
#include <limits.h>

#if UINT_MAX < 4294967295UL
#error Chunk state storage requires an unsigned int with at least 32 bits
#endif


/* Only section contents are palette-compressed, never registry identities or
 * the World API. Packing is best-effort at immutable publication boundaries:
 * allocation pressure leaves a correct, directly indexed section in place. */
maage_section *maage_section_create(maage_block_state_id air) {
    maage_section *section;
    size_t i;
    section = (maage_section *) calloc(1, sizeof(*section));
    if (!section) return NULL;
    section->states = (unsigned int *) malloc(MAAGE_SECTION_VOLUME * sizeof(*section->states));
    if (!section->states) {
        free(section);
        return NULL;
    }
    for (i = 0; i < MAAGE_SECTION_VOLUME; ++i) section->states[i] = (unsigned int) air;
    return section;
}
void maage_section_destroy(maage_section *section) {
    if (!section) return;
    free(section->states);
    free(section->palette);
    free(section->packed);
    free(section);
}
static unsigned int maage_section_index(const maage_section *section, size_t cell) {
    size_t offset;
    unsigned int bit;
    if (!section->bits) return 0;
    if (section->bits == 16) return (unsigned int) section->packed[cell * 2] | ((unsigned int) section->packed[cell * 2 + 1] << 8);
    offset = (cell * section->bits) / 8;
    bit = (unsigned int) ((cell * section->bits) % 8);
    return ((unsigned int) section->packed[offset] >> bit) & ((1U << section->bits) - 1U);
}
maage_block_state_id maage_section_get(const maage_section *section, size_t cell) {
    if (section->states) return (maage_block_state_id) section->states[cell];
    return (maage_block_state_id) section->palette[maage_section_index(section, cell)];
}
maage_status maage_section_expand(maage_section *section) {
    unsigned int *states;
    size_t cell;
    if (section->states) return maage_status_make(MAAGE_STATUS_NOOP, 0, MAAGE_OPERATION_COPY, 0);
    states = (unsigned int *) malloc(MAAGE_SECTION_VOLUME * sizeof(*states));
    if (!states) return maage_status_make(MAAGE_STATUS_ALLOCATION_FAILED, MAAGE_CONTEXT_WORLD, MAAGE_OPERATION_COPY, 0);
    for (cell = 0; cell < MAAGE_SECTION_VOLUME; ++cell) states[cell] = section->palette[maage_section_index(section, cell)];
    section->states = states;
    free(section->palette);
    free(section->packed);
    section->palette = NULL;
    section->packed = NULL;
    section->palette_count = section->bits = 0;
    return maage_status_make(MAAGE_STATUS_SUCCESS, 0, MAAGE_OPERATION_COPY, 0);
}
maage_status maage_section_set(maage_section *section, size_t cell, maage_block_state_id state) {
    maage_status result;
    if (state >= MAAGE_CONTENT_ID_INVALID) return maage_status_make(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_CONTEXT_WORLD, MAAGE_OPERATION_SET, 0);
    if (maage_section_get(section, cell) == state) return maage_status_make(MAAGE_STATUS_NOOP, 0, MAAGE_OPERATION_SET, 0);
    result = maage_section_expand(section);
    if (maage_status_failed(result)) return result;
    section->states[cell] = (unsigned int) state;
    return maage_status_make(MAAGE_STATUS_SUCCESS, 0, MAAGE_OPERATION_SET, 0);
}
maage_section *maage_section_clone(const maage_section *source) {
    maage_section *copy;
    size_t bytes;
    copy = (maage_section *) calloc(1, sizeof(*copy));
    if (!copy) return NULL;
    memcpy(copy->light, source->light, sizeof(copy->light));
    copy->palette_count = source->palette_count;
    copy->bits = source->bits;
    if (source->states) {
        copy->states = (unsigned int *) malloc(MAAGE_SECTION_VOLUME * sizeof(*copy->states));
        if (copy->states) memcpy(copy->states, source->states, MAAGE_SECTION_VOLUME * sizeof(*copy->states));
    } else {
        bytes = (MAAGE_SECTION_VOLUME * source->bits + 7) / 8;
        copy->palette = (unsigned int *) malloc(source->palette_count * sizeof(*copy->palette));
        if (copy->palette) memcpy(copy->palette, source->palette, source->palette_count * sizeof(*copy->palette));
        if (bytes) {
            copy->packed = (unsigned char *) malloc(bytes);
            if (copy->packed) memcpy(copy->packed, source->packed, bytes);
        }
    }
    if ((source->states && !copy->states) || (!source->states && (!copy->palette || (source->bits && !copy->packed)))) {
        maage_section_destroy(copy);
        return NULL;
    }
    return copy;
}
static void maage_section_compact(maage_section *section) {
    unsigned int slots[8192], palette[MAAGE_SECTION_VOLUME], indices[MAAGE_SECTION_VOLUME];
    unsigned int count = 0, bits, state, bucket;
    unsigned int *saved_palette;
    unsigned char *packed = NULL;
    size_t cell, bytes, at;
    if (!section || !section->states) return;
    memset(slots, 0, sizeof(slots));
    for (cell = 0; cell < MAAGE_SECTION_VOLUME; ++cell) {
        state = section->states[cell];
        bucket = (unsigned int) ((state * 2654435761UL) & 8191UL);
        while (slots[bucket] && palette[slots[bucket] - 1U] != state) bucket = (bucket + 1U) & 8191U;
        if (!slots[bucket]) {
            palette[count] = state;
            slots[bucket] = ++count;
        }
        indices[cell] = slots[bucket] - 1U;
    }
    bits = count <= 1U ? 0U : (count <= 2U ? 1U : (count <= 4U ? 2U : (count <= 16U ? 4U : (count <= 256U ? 8U : 16U))));
    bytes = (MAAGE_SECTION_VOLUME * bits + 7) / 8;
    saved_palette = (unsigned int *) malloc(count * sizeof(*saved_palette));
    if (!saved_palette) return;
    if (bytes) packed = (unsigned char *) calloc(bytes, 1);
    if (bytes && !packed) {
        free(saved_palette);
        return;
    }
    memcpy(saved_palette, palette, count * sizeof(*saved_palette));
    for (cell = 0; cell < MAAGE_SECTION_VOLUME && bits; ++cell) {
        if (bits == 16) {
            packed[cell * 2] = (unsigned char) (indices[cell] & 255U);
            packed[cell * 2 + 1] = (unsigned char) (indices[cell] >> 8);
        } else {
            at = (cell * bits) / 8;
            packed[at] |= (unsigned char) (indices[cell] << ((cell * bits) % 8));
        }
    }
    free(section->states);
    section->states = NULL;
    section->palette = saved_palette;
    section->palette_count = count;
    section->packed = packed;
    section->bits = bits;
}
void maage_chunk_compact(maage_chunk *chunk) {
    int section;
    if (!chunk) return;
    for (section = 0; section < 16; ++section) maage_section_compact(chunk->sections[section]);
}

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
maage_status maage_chunk_create(int x, int z, maage_block_state_id air, maage_chunk **output) {
    maage_chunk *chunk;
    if (!output) return maage_status_make(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_CONTEXT_WORLD, MAAGE_OPERATION_CREATE, 2);
    *output = NULL;
    if (air >= MAAGE_CONTENT_ID_INVALID) return maage_status_make(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_CONTEXT_WORLD, MAAGE_OPERATION_CREATE, 2);
    chunk = (maage_chunk *) calloc(1, sizeof(*chunk));
    if (chunk) {
        chunk->x = x;
        chunk->z = z;
        chunk->air = air;
    }
    *output = chunk;
    return maage_status_make(chunk ? MAAGE_STATUS_SUCCESS : MAAGE_STATUS_ALLOCATION_FAILED, MAAGE_CONTEXT_WORLD, MAAGE_OPERATION_CREATE, 0);
}
maage_status maage_chunk_destroy(maage_chunk *chunk) {
    int section;
    if (!chunk) return maage_status_make(MAAGE_STATUS_NOOP, 0, MAAGE_OPERATION_DESTROY, 0);
    for (section = 0; section < 16; ++section)
        maage_section_destroy(chunk->sections[section]);
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
    if (!chunk) return MAAGE_CONTENT_ID_INVALID;
    if (x < 0 || x >= 16 || z < 0 || z >= 16 || y < 0 || y >= 256 || !chunk->sections[y / 16]) return chunk->air;
    return maage_section_get(chunk->sections[y / 16], maage_chunk_cell(x, y, z));
}
maage_status maage_chunk_set(maage_chunk *chunk, int x, int y, int z, maage_block_state_id state) {
    maage_section *section;
    size_t cell;
    if (!chunk || x < 0 || x >= 16 || z < 0 || z >= 16 || y < 0 || y >= 256) return maage_status_make(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_CONTEXT_WORLD, MAAGE_OPERATION_SET, 0);
    section = chunk->sections[y / 16];
    if (!section && state == chunk->air) return maage_status_make(MAAGE_STATUS_NOOP, 0, MAAGE_OPERATION_SET, 0);
    if (!section) {
        section = maage_section_create(chunk->air);
        if (!section) return maage_status_make(MAAGE_STATUS_ALLOCATION_FAILED, MAAGE_CONTEXT_WORLD, MAAGE_OPERATION_SET, 0);
        if (chunk->direct_sky)
            for (cell = 0; cell < MAAGE_SECTION_VOLUME; ++cell)
                section->light[cell] = (unsigned char) (chunk->direct_sky[(size_t) (y / 16) * MAAGE_SECTION_VOLUME + cell] << 4);
        chunk->sections[y / 16] = section;
    }
    return maage_section_set(section, maage_chunk_cell(x, y, z), state);
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
            state = maage_block_registry_state(blocks, maage_section_get(chunk->sections[section], (size_t) cell));
            block = state ? maage_block_registry_block(blocks, state->block_id) : NULL;
            if (!block) return maage_status_make(MAAGE_STATUS_INVARIANT_VIOLATION, MAAGE_CONTEXT_WORLD, MAAGE_OPERATION_INITIALIZE, 0);
            /* Detached load/generation output only. Rechecking natural leaves
             * catches support removals that occurred while this column was
             * absent, without retaining events or generating distant terrain. */
            if (block->import_state) {
                result = block->import_state(blocks, state, &pending);
                if (maage_status_failed(result)) return result;
                if (!maage_block_registry_state(blocks, pending)) return maage_status_make(MAAGE_STATUS_INVARIANT_VIOLATION, MAAGE_CONTEXT_WORLD, MAAGE_OPERATION_INITIALIZE, 0);
                result = maage_section_set(chunk->sections[section], (size_t) cell, pending);
                if (maage_status_failed(result)) return result;
                if (pending != state->id) *leaf_rechecks = 1;
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

int maage_chunk_x(const maage_chunk *chunk) {
    return chunk ? chunk->x : 0;
}
int maage_chunk_z(const maage_chunk *chunk) {
    return chunk ? chunk->z : 0;
}
maage_status maage_chunk_set_biome(maage_chunk *chunk, int x, int z, maage_biome_id biome) {
    if (!chunk || x < 0 || x >= 16 || z < 0 || z >= 16 || biome >= MAAGE_CONTENT_ID_INVALID) return maage_status_make(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_CONTEXT_WORLD, MAAGE_OPERATION_SET, 0);
    chunk->biomes[z * 16 + x] = biome;
    return maage_status_make(MAAGE_STATUS_SUCCESS, 0, MAAGE_OPERATION_SET, 0);
}

maage_status maage_chunk_generation_output(maage_chunk *chunk, maage_generation_output *output) {
    if (!chunk || !output) return maage_status_make(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_CONTEXT_WORLD, MAAGE_OPERATION_QUERY, 0);
    output->x = chunk->x;
    output->z = chunk->z;
    output->biomes = chunk->biomes;
    output->heightmap = chunk->heightmap;
    output->stages = &chunk->generation_stages;
    output->features = chunk->generation_features;
    return maage_status_make(MAAGE_STATUS_SUCCESS, 0, MAAGE_OPERATION_QUERY, 0);
}
