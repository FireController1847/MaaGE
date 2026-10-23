#include "world_internal.h"
#include "maage/jobs.h"

#include <stdlib.h>

#define MAAGE_LIGHT_VOLUME ((size_t)MAAGE_SECTION_VOLUME * 16)
#define MAAGE_LIGHT_OPAQUE 255

typedef struct maage_light_work {
    unsigned char *sky, *emitted, *levels, *attenuation, *queued;
    size_t *queue;
    size_t head, tail, count;
} maage_light_work;

static void maage_light_enqueue(maage_light_work *work, size_t index)
{
    if (work->queued[index]) return;
    work->queue[work->tail] = index;
    work->tail = (work->tail + 1) % MAAGE_LIGHT_VOLUME;
    ++work->count;
    work->queued[index] = 1;
}

static void maage_light_spread(maage_light_work *work, size_t destination,
    unsigned int source)
{
    unsigned int attenuation, value;
    attenuation = work->attenuation[destination];
    if (attenuation == MAAGE_LIGHT_OPAQUE || source <= attenuation) return;
    value = source - attenuation;
    if (value <= work->levels[destination]) return;
    work->levels[destination] = (unsigned char)value;
    if (value > 1) maage_light_enqueue(work, destination);
}

static int maage_light_propagate(maage_light_work *work,
    const maage_chunk *neighbors[4], int sky_channel,
    const maage_job_token *token)
{
    size_t index, steps = 0;
    unsigned int source;
    int x, y, z, side;
    work->levels = sky_channel ? work->sky : work->emitted;
    /* Missing neighbors supply no light. Loaded neighbors seed this column
     * without writes to their state or light arrays. Each channel drains the
     * shared bounded queue before the next channel begins. */
    if (neighbors != NULL) {
        for (side = 0; side < 4; ++side) {
            if (neighbors[side] == NULL) continue;
            for (y = 0; y < MAAGE_WORLD_HEIGHT; ++y) {
                if (!(y & 15) && token && maage_job_cancelled(token)) return 0;
                for (z = 0; z < MAAGE_CHUNK_EDGE; ++z) {
                    x = side == 0 ? 0 : (side == 1 ? 15 : z);
                    index = (size_t)y * 256 +
                        (size_t)(side == 2 ? 0 : (side == 3 ? 15 : z)) * 16 + x;
                    source = maage_chunk_light(neighbors[side],
                        side == 0 ? 15 : (side == 1 ? 0 : z), y,
                        side == 2 ? 15 : (side == 3 ? 0 : z));
                    source = sky_channel ? source >> 4 : source & 15;
                    maage_light_spread(work, index, source);
                }
            }
        }
    }
    for (index = 0; index < MAAGE_LIGHT_VOLUME; ++index) {
        if (!(index & 2047) && token && maage_job_cancelled(token)) return 0;
        if (work->levels[index] > 1) maage_light_enqueue(work, index);
    }
    while (work->count != 0) {
        if (!(steps++ & 2047) && token && maage_job_cancelled(token)) return 0;
        index = work->queue[work->head];
        work->head = (work->head + 1) % MAAGE_LIGHT_VOLUME;
        --work->count;
        work->queued[index] = 0;
        source = work->levels[index];
        x = (int)(index % 16);
        z = (int)((index / 16) % 16);
        y = (int)(index / 256);
        /* Emitting opaque blocks are valid sources. Only destination opacity
         * suppresses propagation, so their light can leave the source cell. */
        if (x > 0) maage_light_spread(work, index - 1, source);
        if (x < 15) maage_light_spread(work, index + 1, source);
        if (z > 0) maage_light_spread(work, index - 16, source);
        if (z < 15) maage_light_spread(work, index + 16, source);
        if (y > 0) maage_light_spread(work, index - 256, source);
        if (y < 255) maage_light_spread(work, index + 256, source);
    }
    return 1;
}

