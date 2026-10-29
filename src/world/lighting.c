#include "world_internal.h"
#include "maage/jobs.h"

#include <stdlib.h>
#include <string.h>

/* Incremental Client light solution over a 3x3 column transaction. A source
 * reaches at most fourteen cells sideways: the outer immutable column ring
 * cannot depend on a change in the center column. Removal and addition use
 * the same decreasing/increasing fixed point, retaining unaffected values. */
#define MAAGE_REGION_EDGE 48U
#define MAAGE_REGION_VOLUME (48UL * 48UL * 256UL)
typedef struct maage_region_light {
    maage_chunk **columns;
    const maage_chunk **halo;
    const maage_block_registry *blocks;
    unsigned char *queued;
    unsigned int *queue;
    size_t head, tail, count, capacity;
} maage_region_light;
static void maage_region_enqueue(maage_region_light *work, int x, int y, int z) {
    size_t index;
    if (x < 0 || x >= 48 || z < 0 || z >= 48 || y < 0 || y >= 256 || !work->columns[x / 16 + 3 * (z / 16)]) return;
    index = (size_t) (y * 48 + z) * 48 + x;
    if (work->queued[index / 8] & (1U << (index % 8))) return;
    work->queued[index / 8] |= (unsigned char) (1U << (index % 8));
    work->queue[work->tail] = (unsigned int) index;
    work->tail = (work->tail + 1) % work->capacity;
    ++work->count;
}
static unsigned char maage_region_sample(const maage_region_light *work, int x, int y, int z) {
    const maage_chunk *chunk;
    if (y >= 256) return 240;
    if (y < 0) return 0;
    if (x >= 0 && x < 48 && z >= 0 && z < 48) {
        chunk = work->columns[x / 16 + 3 * (z / 16)];
        if (!chunk) chunk = work->halo[x / 16 + 1 + 5 * (z / 16 + 1)];
    } else {
        int cx = maage_world_chunk_coordinate(x) + 1, cz = maage_world_chunk_coordinate(z) + 1;
        chunk = work->halo[cx + 5 * cz];
    }
    return chunk ? maage_chunk_light(chunk, maage_world_local_coordinate(x), y, maage_world_local_coordinate(z)) : 0;
}
static maage_status maage_region_write(maage_chunk *chunk, int x, int y, int z, unsigned char light) {
    maage_section *section = chunk->sections[y / 16];
    size_t cell;
    if (!section) {
        section = (maage_section *) calloc(1, sizeof(*section));
        if (!section) return maage_status_make(MAAGE_STATUS_ALLOCATION_FAILED, MAAGE_CONTEXT_WORLD, MAAGE_OPERATION_LIGHT, 0);
        for (cell = 0; cell < 4096; ++cell)
            section->light[cell] = chunk->direct_sky ? (unsigned char) (chunk->direct_sky[(size_t) (y / 16) * 4096 + cell] << 4) : (y / 16 * 16 + (int) (cell / 256) >= chunk->heightmap[cell % 256] ? 240 : 0);
        chunk->sections[y / 16] = section;
    }
    section->light[maage_chunk_cell(x, y % 16, z)] = light;
    return maage_status_make(MAAGE_STATUS_SUCCESS, 0, MAAGE_OPERATION_LIGHT, 0);
}
maage_status maage_light_region_job(maage_chunk *columns[9], const maage_chunk *halo[25], const unsigned char dirty[9][8192], const maage_block_registry *blocks, const maage_job_token *token, maage_status_trace *trace) {
    static const int offsets[6][3] = {{-1, 0, 0}, {1, 0, 0}, {0, -1, 0}, {0, 1, 0}, {0, 0, -1}, {0, 0, 1}};
    maage_region_light work;
    maage_chunk *chunk;
    const maage_block_state_definition *state;
    const maage_block_definition *block;
    unsigned char shafts[256], previous, light, adjacent;
    unsigned int column, cell, side, sky, emitted, opacity, value;
    size_t index, steps = 0;
    int x, y, z, rx, rz, initial, changed = 0;
    maage_status result = maage_status_make(MAAGE_STATUS_SUCCESS, 0, MAAGE_OPERATION_LIGHT, 0);
    work.columns = columns;
    work.halo = halo;
    work.blocks = blocks;
    work.capacity = 0;
    for (column = 0; column < 9; ++column)
        if (columns[column]) work.capacity += 65536UL;
    work.queued = (unsigned char *) calloc((MAAGE_REGION_VOLUME + 7) / 8, 1);
    work.queue = (unsigned int *) malloc(work.capacity * sizeof(*work.queue));
    work.head = work.tail = work.count = 0;
    if (!work.queued || !work.queue) {
        result = maage_status_make(MAAGE_STATUS_ALLOCATION_FAILED, MAAGE_CONTEXT_WORLD, MAAGE_OPERATION_LIGHT, 0);
        goto done;
    }
    for (column = 0; column < 9; ++column) {
        chunk = columns[column];
        if (!chunk) continue;
        initial = chunk->direct_sky == NULL;
        if (initial) chunk->direct_sky = (unsigned char *) calloc(65536UL, 1);
        if (!chunk->direct_sky) {
            result = maage_status_make(MAAGE_STATUS_ALLOCATION_FAILED, MAAGE_CONTEXT_WORLD, MAAGE_OPERATION_LIGHT, 0);
            goto done;
        }
        memset(shafts, initial ? 1 : 0, sizeof(shafts));
        rx = (int) (column % 3) * 16;
        rz = (int) (column / 3) * 16;
        for (cell = 0; cell < 65536UL; ++cell)
            if (initial || (dirty[column][cell / 8] & (1U << (cell % 8)))) {
                x = cell % 16;
                z = (cell / 16) % 16;
                y = cell / 256;
                shafts[z * 16 + x] = 1;
                maage_region_enqueue(&work, rx + x, y, rz + z);
                for (side = 0; side < 6; ++side)
                    maage_region_enqueue(&work, rx + x + offsets[side][0], y + offsets[side][1], rz + z + offsets[side][2]);
            }
        for (z = 0; z < 16; ++z)
            for (x = 0; x < 16; ++x)
                if (shafts[z * 16 + x]) {
                    sky = 15;
                    for (y = 255; y >= 0; --y) {
                        state = maage_block_registry_state(blocks, maage_chunk_get(chunk, x, y, z));
                        block = state ? maage_block_registry_block(blocks, state->block_id) : NULL;
                        if (!block) {
                            result = maage_status_make(MAAGE_STATUS_INVARIANT_VIOLATION, MAAGE_CONTEXT_WORLD, MAAGE_OPERATION_LIGHT, 0);
                            goto done;
                        }
                        opacity = block->light_opacity;
                        if (!opacity && sky < 15) opacity = 1;
                        sky = sky > opacity ? sky - opacity : 0;
                        cell = (unsigned int) ((y * 16 + z) * 16 + x);
                        if (chunk->direct_sky[cell] != sky) {
                            chunk->direct_sky[cell] = (unsigned char) sky;
                            maage_region_enqueue(&work, rx + x, y, rz + z);
                            for (side = 0; side < 6; ++side)
                                maage_region_enqueue(&work, rx + x + offsets[side][0], y + offsets[side][1], rz + z + offsets[side][2]);
                        }
                    }
                }
        /* Newly arrived/missing neighbors may change a boundary without any
         * local block edit. Only a surface, rather than the column volume. */
        for (y = 0; y < 256; ++y)
            for (x = 0; x < 16; ++x) {
                maage_region_enqueue(&work, rx, y, rz + x);
                maage_region_enqueue(&work, rx + 15, y, rz + x);
                maage_region_enqueue(&work, rx + x, y, rz);
                maage_region_enqueue(&work, rx + x, y, rz + 15);
            }
    }
    while (work.count) {
        if (!(steps++ & 2047) && token && maage_job_cancelled(token)) {
            result = maage_status_make(MAAGE_STATUS_INTERRUPTED, MAAGE_CONTEXT_WORLD, MAAGE_OPERATION_LIGHT, MAAGE_DETAIL_CANCELLED);
            goto done;
        }
        index = work.queue[work.head];
        work.head = (work.head + 1) % work.capacity;
        --work.count;
        work.queued[index / 8] &= (unsigned char) ~(1U << (index % 8));
        x = (int) (index % 48);
        z = (int) ((index / 48) % 48);
        y = (int) (index / (48 * 48));
        column = (unsigned int) (x / 16 + 3 * (z / 16));
        chunk = columns[column];
        cell = (unsigned int) ((y * 16 + z % 16) * 16 + x % 16);
        state = maage_block_registry_state(blocks, maage_chunk_get(chunk, x % 16, y, z % 16));
        block = state ? maage_block_registry_block(blocks, state->block_id) : NULL;
        if (!block) {
            result = maage_status_make(MAAGE_STATUS_INVARIANT_VIOLATION, MAAGE_CONTEXT_WORLD, MAAGE_OPERATION_LIGHT, 0);
            goto done;
        }
        sky = chunk->direct_sky[cell];
        emitted = block->emitted_light;
        opacity = block->light_opacity;
        if (!opacity || (opacity >= 15 && emitted)) opacity = 1;
        if (opacity < 15)
            for (side = 0; side < 6; ++side) {
                adjacent = maage_region_sample(&work, x + offsets[side][0], y + offsets[side][1], z + offsets[side][2]);
                value = adjacent >> 4;
                if (value > opacity && value - opacity > sky) sky = value - opacity;
                value = adjacent & 15;
                if (value > opacity && value - opacity > emitted) emitted = value - opacity;
            }
        light = (unsigned char) ((sky << 4) | emitted);
        previous = maage_region_sample(&work, x, y, z);
        if (light == previous) continue;
        changed = 1;
        result = maage_region_write(chunk, x % 16, y, z % 16, light);
        if (maage_status_failed(result)) goto done;
        for (side = 0; side < 6; ++side)
            maage_region_enqueue(&work, x + offsets[side][0], y + offsets[side][1], z + offsets[side][2]);
    }
done:
    free(work.queued);
    free(work.queue);
    if (!maage_status_failed(result)) result = maage_status_make(changed ? MAAGE_STATUS_SUCCESS : MAAGE_STATUS_NOOP, 0, MAAGE_OPERATION_LIGHT, 0);
    return maage_status_failed(result) ? MAAGE_STATUS_TRACE(trace, result, MAAGE_CONTEXT_WORLD, MAAGE_OPERATION_LIGHT) : result;
}

