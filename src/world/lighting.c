#include "world_internal.h"
#include "maage/jobs.h"
#include <stdlib.h>
#include <string.h>

/* One job owns one output column. Immutable boundary values are numeric
 * inputs; the owner schedules another pass when a facing boundary changes.
 * Both light channels converge by the same attenuating fixed point. */
typedef struct maage_column_light {
    maage_chunk *chunk;
    const maage_chunk **neighbors;
    unsigned char queued[8192];
    unsigned int *queue;
    size_t head, tail, count;
} maage_column_light;
static void maage_light_enqueue(maage_column_light *work, int x, int y, int z) {
    unsigned int cell;
    if (x < 0 || x >= 16 || z < 0 || z >= 16 || y < 0 || y >= 256) return;
    cell = (unsigned int) ((y * 16 + z) * 16 + x);
    if (work->queued[cell / 8] & (1U << (cell % 8))) return;
    work->queued[cell / 8] |= (unsigned char) (1U << (cell % 8));
    work->queue[work->tail] = cell;
    work->tail = (work->tail + 1) % 65536UL;
    ++work->count;
}
static unsigned char maage_light_sample(const maage_column_light *work, int x, int y, int z) {
    const maage_chunk *chunk = work->chunk;
    if (y >= 256) return 240;
    if (y < 0) return 0;
    if (x < 0) chunk = work->neighbors[0];
    else if (x >= 16) chunk = work->neighbors[1];
    else if (z < 0) chunk = work->neighbors[2];
    else if (z >= 16) chunk = work->neighbors[3];
    return chunk ? maage_chunk_light(chunk, maage_world_local_coordinate(x), y, maage_world_local_coordinate(z)) : 0;
}
static maage_status maage_light_write(maage_chunk *chunk, int x, int y, int z, unsigned char light) {
    maage_section *section = chunk->sections[y / 16];
    size_t cell;
    if (!section) {
        section = (maage_section *) calloc(1, sizeof(*section));
        if (!section) return maage_status_make(MAAGE_STATUS_ALLOCATION_FAILED, MAAGE_CONTEXT_WORLD, MAAGE_OPERATION_LIGHT, 0);
        for (cell = 0; cell < 4096; ++cell)
            section->light[cell] = (unsigned char) (chunk->direct_sky[(size_t) (y / 16) * 4096 + cell] << 4);
        chunk->sections[y / 16] = section;
    }
    section->light[maage_chunk_cell(x, y % 16, z)] = light;
    return maage_status_make(MAAGE_STATUS_SUCCESS, 0, MAAGE_OPERATION_LIGHT, 0);
}
maage_status maage_light_provisional_cell(maage_chunk *chunk, const maage_chunk *neighbors[4], const maage_block_registry *blocks, int x, int y, int z) {
    static const int offsets[6][3] = {{-1, 0, 0}, {1, 0, 0}, {0, -1, 0}, {0, 1, 0}, {0, 0, -1}, {0, 0, 1}};
    const maage_block_state_definition *state;
    const maage_block_definition *block = NULL;
    const maage_chunk *sample;
    unsigned int sky = 15, emission, opacity, value, side;
    unsigned char adjacent;
    int height, nx, ny, nz;
    /* Without a numerical cache all consumers already use the heightmap seed.
     * Never invent a numerical cache, or turn an uninitialized zero into dark. */
    if (!chunk->direct_sky) return maage_status_make(MAAGE_STATUS_NOOP, 0, MAAGE_OPERATION_LIGHT, 0);
    for (height = 255; height >= y; --height) {
        state = maage_block_registry_state(blocks, maage_chunk_get(chunk, x, height, z));
        block = state ? maage_block_registry_block(blocks, state->block_id) : NULL;
        if (!block) return maage_status_make(MAAGE_STATUS_INVARIANT_VIOLATION, MAAGE_CONTEXT_WORLD, MAAGE_OPERATION_LIGHT, 0);
        opacity = block->light_opacity;
        if (!opacity && sky < 15) opacity = 1;
        sky = sky > opacity ? sky - opacity : 0;
    }
    emission = block->emitted_light;
    opacity = block->light_opacity;
    if (!opacity || (opacity >= 15 && emission)) opacity = 1;
    if (opacity < 15)
        for (side = 0; side < 6; ++side) {
            nx = x + offsets[side][0];
            ny = y + offsets[side][1];
            nz = z + offsets[side][2];
            sample = chunk;
            if (nx < 0) sample = neighbors[0];
            else if (nx >= 16) sample = neighbors[1];
            else if (nz < 0) sample = neighbors[2];
            else if (nz >= 16) sample = neighbors[3];
            if (!sample) continue;
            adjacent = maage_chunk_presentation_light(sample, blocks, maage_world_local_coordinate(nx), ny, maage_world_local_coordinate(nz));
            value = adjacent >> 4;
            if (value > opacity && value - opacity > sky) sky = value - opacity;
            value = adjacent & 15;
            if (value > opacity && value - opacity > emission) emission = value - opacity;
        }
    /* Keep the direct-sky cache unchanged: the queued dirty pass must detect
     * every changed shaft value, including the cells below a closing roof. */
    return maage_light_write(chunk, x, y, z, (unsigned char) ((sky << 4) | emission));
}
maage_status maage_light_column_job(maage_chunk *chunk, const maage_chunk *neighbors[4], const unsigned char dirty[8192], unsigned boundaries, const maage_block_registry *blocks, const maage_job_token *token, maage_status_trace *trace) {
    static const int offsets[6][3] = {{-1, 0, 0}, {1, 0, 0}, {0, -1, 0}, {0, 1, 0}, {0, 0, -1}, {0, 0, 1}};
    maage_column_light work;
    const maage_block_state_definition *state;
    const maage_block_definition *block;
    unsigned char shafts[256], previous, light, adjacent;
    unsigned int cell, side, sky, emitted, opacity, value, bit;
    size_t byte, steps = 0;
    int x, y, z, initial, changed = 0;
    maage_status result = maage_status_make(MAAGE_STATUS_SUCCESS, 0, MAAGE_OPERATION_LIGHT, 0);
    if (!chunk || !neighbors || !dirty || !maage_block_registry_is_finalized(blocks)) return MAAGE_STATUS_TRACE(trace, maage_status_make(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_CONTEXT_WORLD, MAAGE_OPERATION_LIGHT, 0), MAAGE_CONTEXT_WORLD, MAAGE_OPERATION_LIGHT);
    memset(&work, 0, sizeof(work));
    work.chunk = chunk;
    work.neighbors = neighbors;
    work.queue = (unsigned int *) malloc(65536UL * sizeof(*work.queue));
    initial = chunk->direct_sky == NULL;
    if (initial) chunk->direct_sky = (unsigned char *) calloc(65536UL, 1);
    if (!work.queue || !chunk->direct_sky) {
        result = maage_status_make(MAAGE_STATUS_ALLOCATION_FAILED, MAAGE_CONTEXT_WORLD, MAAGE_OPERATION_LIGHT, 0);
        goto done;
    }
    memset(shafts, initial ? 1 : 0, sizeof(shafts));
    /* Skip empty dirty bytes. A small edit visits its cells and sky shafts,
     * rather than scanning every block merely to discover those cells. */
    for (byte = 0; byte < 8192; ++byte) {
        if (!initial && !dirty[byte]) continue;
        for (bit = 0; bit < 8; ++bit) {
            if (!initial && !(dirty[byte] & (1U << bit))) continue;
            cell = (unsigned int) (byte * 8 + bit);
            x = cell % 16;
            z = (cell / 16) % 16;
            y = cell / 256;
            shafts[z * 16 + x] = 1;
            maage_light_enqueue(&work, x, y, z);
            for (side = 0; side < 6; ++side)
                maage_light_enqueue(&work, x + offsets[side][0], y + offsets[side][1], z + offsets[side][2]);
        }
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
                        maage_light_enqueue(&work, x, y, z);
                        for (side = 0; side < 6; ++side)
                            maage_light_enqueue(&work, x + offsets[side][0], y + offsets[side][1], z + offsets[side][2]);
                    }
                }
            }
    if (initial) boundaries = 15U;
    for (y = 0; y < 256; ++y)
        for (x = 0; x < 16; ++x) {
            if (boundaries & 1U) maage_light_enqueue(&work, 0, y, x);
            if (boundaries & 2U) maage_light_enqueue(&work, 15, y, x);
            if (boundaries & 4U) maage_light_enqueue(&work, x, y, 0);
            if (boundaries & 8U) maage_light_enqueue(&work, x, y, 15);
        }
    while (work.count) {
        if (!(steps++ & 2047) && token && maage_job_cancelled(token)) {
            result = maage_status_make(MAAGE_STATUS_INTERRUPTED, MAAGE_CONTEXT_WORLD, MAAGE_OPERATION_LIGHT, MAAGE_DETAIL_CANCELLED);
            goto done;
        }
        cell = work.queue[work.head];
        work.head = (work.head + 1) % 65536UL;
        --work.count;
        work.queued[cell / 8] &= (unsigned char) ~(1U << (cell % 8));
        x = cell % 16;
        z = (cell / 16) % 16;
        y = cell / 256;
        state = maage_block_registry_state(blocks, maage_chunk_get(chunk, x, y, z));
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
                adjacent = maage_light_sample(&work, x + offsets[side][0], y + offsets[side][1], z + offsets[side][2]);
                value = adjacent >> 4;
                if (value > opacity && value - opacity > sky) sky = value - opacity;
                value = adjacent & 15;
                if (value > opacity && value - opacity > emitted) emitted = value - opacity;
            }
        light = (unsigned char) ((sky << 4) | emitted);
        previous = maage_light_sample(&work, x, y, z);
        if (light == previous) continue;
        changed = 1;
        result = maage_light_write(chunk, x, y, z, light);
        if (maage_status_failed(result)) goto done;
        for (side = 0; side < 6; ++side)
            maage_light_enqueue(&work, x + offsets[side][0], y + offsets[side][1], z + offsets[side][2]);
    }
done:
    free(work.queue);
    if (!maage_status_failed(result)) result = maage_status_make(changed ? MAAGE_STATUS_SUCCESS : MAAGE_STATUS_NOOP, 0, MAAGE_OPERATION_LIGHT, 0);
    return maage_status_failed(result) ? MAAGE_STATUS_TRACE(trace, result, MAAGE_CONTEXT_WORLD, MAAGE_OPERATION_LIGHT) : result;
}
