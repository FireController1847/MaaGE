#include "world_internal.h"
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <stddef.h>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

/* Payload references are mutated by detached jobs and their owner concurrently.
 * Native atomics preserve C90 compatibility without a global world lock. */
struct maage_section_payload {
    volatile long references;
    size_t bytes;
    unsigned int palette[1];
};
struct maage_sky_payload {
    volatile long references;
    size_t bytes;
    unsigned short offsets[16]; /* Byte offsets; USHRT_MAX denotes a uniform section. */
    unsigned char uniform[16];
    unsigned char *cells;
};
struct maage_section_light_payload {
    volatile long references;
    unsigned char *cells; /* Exact 4 KiB allocation, independently copy-on-write. */
};
static long maage_payload_retain(volatile long *references) {
#ifdef _WIN32
    return (long) InterlockedIncrement((volatile LONG *) references);
#else
    return __sync_add_and_fetch(references, 1L);
#endif
}
static long maage_payload_release(volatile long *references) {
#ifdef _WIN32
    return (long) InterlockedDecrement((volatile LONG *) references);
#else
    return __sync_sub_and_fetch(references, 1L);
#endif
}
static long maage_payload_references(volatile long *references) {
#ifdef _WIN32
    return (long) InterlockedCompareExchange((volatile LONG *) references, 0, 0);
#else
    return __sync_add_and_fetch(references, 0L);
#endif
}
static void maage_section_light_payload_release(maage_section_light_payload *payload) {
    if (payload && !maage_payload_release(&payload->references)) {
        free(payload->cells);
        free(payload);
    }
}
size_t maage_section_light_resident_bytes(const maage_section *section) {
    long references;
    if (!section || !section->light_payload) return 0;
    references = maage_payload_references(&section->light_payload->references);
    return references > 0 ? (sizeof(*section->light_payload) + MAAGE_SECTION_VOLUME) / (size_t) references : 0;
}
maage_status maage_section_light_writable(maage_section *section) {
    maage_section_light_payload *copy;
    if (!section) return maage_status_make(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_CONTEXT_WORLD, MAAGE_OPERATION_COPY, 0);
    if (section->light_payload && maage_payload_references(&section->light_payload->references) == 1)
        return maage_status_make(MAAGE_STATUS_SUCCESS, 0, MAAGE_OPERATION_COPY, 0);
    copy = (maage_section_light_payload *) malloc(sizeof(*copy));
    if (!copy) return maage_status_make(MAAGE_STATUS_ALLOCATION_FAILED, MAAGE_CONTEXT_WORLD, MAAGE_OPERATION_COPY, 0);
    copy->cells = (unsigned char *) calloc(MAAGE_SECTION_VOLUME, 1);
    if (!copy->cells) {
        free(copy);
        return maage_status_make(MAAGE_STATUS_ALLOCATION_FAILED, MAAGE_CONTEXT_WORLD, MAAGE_OPERATION_COPY, 0);
    }
    copy->references = 1;
    if (section->light) memcpy(copy->cells, section->light, MAAGE_SECTION_VOLUME);
    maage_section_light_payload_release(section->light_payload);
    section->light_payload = copy;
    section->light = copy->cells;
    return maage_status_make(MAAGE_STATUS_SUCCESS, 0, MAAGE_OPERATION_COPY, 0);
}
static void maage_section_payload_release(maage_section_payload *payload) {
    if (payload && !maage_payload_release(&payload->references)) free(payload);
}
static void maage_sky_payload_release(maage_sky_payload *payload) {
    if (payload && !maage_payload_release(&payload->references)) {
        free(payload->cells);
        free(payload);
    }
}
size_t maage_section_payload_resident_bytes(const maage_section *section) {
    long references;
    if (!section || !section->payload) return 0;
    references = maage_payload_references(&section->payload->references);
    return references > 0 ? section->payload->bytes / (size_t) references : 0;
}
size_t maage_chunk_sky_resident_bytes(const maage_chunk *chunk) {
    long references;
    if (!chunk || !chunk->sky_payload) return 0;
    references = maage_payload_references(&chunk->sky_payload->references);
    return references > 0 ? (sizeof(*chunk->sky_payload) + chunk->sky_payload->bytes) / (size_t) references : 0;
}
void maage_chunk_sky_share(maage_chunk *destination, const maage_chunk *source) {
    maage_sky_payload *payload = source->sky_payload;
    if (payload) maage_payload_retain(&payload->references);
    maage_sky_payload_release(destination->sky_payload);
    destination->sky_payload = payload;
    destination->direct_sky = payload ? payload->cells : NULL;
}
maage_status maage_chunk_sky_writable(maage_chunk *chunk) {
    maage_sky_payload *copy;
    int section;
    if (!chunk) return maage_status_make(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_CONTEXT_WORLD, MAAGE_OPERATION_COPY, 0);
    if (chunk->sky_payload && chunk->sky_payload->bytes == MAAGE_DIRECT_SKY_BYTES && maage_payload_references(&chunk->sky_payload->references) == 1)
        return maage_status_make(MAAGE_STATUS_SUCCESS, 0, MAAGE_OPERATION_COPY, 0);
    copy = (maage_sky_payload *) malloc(sizeof(*copy));
    if (!copy) return maage_status_make(MAAGE_STATUS_ALLOCATION_FAILED, MAAGE_CONTEXT_WORLD, MAAGE_OPERATION_COPY, 0);
    copy->cells = (unsigned char *) calloc(MAAGE_DIRECT_SKY_BYTES, 1);
    if (!copy->cells) {
        free(copy);
        return maage_status_make(MAAGE_STATUS_ALLOCATION_FAILED, MAAGE_CONTEXT_WORLD, MAAGE_OPERATION_COPY, 0);
    }
    copy->references = 1;
    copy->bytes = MAAGE_DIRECT_SKY_BYTES;
    for (section = 0; section < 16; ++section) {
        copy->offsets[section] = (unsigned short) (section * (MAAGE_SECTION_VOLUME / 2));
        copy->uniform[section] = 0;
    }
    if (chunk->sky_payload)
        for (section = 0; section < 16; ++section) {
            unsigned short offset = chunk->sky_payload->offsets[section];
            if (offset == USHRT_MAX) memset(copy->cells + copy->offsets[section], chunk->sky_payload->uniform[section] * 17U, MAAGE_SECTION_VOLUME / 2);
            else memcpy(copy->cells + copy->offsets[section], chunk->direct_sky + offset, MAAGE_SECTION_VOLUME / 2);
        }
    maage_sky_payload_release(chunk->sky_payload);
    chunk->sky_payload = copy;
    chunk->direct_sky = copy->cells;
    return maage_status_make(MAAGE_STATUS_SUCCESS, 0, MAAGE_OPERATION_COPY, 0);
}
static void maage_chunk_sky_compact(maage_chunk *chunk) {
    maage_sky_payload *source = chunk->sky_payload, *copy;
    unsigned char uniform[16], value;
    unsigned short offsets[16];
    size_t section, cell, bytes = 0;
    if (!source || source->bytes != MAAGE_DIRECT_SKY_BYTES || maage_payload_references(&source->references) != 1) return;
    for (section = 0; section < 16; ++section) {
        value = source->cells[section * (MAAGE_SECTION_VOLUME / 2)];
        for (cell = 0; cell < MAAGE_SECTION_VOLUME / 2; ++cell)
            if (source->cells[section * (MAAGE_SECTION_VOLUME / 2) + cell] != value) break;
        uniform[section] = value & 15U;
        if (cell == MAAGE_SECTION_VOLUME / 2 && value == (unsigned char) (uniform[section] * 17U)) offsets[section] = USHRT_MAX;
        else {
            offsets[section] = (unsigned short) bytes;
            bytes += MAAGE_SECTION_VOLUME / 2;
        }
    }
    if (bytes == MAAGE_DIRECT_SKY_BYTES) return;
    copy = (maage_sky_payload *) malloc(sizeof(*copy));
    if (!copy) return;
    copy->cells = (unsigned char *) malloc(bytes ? bytes : 1);
    if (!copy->cells) {
        free(copy);
        return;
    }
    copy->references = 1;
    copy->bytes = bytes ? bytes : 1;
    memcpy(copy->uniform, uniform, sizeof(uniform));
    memcpy(copy->offsets, offsets, sizeof(offsets));
    for (section = 0; section < 16; ++section)
        if (offsets[section] != USHRT_MAX) memcpy(copy->cells + offsets[section], source->cells + section * (MAAGE_SECTION_VOLUME / 2), MAAGE_SECTION_VOLUME / 2);
    maage_sky_payload_release(source);
    chunk->sky_payload = copy;
    chunk->direct_sky = copy->cells;
}
int maage_chunk_sky_equal(const maage_chunk *left, const maage_chunk *right) {
    const maage_sky_payload *a = left->sky_payload, *b = right->sky_payload;
    size_t section, byte;
    unsigned short ao, bo;
    unsigned char value;
    if (a == b) return 1;
    if (!a || !b) return 0;
    for (section = 0; section < 16; ++section) {
        ao = a->offsets[section];
        bo = b->offsets[section];
        if (ao == USHRT_MAX && bo == USHRT_MAX) {
            if (a->uniform[section] != b->uniform[section]) return 0;
        } else if (ao != USHRT_MAX && bo != USHRT_MAX) {
            if (memcmp(a->cells + ao, b->cells + bo, MAAGE_SECTION_VOLUME / 2)) return 0;
        } else {
            const unsigned char *cells = ao == USHRT_MAX ? b->cells + bo : a->cells + ao;
            value = (unsigned char) ((ao == USHRT_MAX ? a->uniform[section] : b->uniform[section]) * 17U);
            for (byte = 0; byte < MAAGE_SECTION_VOLUME / 2; ++byte)
                if (cells[byte] != value) return 0;
        }
    }
    return 1;
}

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
    if (maage_status_failed(maage_section_light_writable(section))) {
        free(section);
        return NULL;
    }
    section->states = (unsigned int *) malloc(MAAGE_SECTION_VOLUME * sizeof(*section->states));
    if (!section->states) {
        maage_section_destroy(section);
        return NULL;
    }
    for (i = 0; i < MAAGE_SECTION_VOLUME; ++i)
        section->states[i] = (unsigned int) air;
    return section;
}
void maage_section_destroy(maage_section *section) {
    if (!section) return;
    free(section->states);
    maage_section_light_payload_release(section->light_payload);
    if (section->payload) maage_section_payload_release(section->payload);
    else {
        free(section->palette);
        free(section->packed);
    }
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
    for (cell = 0; cell < MAAGE_SECTION_VOLUME; ++cell)
        states[cell] = section->palette[maage_section_index(section, cell)];
    section->states = states;
    if (section->payload) maage_section_payload_release(section->payload);
    else {
        free(section->palette);
        free(section->packed);
    }
    section->payload = NULL;
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
    copy->light_payload = source->light_payload;
    if (copy->light_payload) maage_payload_retain(&copy->light_payload->references);
    copy->light = source->light;
    copy->palette_count = source->palette_count;
    copy->bits = source->bits;
    if (source->payload) {
        copy->payload = source->payload;
        maage_payload_retain(&copy->payload->references);
        copy->palette = source->palette;
        copy->packed = source->packed;
        return copy;
    }
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
    maage_section_payload *payload;
    unsigned char *packed;
    size_t cell, bytes, at, total;
    if (!section || !section->states) return;
    memset(slots, 0, sizeof(slots));
    for (cell = 0; cell < MAAGE_SECTION_VOLUME; ++cell) {
        state = section->states[cell];
        bucket = (unsigned int) ((state * 2654435761UL) & 8191UL);
        while (slots[bucket] && palette[slots[bucket] - 1U] != state)
            bucket = (bucket + 1U) & 8191U;
        if (!slots[bucket]) {
            palette[count] = state;
            slots[bucket] = ++count;
        }
        indices[cell] = slots[bucket] - 1U;
    }
    bits = count <= 1U ? 0U : (count <= 2U ? 1U : (count <= 4U ? 2U : (count <= 16U ? 4U : (count <= 256U ? 8U : 16U))));
    bytes = (MAAGE_SECTION_VOLUME * bits + 7) / 8;
    total = offsetof(maage_section_payload, palette) + count * sizeof(*palette) + bytes;
    if (total < sizeof(*payload)) total = sizeof(*payload);
    payload = (maage_section_payload *) malloc(total);
    if (!payload) return; /* Preserve fully indexed mutable storage on pressure. */
    payload->references = 1;
    payload->bytes = total;
    memcpy(payload->palette, palette, count * sizeof(*palette));
    packed = (unsigned char *) (payload->palette + count);
    if (bytes) memset(packed, 0, bytes);
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
    section->payload = payload;
    section->palette = payload->palette;
    section->palette_count = count;
    section->packed = bytes ? packed : NULL;
    section->bits = bits;
}
maage_status maage_section_export(const maage_section *section, maage_column_section *packet) {
    maage_section *copy = NULL;
    size_t bytes, palette_bytes;
    if (!section) return maage_status_make(MAAGE_STATUS_NOOP, 0, MAAGE_OPERATION_COPY, 0);
    if (section->states) {
        copy = maage_section_clone(section);
        if (!copy) return maage_status_make(MAAGE_STATUS_ALLOCATION_FAILED, MAAGE_CONTEXT_WORLD, MAAGE_OPERATION_COPY, 0);
        maage_section_compact(copy);
        section = copy;
    }
    if (section->states) {
        maage_section_destroy(copy);
        return maage_status_make(MAAGE_STATUS_ALLOCATION_FAILED, MAAGE_CONTEXT_WORLD, MAAGE_OPERATION_COPY, 0);
    }
    bytes = MAAGE_SECTION_VOLUME * section->bits / 8;
    palette_bytes = section->palette_count * sizeof(*packet->palette);
    packet->palette = (unsigned int *) malloc(palette_bytes + bytes);
    if (!packet->palette) {
        maage_section_destroy(copy);
        return maage_status_make(MAAGE_STATUS_ALLOCATION_FAILED, MAAGE_CONTEXT_WORLD, MAAGE_OPERATION_COPY, 0);
    }
    packet->palette_count = section->palette_count;
    packet->bits = section->bits;
    packet->packed = bytes ? (unsigned char *) packet->palette + palette_bytes : NULL;
    memcpy(packet->palette, section->palette, palette_bytes);
    if (bytes) memcpy(packet->packed, section->packed, bytes);
    maage_section_destroy(copy);
    return maage_status_make(MAAGE_STATUS_SUCCESS, 0, MAAGE_OPERATION_COPY, 0);
}
maage_status maage_section_import(const maage_column_section *packet, const maage_block_registry *registry, maage_section **output) {
    maage_section *section;
    maage_section_payload *payload;
    unsigned int seen[8192], index, bucket, state, expected;
    size_t bytes, total, cell;
    *output = NULL;
    if (!packet->palette_count) {
        if (packet->bits || packet->palette || packet->packed) goto malformed;
        return maage_status_make(MAAGE_STATUS_NOOP, 0, MAAGE_OPERATION_COPY, 0);
    }
    if (packet->palette_count > MAAGE_SECTION_VOLUME || !packet->palette) goto malformed;
    expected = packet->palette_count <= 1U ? 0U : (packet->palette_count <= 2U ? 1U : (packet->palette_count <= 4U ? 2U : (packet->palette_count <= 16U ? 4U : (packet->palette_count <= 256U ? 8U : 16U))));
    if (packet->bits != expected || (expected && !packet->packed) || (!expected && packet->packed)) goto malformed;
    memset(seen, 0, sizeof(seen));
    for (index = 0; index < packet->palette_count; ++index) {
        state = packet->palette[index];
        if (!maage_block_registry_state(registry, state)) goto malformed;
        bucket = (unsigned int) ((state * 2654435761UL) & 8191UL);
        while (seen[bucket]) {
            if (packet->palette[seen[bucket] - 1U] == state) goto malformed;
            bucket = (bucket + 1U) & 8191U;
        }
        seen[bucket] = index + 1U;
    }
    section = (maage_section *) calloc(1, sizeof(*section));
    if (!section) goto allocation;
    bytes = MAAGE_SECTION_VOLUME * expected / 8;
    total = offsetof(maage_section_payload, palette) + packet->palette_count * sizeof(*packet->palette) + bytes;
    if (total < sizeof(*payload)) total = sizeof(*payload);
    payload = (maage_section_payload *) malloc(total);
    if (!payload) {
        free(section);
        goto allocation;
    }
    payload->references = 1;
    payload->bytes = total;
    memcpy(payload->palette, packet->palette, packet->palette_count * sizeof(*packet->palette));
    section->payload = payload;
    section->palette = payload->palette;
    section->palette_count = packet->palette_count;
    section->bits = expected;
    section->packed = bytes ? (unsigned char *) (payload->palette + packet->palette_count) : NULL;
    if (bytes) memcpy(section->packed, packet->packed, bytes);
    for (cell = 0; cell < MAAGE_SECTION_VOLUME; ++cell)
        if (maage_section_index(section, cell) >= section->palette_count) {
            maage_section_destroy(section);
            goto malformed;
        }
    if (maage_status_failed(maage_section_light_writable(section))) {
        maage_section_destroy(section);
        goto allocation;
    }
    *output = section;
    return maage_status_make(MAAGE_STATUS_SUCCESS, 0, MAAGE_OPERATION_COPY, 0);
malformed:
    return maage_status_make(MAAGE_STATUS_MALFORMED_INPUT, MAAGE_CONTEXT_WORLD, MAAGE_OPERATION_COPY, 0);
allocation:
    return maage_status_make(MAAGE_STATUS_ALLOCATION_FAILED, MAAGE_CONTEXT_WORLD, MAAGE_OPERATION_COPY, 0);
}
void maage_chunk_compact(maage_chunk *chunk) {
    int section;
    if (!chunk) return;
    for (section = 0; section < 16; ++section)
        maage_section_compact(chunk->sections[section]);
    maage_chunk_sky_compact(chunk);
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
    free(chunk->changes);
    if (chunk->sky_payload) maage_sky_payload_release(chunk->sky_payload);
    free(chunk);
    return maage_status_make(MAAGE_STATUS_SUCCESS, 0, MAAGE_OPERATION_DESTROY, 0);
}
unsigned char maage_chunk_direct_sky_get(const maage_chunk *chunk, size_t cell) {
    size_t section = cell / MAAGE_SECTION_VOLUME;
    unsigned short offset = chunk->sky_payload->offsets[section];
    unsigned char packed;
    if (offset == USHRT_MAX) return chunk->sky_payload->uniform[section];
    packed = chunk->direct_sky[offset + (cell % MAAGE_SECTION_VOLUME) / 2];
    return (unsigned char) ((cell & 1U) ? packed >> 4 : packed & 15U);
}
void maage_chunk_direct_sky_set(maage_chunk *chunk, size_t cell, unsigned char value) {
    unsigned char *packed = &chunk->direct_sky[cell >> 1];
    if (cell & 1U) *packed = (unsigned char) ((*packed & 15U) | (value << 4));
    else *packed = (unsigned char) ((*packed & 240U) | (value & 15U));
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
                section->light[cell] = (unsigned char) (maage_chunk_direct_sky_get(chunk, (size_t) (y / 16) * MAAGE_SECTION_VOLUME + cell) << 4);
        chunk->sections[y / 16] = section;
    }
    return maage_section_set(section, maage_chunk_cell(x, y, z), state);
}
unsigned char maage_chunk_light(const maage_chunk *chunk, int x, int y, int z) {
    if (!chunk || x < 0 || x >= 16 || z < 0 || z >= 16 || y < 0) return 0;
    if (y >= 256) return 0xf0;
    if (chunk->sections[y / 16]) return chunk->sections[y / 16]->light[maage_chunk_cell(x, y, z)];
    if (chunk->direct_sky) return (unsigned char) (maage_chunk_direct_sky_get(chunk, (size_t) (y * 16 + z) * 16 + x) << 4);
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
    if (y >= 256) return 240;
    if (y < 0) return 0;
    if (chunk->direct_sky) return maage_chunk_light(chunk, x, y, z);
    state = maage_block_registry_state(blocks, maage_chunk_get(chunk, x, y, z));
    /* This is never stored in numerical light buffers. Roofed terrain has a
     * modest sky approximation; calculated cave darkness is left untouched. */
    return (unsigned char) ((y >= chunk->heightmap[z * 16 + x] ? 240 : 64) | (state ? state->emitted_light : 0));
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