/* Gameplay and presentation share the numerical solver. The Server schedules
 * its authoritative single-column cache independently; it exports no light. */
maage_status maage_light_chunk_job(maage_chunk *chunk, const maage_block_registry *blocks, const maage_chunk *neighbors[4], maage_section *added[16], const maage_job_token *token, maage_status_trace *trace) {
    maage_chunk *columns[9];
    const maage_chunk *halo[25];
    unsigned char dirty[9][8192];
    unsigned int i;
    if (!chunk || !added || !maage_block_registry_is_finalized(blocks)) return MAAGE_STATUS_TRACE(trace, maage_status_make(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_CONTEXT_WORLD, MAAGE_OPERATION_LIGHT, 0), MAAGE_CONTEXT_WORLD, MAAGE_OPERATION_LIGHT);
    memset(columns, 0, sizeof(columns));
    memset(halo, 0, sizeof(halo));
    memset(dirty, 0, sizeof(dirty));
    for (i = 0; i < 16; ++i)
        added[i] = NULL;
    columns[4] = chunk;
    memset(dirty[4], 255, 8192);
    if (neighbors) {
        halo[11] = neighbors[0];
        halo[13] = neighbors[1];
        halo[7] = neighbors[2];
        halo[17] = neighbors[3];
    }
    return maage_light_region_job(columns, halo, dirty, blocks, token, trace);
}