maage_status maage_light_chunk_job(maage_chunk *chunk, const maage_block_registry *blocks,
    const maage_chunk *neighbors[4], maage_section *added[16],
    const maage_job_token *token, maage_status_trace *trace)
{
    maage_light_work work;
    const maage_block_state_definition *state;
    const maage_block_definition *block;
    maage_section *section;
    maage_block_state_id id;
    size_t index, local;
    unsigned int sky, opacity;
    unsigned char desired;
    int x, y, z, section_y, changed, valid;
    unsigned char failure = MAAGE_STATUS_ALLOCATION_FAILED;
    maage_status result;
    if (added == NULL) return MAAGE_STATUS_TRACE(trace, maage_status_make(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_CONTEXT_WORLD, MAAGE_OPERATION_LIGHT, 3), MAAGE_CONTEXT_WORLD, MAAGE_OPERATION_LIGHT);
    for (section_y = 0; section_y < 16; ++section_y) added[section_y] = NULL;
    if (chunk == NULL || !maage_block_registry_is_finalized(blocks))
        return MAAGE_STATUS_TRACE(trace, maage_status_make(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_CONTEXT_WORLD, MAAGE_OPERATION_LIGHT, chunk ? 1 : 0), MAAGE_CONTEXT_WORLD, MAAGE_OPERATION_LIGHT);
    if (token && maage_job_cancelled(token))
        return MAAGE_STATUS_TRACE(trace, maage_status_make(MAAGE_STATUS_INTERRUPTED, MAAGE_CONTEXT_WORLD, MAAGE_OPERATION_LIGHT, MAAGE_DETAIL_CANCELLED), MAAGE_CONTEXT_WORLD, MAAGE_OPERATION_LIGHT);
    work.sky = (unsigned char *)malloc(MAAGE_LIGHT_VOLUME);
    work.emitted = (unsigned char *)malloc(MAAGE_LIGHT_VOLUME);
    work.attenuation = (unsigned char *)malloc(MAAGE_LIGHT_VOLUME);
    work.queued = (unsigned char *)calloc(MAAGE_LIGHT_VOLUME, 1);
    work.queue = (size_t *)malloc(MAAGE_LIGHT_VOLUME * sizeof(size_t));
    work.head = work.tail = work.count = 0;
    changed = 0;
    valid = work.sky != NULL && work.emitted != NULL && work.attenuation != NULL &&
        work.queued != NULL && work.queue != NULL;
    if (!valid) goto maage_light_done;
    for (z = 0; z < MAAGE_CHUNK_EDGE; ++z) {
        if (token && maage_job_cancelled(token)) { failure = MAAGE_STATUS_INTERRUPTED; valid = 0; goto maage_light_done; }
        for (x = 0; x < MAAGE_CHUNK_EDGE; ++x) {
            sky = 15;
            for (y = MAAGE_WORLD_HEIGHT - 1; y >= 0; --y) {
                index = (size_t)y * 256 + (size_t)z * 16 + x;
                id = maage_chunk_get(chunk, x, y, z);
                state = maage_block_registry_state(blocks, id);
                block = state == NULL ? NULL :
                    maage_block_registry_block(blocks, state->block_id);
                if (block == NULL) {
                    failure = MAAGE_STATUS_INVARIANT_VIOLATION; valid = 0;
                    goto maage_light_done;
                }
                opacity = block->light_opacity;
                if (opacity == 15) {
                    work.attenuation[index] = MAAGE_LIGHT_OPAQUE;
                    sky = 0;
                } else {
                    work.attenuation[index] = (unsigned char)(opacity ? opacity : 1);
                    /* Clear plants/torches do not shade direct sky. Once sky
                     * is attenuated, even empty cells lose one level. */
                    if (!opacity && sky != 15) opacity = 1;
                    sky = sky > opacity ? sky - opacity : 0;
                }
                work.sky[index] = (unsigned char)sky;
                work.emitted[index] = (unsigned char)block->emitted_light;
            }
        }
    }
    if (!maage_light_propagate(&work, neighbors, 1, token) ||
        !maage_light_propagate(&work, neighbors, 0, token)) {
        failure = MAAGE_STATUS_INTERRUPTED; valid = 0;
        goto maage_light_done;
    }
    for (section_y = 0; section_y < 16; ++section_y) {
        if (token && maage_job_cancelled(token)) { failure = MAAGE_STATUS_INTERRUPTED; valid = 0; goto maage_light_done; }
        section = chunk->sections[section_y];
        for (y = 0; y < MAAGE_CHUNK_EDGE; ++y) {
            for (z = 0; z < MAAGE_CHUNK_EDGE; ++z) {
                for (x = 0; x < MAAGE_CHUNK_EDGE; ++x) {
                    index = (size_t)(section_y * 16 + y) * 256 +
                        (size_t)z * 16 + x;
                    desired = (unsigned char)((work.sky[index] << 4) |
                        work.emitted[index]);
                    local = maage_chunk_cell(x, y, z);
                    if ((section != NULL ? section->light[local] :
                        maage_chunk_light(chunk, x, section_y * 16 + y, z)) == desired)
                        continue;
                    if (section == NULL) {
                        section = (maage_section *)calloc(1, sizeof(*section));
                        if (section == NULL) {
                            valid = 0;
                            goto maage_light_done;
                        }
                        /* Preserve implicit light for cells not yet visited. */
                        for (local = 0; local < MAAGE_SECTION_VOLUME; ++local) {
                            section->light[local] =
                                (section_y * 16 + (int)(local / 256) >=
                                chunk->heightmap[local % 256]) ? 240 : 0;
                        }
                        added[section_y] = section;
                    }
                    local = maage_chunk_cell(x, y, z);
                    section->light[local] = desired;
                    changed = 1;
                }
            }
        }
    }
maage_light_done:
    free(work.queue);
    free(work.queued);
    free(work.attenuation);
    free(work.emitted);
    free(work.sky);
    result = maage_status_make(valid ? (changed ? MAAGE_STATUS_SUCCESS : MAAGE_STATUS_NOOP) : failure,
        MAAGE_CONTEXT_WORLD, MAAGE_OPERATION_LIGHT, failure == MAAGE_STATUS_INTERRUPTED ? MAAGE_DETAIL_CANCELLED : 0);
    return valid ? result : MAAGE_STATUS_TRACE(trace, result, MAAGE_CONTEXT_WORLD, MAAGE_OPERATION_LIGHT);
}
