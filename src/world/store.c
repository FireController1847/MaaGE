#include "maage/block_entity.h"
#include "world_internal.h"
#include "region_store.h"
#include "maage/world_store.h"
#include "maage/storage.h"
#include "maage/folder_link.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <time.h>
#include <limits.h>
#include <errno.h>
#include <zlib.h>

/* Chunk palettes contain at most 65536 numeric saved state identities.
 * Reads allocate only the actual validated file size, not this maximum. */
#define MAAGE_STORE_CHUNK_MAX 18000000UL
/* Explicit file/address-space bound, not an entity-content limit. */
#define MAAGE_STORE_METADATA_MAX 4294967295UL
#define MAAGE_STORE_WORD 4294967295UL
typedef struct maage_store_identity {
    unsigned long runtime, block, metadata;
    char *name;
} maage_store_identity;
typedef struct maage_store_domain {
    maage_store_identity *entries;
    unsigned long *inverse;
    size_t count, capacity, runtime_count;
} maage_store_domain;
enum {
    MAAGE_STORE_BLOCK,
    MAAGE_STORE_STATE,
    MAAGE_STORE_ITEM,
    MAAGE_STORE_BIOME,
    MAAGE_STORE_DOMAINS
};
struct maage_world_store {
    char directory[MAAGE_PATH_CAPACITY], regions_dir[MAAGE_PATH_CAPACITY];
    maage_storage_lock *lock;
    maage_region_store *regions;
    maage_world_record record;
    const maage_content *content;
    const maage_block_registry *blocks;
    const maage_biome_registry *biomes;
    const maage_item_registry *items;
    maage_store_domain domains[MAAGE_STORE_DOMAINS];
    char map_id[37];
    unsigned char *map_bytes;
    size_t map_length;
    int map_extended;
};
typedef struct maage_store_bytes {
    unsigned char *data;
    size_t length, position;
    int failed;
    maage_status failure;
} maage_store_bytes;

static int maage_store_error(char error[256], const char *message) {
    if (error) strcpy(error, message);
    return 0;
}
/* The on-disk CRC polynomial and byte ordering are unchanged. The existing
 * zlib dependency uses table-based CRC32 instead of eight divisions per byte;
 * process inputs in portable uInt-sized pieces for 32-bit target builds. */
static unsigned long maage_store_crc(const unsigned char *data, size_t length) {
    uLong crc = crc32(0L, Z_NULL, 0);
    while (length) {
        uInt count = length > (size_t) UINT_MAX ? UINT_MAX : (uInt) length;
        crc = crc32(crc, data, count);
        data += count;
        length -= count;
    }
    return (unsigned long) crc & MAAGE_STORE_WORD;
}
static void maage_store_put(maage_store_bytes *bytes, unsigned long value, size_t count) {
    size_t i;
    if (count > bytes->length - bytes->position) {
        bytes->failed = 1;
        return;
    }
    for (i = 0; i < count; ++i) {
        bytes->data[bytes->position++] = (unsigned char) (value & 255UL);
        value >>= 8;
    }
}
static unsigned long maage_store_get(maage_store_bytes *bytes, size_t count) {
    unsigned long value = 0;
    size_t i;
    if (count > bytes->length - bytes->position) {
        bytes->failed = 1;
        return 0;
    }
    for (i = 0; i < count; ++i)
        value |= (unsigned long) bytes->data[bytes->position++] << (i * 8);
    return value;
}
static void maage_store_text_put(maage_store_bytes *bytes, const char *text, size_t length) {
    size_t i;
    for (i = 0; i < length; ++i)
        maage_store_put(bytes, (unsigned char) text[i], 1);
}
static int maage_store_magic(maage_store_bytes *bytes, const char *magic) {
    size_t i;
    for (i = 0; i < 8; ++i)
        if (maage_store_get(bytes, 1) != (unsigned char) magic[i]) return 0;
    return !bytes->failed;
}
/* Encode IEEE binary64 explicitly; never serialize host-endian double objects
 * or rely on native integer widths, padding, or floating-point byte layout. */
static void maage_store_double_put(maage_store_bytes *bytes, double value) {
    unsigned long high = 0, low;
    double fraction, mantissa;
    int exponent;
    if (value < 0) {
        high = 2147483648UL;
        value = -value;
    }
    if (!value) {
        maage_store_put(bytes, 0, 4);
        maage_store_put(bytes, high, 4);
        return;
    }
    fraction = frexp(value, &exponent);
    if (exponent < -1021) mantissa = ldexp(value, 1074);
    else {
        high |= (unsigned long) (exponent + 1022) << 20;
        mantissa = (fraction * 2.0 - 1.0) * 4503599627370496.0;
    }
    high |= (unsigned long) floor(mantissa / 4294967296.0);
    low = (unsigned long) fmod(mantissa, 4294967296.0);
    maage_store_put(bytes, low, 4);
    maage_store_put(bytes, high, 4);
}
static double maage_store_double_get(maage_store_bytes *bytes) {
    unsigned long low = maage_store_get(bytes, 4), high = maage_store_get(bytes, 4);
    unsigned int exponent = (unsigned int) ((high >> 20) & 2047UL);
    double value = (double) (high & 1048575UL) * 4294967296.0 + low;
    if (exponent == 2047) {
        bytes->failed = 1;
        return 0;
    }
    if (!exponent) value = ldexp(value, -1074);
    else
        value = ldexp(1.0 + value / 4503599627370496.0, (int) exponent - 1023);
    return high & 2147483648UL ? -value : value;
}
static int maage_store_name(const char *name) {
    const unsigned char *p = (const unsigned char *) name;
    unsigned long point, minimum;
    unsigned int remaining;
    size_t length = strlen(name);
    if (!length || length >= 128) return 0;
    while (*p) {
        if (*p < 32 || *p == 127) return 0;
        if (*p < 128) {
            ++p;
            continue;
        }
        if (*p >= 194 && *p <= 223) {
            point = *p & 31;
            remaining = 1;
            minimum = 128;
        } else if (*p >= 224 && *p <= 239) {
            point = *p & 15;
            remaining = 2;
            minimum = 2048;
        } else if (*p >= 240 && *p <= 244) {
            point = *p & 7;
            remaining = 3;
            minimum = 65536UL;
        } else
            return 0;
        ++p;
        while (remaining--) {
            if (*p < 128 || *p > 191) return 0;
            point = (point << 6) | (*p++ & 63);
        }
        if (point < minimum || (point >= 55296UL && point <= 57343UL) || point > 1114111UL) return 0;
    }
    return 1;
}
static int maage_store_player(const maage_world_record *record) {
    unsigned int i;
    for (i = 0; i < 3; ++i)
        if (!(record->position[i] > -MAAGE_WORLD_LIMIT && record->position[i] < MAAGE_WORLD_LIMIT)) return 0;
    if (!(record->position[1] >= 0 && record->position[1] < MAAGE_WORLD_LIMIT)) return 0;
    return record->yaw >= -1000000.0 && record->yaw <= 1000000.0 && record->pitch >= -1.571 && record->pitch <= 1.571;
}
static maage_status maage_store_fail(char error[256], const char *message, unsigned char code, unsigned char operation, maage_status_trace *trace) {
    maage_store_error(error, message);
    return MAAGE_STATUS_TRACE(trace, maage_status_make(code, MAAGE_CONTEXT_WORLD_IO, operation, 0), MAAGE_CONTEXT_WORLD_IO, operation);
}
static maage_status maage_store_native_error(int native, unsigned char operation, maage_status_trace *trace) {
    maage_status result = native ? maage_status_from_errno(native, MAAGE_CONTEXT_STORAGE, operation) : maage_status_make(MAAGE_STATUS_IO_ERROR, MAAGE_CONTEXT_STORAGE, operation, 0);
    return maage_status_trace_record(trace, result, MAAGE_CONTEXT_STORAGE, operation, __FILE__, __LINE__, (long) native);
}
static maage_status maage_store_read(const char *path, size_t maximum, unsigned char **output, size_t *length, char error[256], maage_status_trace *trace) {
    unsigned char *data;
    FILE *file;
    long file_length;
    int native, failed;
    maage_status result;
    if (!output || !length || !path) return maage_store_fail(error, "Missing world file read arguments.", MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_OPERATION_READ, trace);
    *output = NULL;
    *length = 0;
    /* Storage opens verify no-follow regular files. Avoid a separate status
     * probe for every read; the caller retains missing-chunk detection. */
    errno = 0;
    file = maage_storage_open(path, "rb");
    if (!file) {
        native = errno;
        result = native ? maage_status_from_errno(native, MAAGE_CONTEXT_STORAGE, MAAGE_OPERATION_OPEN) : maage_status_make(MAAGE_STATUS_IO_ERROR, MAAGE_CONTEXT_STORAGE, MAAGE_OPERATION_OPEN, 0);
        maage_store_error(error, "Could not open world file.");
        maage_status_trace_record(trace, result, MAAGE_CONTEXT_STORAGE, MAAGE_OPERATION_OPEN, __FILE__, (unsigned long) __LINE__, (long) native);
        return MAAGE_STATUS_PROPAGATE(trace, result, MAAGE_CONTEXT_WORLD_IO, MAAGE_OPERATION_READ);
    }
    if (fseek(file, 0, SEEK_END) || (file_length = ftell(file)) < 0 || fseek(file, 0, SEEK_SET)) {
        native = errno;
        fclose(file);
        maage_store_error(error, "World file could not be measured or read.");
        result = maage_store_native_error(native, MAAGE_OPERATION_READ, trace);
        return MAAGE_STATUS_PROPAGATE(trace, result, MAAGE_CONTEXT_WORLD_IO, MAAGE_OPERATION_READ);
    }
    if ((unsigned long) file_length > maximum || (long) (size_t) file_length != file_length) {
        fclose(file);
        return maage_store_fail(error, "World file exceeds its format bound.", MAAGE_STATUS_MALFORMED_INPUT, MAAGE_OPERATION_READ, trace);
    }
    *length = (size_t) file_length;
    data = (unsigned char *) malloc(*length ? *length : 1);
    if (!data) {
        fclose(file);
        return maage_store_fail(error, "Not enough memory to read world file.", MAAGE_STATUS_ALLOCATION_FAILED, MAAGE_OPERATION_READ, trace);
    }
    errno = 0;
    failed = fread(data, 1, *length, file) != *length || ferror(file);
    native = errno;
    if (fclose(file) && !failed) {
        failed = 1;
        native = errno;
    }
    if (failed) {
        free(data);
        *length = 0;
        maage_store_error(error, "World file could not be read.");
        result = maage_store_native_error(native, MAAGE_OPERATION_READ, trace);
        return MAAGE_STATUS_PROPAGATE(trace, result, MAAGE_CONTEXT_WORLD_IO, MAAGE_OPERATION_READ);
    }
    *output = data;
    return maage_status_make(MAAGE_STATUS_SUCCESS, 0, MAAGE_OPERATION_READ, 0);
}
static maage_status maage_store_write(const char *path, const unsigned char *data, size_t length, char error[256], maage_status_trace *trace) {
    char temporary[MAAGE_PATH_CAPACITY];
    FILE *file;
    maage_status result;
    int native;
    if (strlen(path) + 5 > sizeof(temporary)) return maage_store_fail(error, "World file path is too long.", MAAGE_STATUS_BUFFER_OVERFLOW, MAAGE_OPERATION_WRITE, trace);
    strcpy(temporary, path);
    strcat(temporary, ".tmp");
    result = maage_storage_remove(temporary);
    if (maage_status_failed(result)) {
        maage_store_error(error, "Could not remove an abandoned temporary world file.");
        return MAAGE_STATUS_PROPAGATE(trace, result, MAAGE_CONTEXT_WORLD_IO, MAAGE_OPERATION_WRITE);
    }
    errno = 0;
    file = maage_storage_open(temporary, "wb");
    if (!file) {
        native = errno;
        result = native ? maage_status_from_errno(native, MAAGE_CONTEXT_STORAGE, MAAGE_OPERATION_OPEN) : maage_status_make(MAAGE_STATUS_IO_ERROR, MAAGE_CONTEXT_STORAGE, MAAGE_OPERATION_OPEN, 0);
        maage_store_error(error, "Could not create a temporary world file.");
        maage_status_trace_record(trace, result, MAAGE_CONTEXT_STORAGE, MAAGE_OPERATION_OPEN, __FILE__, (unsigned long) __LINE__, (long) native);
        return MAAGE_STATUS_PROPAGATE(trace, result, MAAGE_CONTEXT_WORLD_IO, MAAGE_OPERATION_WRITE);
    }
    errno = 0;
    result = fwrite(data, 1, length, file) == length ? maage_storage_sync(file) : maage_store_native_error(errno, MAAGE_OPERATION_WRITE, trace);
    if (fclose(file) && !maage_status_failed(result)) result = maage_store_native_error(errno, MAAGE_OPERATION_CLOSE, trace);
    if (!maage_status_failed(result)) result = maage_storage_private_file(temporary);
    if (!maage_status_failed(result)) result = maage_storage_replace(temporary, path);
    if (maage_status_failed(result)) {
        maage_storage_remove(temporary);
        maage_store_error(error, "World save failed; the previous file was preserved.");
        return MAAGE_STATUS_PROPAGATE(trace, result, MAAGE_CONTEXT_WORLD_IO, MAAGE_OPERATION_WRITE);
    }
    return maage_status_make(MAAGE_STATUS_SUCCESS, 0, MAAGE_OPERATION_WRITE, 0);
}
static int maage_store_checksum(maage_store_bytes *bytes) {
    unsigned long expected;
    if (bytes->length < 4) return 0;
    bytes->position = bytes->length - 4;
    expected = maage_store_get(bytes, 4);
    bytes->position = 0;
    bytes->length -= 4;
    return expected == maage_store_crc(bytes->data, bytes->length);
}
static void maage_store_checksum_put(maage_store_bytes *bytes) {
    unsigned long crc = maage_store_crc(bytes->data, bytes->position);
    maage_store_put(bytes, crc, 4);
}
static int maage_store_identity_get(maage_store_bytes *bytes, char name[256]) {
    unsigned long length = maage_store_get(bytes, 1);
    size_t i;
    if (!length || bytes->failed || length > bytes->length - bytes->position) {
        bytes->failed = 1;
        return 0;
    }
    for (i = 0; i < length; ++i) {
        name[i] = (char) maage_store_get(bytes, 1);
        if (!name[i]) bytes->failed = 1;
    }
    name[length] = 0;
    return !bytes->failed;
}
static void maage_store_identity_put(maage_store_bytes *bytes, const char *name) {
    size_t length;
    if (!name) {
        bytes->failed = 1;
        return;
    }
    length = strlen(name);
    if (!length || length > 255) {
        bytes->failed = 1;
        return;
    }
    maage_store_put(bytes, (unsigned long) length, 1);
    maage_store_text_put(bytes, name, length);
}
/* Only map initialization resolves names. Both directions are immutable before
 * any World worker can borrow the store; extension preserves every saved ID. */
static void maage_store_map_clear(maage_world_store *store) {
    unsigned int domain;
    size_t i;
    for (domain = 0; domain < MAAGE_STORE_DOMAINS; ++domain) {
        maage_store_domain *map = &store->domains[domain];
        for (i = 0; i < map->count; ++i)
            free(map->entries[i].name);
        free(map->entries);
        free(map->inverse);
        memset(map, 0, sizeof(*map));
    }
    free(store->map_bytes);
    store->map_bytes = NULL;
    store->map_length = 0;
}
static maage_status maage_store_map_open(maage_world_store *store, unsigned char *data, size_t length, const char *world_id) {
    maage_store_bytes input, output;
    maage_store_domain *map;
    maage_store_identity *entry;
    const maage_block_state_definition *state;
    const char *name;
    char identity[256], saved_world[37];
    size_t counts[MAAGE_STORE_DOMAINS], saved[MAAGE_STORE_DOMAINS];
    size_t i, j, capacity = 8 + 36 + 36 + 16 + 4;
    unsigned int domain;
    unsigned long runtime, value, block;
    maage_status result;
    if (store->map_bytes) return maage_status_make(MAAGE_STATUS_INVALID_STATE, MAAGE_CONTEXT_WORLD_IO, MAAGE_OPERATION_INITIALIZE, 0);
    counts[MAAGE_STORE_BLOCK] = maage_block_registry_block_count(store->blocks);
    counts[MAAGE_STORE_STATE] = maage_block_registry_state_count(store->blocks);
    counts[MAAGE_STORE_ITEM] = maage_item_registry_count(store->items);
    counts[MAAGE_STORE_BIOME] = maage_biome_registry_count(store->biomes);
    memset(saved, 0, sizeof(saved));
    memset(&input, 0, sizeof(input));
    input.data = data;
    input.length = length;
    if (data) {
        if (length < capacity || !maage_store_checksum(&input) || !maage_store_magic(&input, "MGIDMAP1")) goto malformed;
        for (i = 0; i < 36; ++i)
            store->map_id[i] = (char) maage_store_get(&input, 1);
        store->map_id[36] = 0;
        for (i = 0; i < 36; ++i)
            saved_world[i] = (char) maage_store_get(&input, 1);
        saved_world[36] = 0;
        if (!maage_storage_uuid_valid(store->map_id) || !maage_storage_uuid_valid(saved_world) || strcmp(world_id, saved_world)) goto malformed;
        for (domain = 0; domain < MAAGE_STORE_DOMAINS; ++domain) {
            value = maage_store_get(&input, 4);
            if (value > (size_t) - 1 || value > input.length / (domain == MAAGE_STORE_STATE ? 8UL : 4UL)) goto malformed;
            saved[domain] = (size_t) value;
        }
    } else {
        result = maage_storage_uuid_generate(store->map_id);
        if (maage_status_failed(result)) return result;
    }
    for (domain = 0; domain < MAAGE_STORE_DOMAINS; ++domain) {
        map = &store->domains[domain];
        if (counts[domain] > MAAGE_STORE_WORD) goto overflow;
        if (saved[domain] > counts[domain]) return maage_status_make(MAAGE_STATUS_NOT_FOUND, MAAGE_CONTEXT_REGISTRY, MAAGE_OPERATION_RESOLVE, 0);
        map->capacity = counts[domain];
        map->runtime_count = counts[domain];
        if (domain == MAAGE_STORE_ITEM) {
            if (map->runtime_count == (size_t) - 1) goto overflow;
            ++map->runtime_count;
        }
        if (map->capacity > (size_t) - 1 / sizeof(*map->entries) || map->runtime_count > (size_t) - 1 / sizeof(*map->inverse)) goto overflow;
        map->entries = (maage_store_identity *) calloc(map->capacity ? map->capacity : 1, sizeof(*map->entries));
        map->inverse = (unsigned long *) calloc(map->runtime_count ? map->runtime_count : 1, sizeof(*map->inverse));
        if (!map->entries || !map->inverse) goto allocation;
        for (i = 0; i < saved[domain]; ++i) {
            entry = &map->entries[map->count++];
            if (domain == MAAGE_STORE_STATE) {
                entry->block = maage_store_get(&input, 4);
                entry->metadata = maage_store_get(&input, 4);
                if (!entry->block || entry->block > saved[MAAGE_STORE_BLOCK] || entry->metadata > UINT_MAX) goto malformed;
                block = store->domains[MAAGE_STORE_BLOCK].entries[entry->block - 1].runtime;
                result = maage_block_registry_resolve_state(store->blocks, block, (unsigned int) entry->metadata, &runtime);
            } else {
                if (!maage_store_identity_get(&input, identity)) goto malformed;
                entry->name = (char *) malloc(strlen(identity) + 1);
                if (!entry->name) goto allocation;
                strcpy(entry->name, identity);
                if (domain == MAAGE_STORE_BLOCK) result = maage_block_registry_resolve_name(store->blocks, identity, &runtime);
                else if (domain == MAAGE_STORE_ITEM) result = maage_item_registry_resolve_name(store->items, identity, &runtime);
                else result = maage_biome_registry_resolve_name(store->biomes, identity, &runtime);
            }
            if (maage_status_failed(result)) return result;
            if (input.failed || runtime >= map->runtime_count || map->inverse[runtime] || (domain == MAAGE_STORE_ITEM && !runtime)) goto malformed;
            entry->runtime = runtime;
            map->inverse[runtime] = (unsigned long) map->count;
        }
        /* Enumerating the finalized dense allocation sequence happens once.
         * Sparse pages still own ID-to-definition access in the registries. */
        for (j = domain == MAAGE_STORE_ITEM ? 1 : 0; j < map->runtime_count; ++j) {
            if (map->inverse[j]) continue;
            entry = &map->entries[map->count++];
            entry->runtime = (unsigned long) j;
            map->inverse[j] = (unsigned long) map->count;
            store->map_extended = data != NULL;
            if (domain == MAAGE_STORE_STATE) {
                state = maage_block_registry_state(store->blocks, entry->runtime);
                if (!state || state->block_id >= store->domains[MAAGE_STORE_BLOCK].runtime_count) goto malformed;
                entry->block = store->domains[MAAGE_STORE_BLOCK].inverse[state->block_id];
                entry->metadata = state->metadata;
                if (!entry->block || entry->metadata > MAAGE_STORE_WORD) goto overflow;
            } else {
                if (domain == MAAGE_STORE_BLOCK) name = maage_block_registry_block_name(store->blocks, entry->runtime);
                else if (domain == MAAGE_STORE_ITEM) name = maage_item_registry_name(store->items, entry->runtime);
                else name = maage_biome_registry_name(store->biomes, entry->runtime);
                if (!name || !strlen(name) || strlen(name) > 255) goto malformed;
                entry->name = (char *) malloc(strlen(name) + 1);
                if (!entry->name) goto allocation;
                strcpy(entry->name, name);
            }
        }
        for (i = 0; i < map->count; ++i) {
            size_t addition = domain == MAAGE_STORE_STATE ? 8 : strlen(map->entries[i].name) + 1;
            if (addition > MAAGE_STORE_WORD - capacity || addition > (size_t) - 1 - capacity) goto overflow;
            capacity += addition;
        }
    }
    if (data && (input.failed || input.position != input.length)) goto malformed;
    memset(&output, 0, sizeof(output));
    output.length = capacity;
    output.data = (unsigned char *) malloc(capacity);
    if (!output.data) goto allocation;
    maage_store_text_put(&output, "MGIDMAP1", 8);
    maage_store_text_put(&output, store->map_id, 36);
    maage_store_text_put(&output, world_id, 36);
    for (domain = 0; domain < MAAGE_STORE_DOMAINS; ++domain)
        maage_store_put(&output, (unsigned long) store->domains[domain].count, 4);
    for (domain = 0; domain < MAAGE_STORE_DOMAINS; ++domain) {
        map = &store->domains[domain];
        for (i = 0; i < map->count; ++i) {
            entry = &map->entries[i];
            if (domain == MAAGE_STORE_STATE) {
                maage_store_put(&output, entry->block, 4);
                maage_store_put(&output, entry->metadata, 4);
            } else maage_store_identity_put(&output, entry->name);
        }
    }
    maage_store_checksum_put(&output);
    if (output.failed || output.position != capacity) {
        free(output.data);
        goto malformed;
    }
    store->map_bytes = output.data;
    store->map_length = capacity;
    return maage_status_make(MAAGE_STATUS_SUCCESS, 0, MAAGE_OPERATION_INITIALIZE, 0);
malformed:
    return maage_status_make(MAAGE_STATUS_MALFORMED_INPUT, MAAGE_CONTEXT_WORLD_IO, MAAGE_OPERATION_DECODE, 0);
overflow:
    return maage_status_make(MAAGE_STATUS_BUFFER_OVERFLOW, MAAGE_CONTEXT_WORLD_IO, MAAGE_OPERATION_INITIALIZE, 0);
allocation:
    return maage_status_make(MAAGE_STATUS_ALLOCATION_FAILED, MAAGE_CONTEXT_WORLD_IO, MAAGE_OPERATION_INITIALIZE, 0);
}

static void maage_store_gameplay_default(maage_gameplay_save *save) {
    memset(save, 0, sizeof(*save));
    save->mode = MAAGE_GAME_MODE_CREATIVE;
    save->health = save->hunger = 20;
    save->air = 300;
    save->saturation = 5;
}
static int maage_store_gameplay_valid(const maage_gameplay_save *save, const maage_item_registry *items) {
    unsigned int i;
    if (save->experience > 4294967295UL || save->experience_random > 4294967295UL || save->mode > MAAGE_GAME_MODE_CREATIVE || save->health > 20 || save->hunger > 20 || save->air < -19 || save->air > 300 ||
        save->inventory.selected >= MAAGE_HOTBAR_SLOTS ||
        !(save->saturation >= 0 && save->saturation <= save->hunger) ||
        !(save->exhaustion >= 0 && save->exhaustion <= 40) ||
        !(save->fall_distance >= 0 && save->fall_distance < MAAGE_WORLD_LIMIT) ||
        (save->flying != 0 && save->flying != 1) ||
        (save->mode == MAAGE_GAME_MODE_SURVIVAL && save->flying)) return 0;
    for (i = 0; i < MAAGE_INVENTORY_SLOTS; ++i)
        if (!maage_item_stack_valid(items, &save->inventory.slots[i])) return 0;
    if (!maage_item_stack_valid(items, &save->inventory.cursor)) return 0;
    for (i = 0; i < MAAGE_CRAFTING_CELLS; ++i)
        if (!maage_item_stack_valid(items, &save->crafting_inputs[i])) return 0;
    return 1;
}
/* Every ordinary record uses the immutable open-World numeric maps. */
static unsigned long maage_store_saved(const maage_world_store *store, unsigned int domain, unsigned long runtime, maage_store_bytes *bytes) {
    const maage_store_domain *map = &store->domains[domain];
    if (runtime >= map->runtime_count || !map->inverse[runtime]) {
        bytes->failed = 1;
        return 0;
    }
    return map->inverse[runtime];
}
static unsigned long maage_store_runtime(const maage_world_store *store, unsigned int domain, unsigned long saved, maage_store_bytes *bytes) {
    const maage_store_domain *map = &store->domains[domain];
    if (!saved || saved > map->count) {
        bytes->failed = 1;
        return 0;
    }
    return map->entries[saved - 1].runtime;
}
static void maage_store_stack_put(maage_store_bytes *bytes, const maage_item_stack *stack, const maage_world_store *store) {
    if ((unsigned long) stack->metadata > MAAGE_STORE_WORD || (unsigned long) stack->damage > MAAGE_STORE_WORD) {
        bytes->failed = 1;
        return;
    }
    maage_store_put(bytes, stack->count ? maage_store_saved(store, MAAGE_STORE_ITEM, stack->item_id, bytes) : 0, 4);
    maage_store_put(bytes, stack->count, 1);
    maage_store_put(bytes, stack->metadata, 4);
    maage_store_put(bytes, stack->damage, 4);
}
static void maage_store_stack_get(maage_store_bytes *bytes, maage_item_stack *stack, const maage_world_store *store) {
    unsigned long saved, value;
    memset(stack, 0, sizeof(*stack));
    saved = maage_store_get(bytes, 4);
    stack->count = (unsigned int) maage_store_get(bytes, 1);
    if (saved) stack->item_id = maage_store_runtime(store, MAAGE_STORE_ITEM, saved, bytes);
    if ((!saved) != (!stack->count)) bytes->failed = 1;
    value = maage_store_get(bytes, 4);
    stack->metadata = (unsigned int) value;
    if ((unsigned long) stack->metadata != value) bytes->failed = 1;
    value = maage_store_get(bytes, 4);
    stack->damage = (unsigned int) value;
    if ((unsigned long) stack->damage != value || !maage_item_stack_valid(store->items, stack)) bytes->failed = 1;
}
static void maage_store_gameplay_put(maage_store_bytes *bytes, const maage_gameplay_save *save, const maage_world_store *store) {
    unsigned int i;

    maage_store_put(bytes, save->experience, 4);
    maage_store_put(bytes, save->experience_random, 4);
    maage_store_put(bytes, save->mode, 1);
    maage_store_put(bytes, save->health, 1);
    maage_store_put(bytes, save->hunger, 1);
    maage_store_put(bytes, (unsigned long) (save->air + 20), 2);
    maage_store_put(bytes, save->flying ? 1 : 0, 1);
    maage_store_put(bytes, save->inventory.selected, 1);
    maage_store_double_put(bytes, save->saturation);
    maage_store_double_put(bytes, save->exhaustion);
    maage_store_double_put(bytes, save->fall_distance);
    for (i = 0; i < MAAGE_INVENTORY_SLOTS; ++i)
        maage_store_stack_put(bytes, &save->inventory.slots[i], store);
    maage_store_stack_put(bytes, &save->inventory.cursor, store);
    for (i = 0; i < MAAGE_CRAFTING_CELLS; ++i)
        maage_store_stack_put(bytes, &save->crafting_inputs[i], store);
}
static void maage_store_gameplay_get(maage_store_bytes *bytes, maage_gameplay_save *save, const maage_world_store *store) {
    unsigned int i;
    unsigned long air;
    memset(save, 0, sizeof(*save));
    save->experience = maage_store_get(bytes, 4);
    save->experience_random = maage_store_get(bytes, 4);
    save->mode = (unsigned int) maage_store_get(bytes, 1);
    save->health = (unsigned int) maage_store_get(bytes, 1);
    save->hunger = (unsigned int) maage_store_get(bytes, 1);
    air = maage_store_get(bytes, 2);
    if (air < 1 || air > 320) bytes->failed = 1;
    save->air = air >= 1 && air <= 320 ? (int) air - 20 : 300;
    save->flying = (int) maage_store_get(bytes, 1);
    save->inventory.selected = (unsigned int) maage_store_get(bytes, 1);
    save->saturation = maage_store_double_get(bytes);
    save->exhaustion = maage_store_double_get(bytes);
    save->fall_distance = maage_store_double_get(bytes);
    for (i = 0; i < MAAGE_INVENTORY_SLOTS; ++i)
        maage_store_stack_get(bytes, &save->inventory.slots[i], store);
    maage_store_stack_get(bytes, &save->inventory.cursor, store);
    for (i = 0; i < MAAGE_CRAFTING_CELLS; ++i)
        maage_store_stack_get(bytes, &save->crafting_inputs[i], store);
    if (!maage_store_gameplay_valid(save, store->items)) bytes->failed = 1;
}
static void maage_store_entities_put(maage_store_bytes *bytes, const maage_entity_save *save, const maage_world_store *store) {
    size_t i;
    unsigned int axis;
    unsigned long previous = 0;
    const maage_entity *entity;
    if (!save->next_id || save->next_id > MAAGE_STORE_WORD || save->count > MAAGE_STORE_WORD || (save->count && !save->entities)) {
        bytes->failed = 1;
        return;
    }
    maage_store_put(bytes, save->next_id, 4);
    maage_store_put(bytes, save->random, 4);
    maage_store_put(bytes, (unsigned long) save->count, 4);
    for (i = 0; i < save->count; ++i) {
        entity = &save->entities[i];
        if (!maage_entity_valid(entity, store->items) || entity->id <= previous || entity->id >= save->next_id) {
            bytes->failed = 1;
            return;
        }
        previous = entity->id;
        maage_store_put(bytes, entity->id, 4);
        maage_store_put(bytes, entity->type, 1);
        maage_store_put(bytes, entity->age, 4);
        maage_store_put(bytes, entity->pickup_delay, 2);
        maage_store_put(bytes, entity->grounded ? 1 : 0, 1);
        for (axis = 0; axis < 3; ++axis)
            maage_store_double_put(bytes, entity->position[axis]);
        for (axis = 0; axis < 3; ++axis)
            maage_store_double_put(bytes, entity->velocity[axis]);
        maage_store_stack_put(bytes, maage_entity_stack(entity), store);
        if (entity->type == MAAGE_ENTITY_FALLING_BLOCK) {
            maage_store_put(bytes, maage_store_saved(store, MAAGE_STORE_STATE, entity->payload.falling.state, bytes), 4);
            for (axis = 0; axis < 3; ++axis)
                maage_store_put(bytes, (unsigned long) ((long) entity->payload.falling.source[axis] + MAAGE_WORLD_LIMIT), 4);
        }
    }
}
static void maage_store_entities_get(maage_store_bytes *bytes, maage_entity_save *save, const maage_world_store *store) {
    unsigned long count, previous = 0, coordinate;
    const maage_item_definition *item;
    size_t i;
    unsigned int axis;
    maage_entity *entity;
    memset(save, 0, sizeof(*save));
    save->next_id = maage_store_get(bytes, 4);
    save->random = maage_store_get(bytes, 4);
    count = maage_store_get(bytes, 4);
    /* Validate counts against actual bytes before allocating. */
    if (bytes->failed || !save->next_id || count > (bytes->length - bytes->position) / 73 || count > (size_t) - 1 / sizeof(*save->entities)) {
        bytes->failed = 1;
        return;
    }
    if (count) {
        save->entities = (maage_entity *) calloc((size_t) count, sizeof(*save->entities));
        if (!save->entities) {
            bytes->failed = 1;
            bytes->failure = maage_status_make(MAAGE_STATUS_ALLOCATION_FAILED, MAAGE_CONTEXT_WORLD_IO, MAAGE_OPERATION_DECODE, 0);
            return;
        }
    }
    save->count = (size_t) count;
    for (i = 0; i < save->count; ++i) {
        entity = &save->entities[i];
        entity->id = maage_store_get(bytes, 4);
        entity->type = (unsigned int) maage_store_get(bytes, 1);
        entity->age = maage_store_get(bytes, 4);
        entity->pickup_delay = (unsigned int) maage_store_get(bytes, 2);
        entity->grounded = (int) maage_store_get(bytes, 1);
        for (axis = 0; axis < 3; ++axis)
            entity->position[axis] = maage_store_double_get(bytes);
        for (axis = 0; axis < 3; ++axis)
            entity->velocity[axis] = maage_store_double_get(bytes);
        memcpy(entity->previous, entity->position, sizeof(entity->previous));
        if (entity->type == MAAGE_ENTITY_FALLING_BLOCK) {
            maage_store_stack_get(bytes, &entity->payload.falling.stack, store);
            item = maage_item_registry_get(store->items, entity->payload.falling.stack.item_id);
            entity->payload.falling.state = maage_store_runtime(store, MAAGE_STORE_STATE, maage_store_get(bytes, 4), bytes);
            if (!item) bytes->failed = 1;
            for (axis = 0; axis < 3; ++axis) {
                coordinate = maage_store_get(bytes, 4);
                if (coordinate >= 2UL * MAAGE_WORLD_LIMIT) bytes->failed = 1;
                entity->payload.falling.source[axis] = coordinate < 2UL * MAAGE_WORLD_LIMIT ? (int) ((long) coordinate - MAAGE_WORLD_LIMIT) : 0;
            }
        } else maage_store_stack_get(bytes, &entity->payload.item, store);
        if (!maage_entity_valid(entity, store->items) || entity->id <= previous || entity->id >= save->next_id) bytes->failed = 1;
        previous = entity->id;
    }
}
static maage_status maage_store_metadata_write(const maage_world_store *store, const maage_world_record *record, char error[256], maage_status_trace *trace) {
    unsigned char *data;
    maage_store_bytes bytes;
    size_t capacity;
    char path[MAAGE_PATH_CAPACITY];
    unsigned int i;
    if (!maage_generation_policy_valid(&record->generation_policy)) return maage_store_fail(error, "World generation policy is invalid.", MAAGE_STATUS_MALFORMED_INPUT, MAAGE_OPERATION_SAVE, trace);
    if (!maage_store_gameplay_valid(&record->gameplay, store->items)) return maage_store_fail(error, "Player gameplay identity or pickup data is invalid.", MAAGE_STATUS_MALFORMED_INPUT, MAAGE_OPERATION_SAVE, trace);
    if (!maage_random_tick_state_valid(&record->random_ticks)) return maage_store_fail(error, "World random-tick checkpoint is invalid.", MAAGE_STATUS_MALFORMED_INPUT, MAAGE_OPERATION_SAVE, trace);
    if (!maage_environment_state_valid(&record->environment)) return maage_store_fail(error, "World environment snapshot is invalid.", MAAGE_STATUS_MALFORMED_INPUT, MAAGE_OPERATION_SAVE, trace);
    /* Each entity uses at most 96 bytes including its numeric item/state IDs.
     * The inventory/header reserve is independent of world entity count. */
    if (record->entities.count > ((size_t) - 1 - 16384UL) / 96UL) return maage_store_fail(error, "Entity checkpoint exceeds addressable memory.", MAAGE_STATUS_BUFFER_OVERFLOW, MAAGE_OPERATION_SAVE, trace);
    capacity = 16384UL + record->entities.count * 96UL;
    if (store->map_length > (size_t) - 1 - capacity) return maage_store_fail(error, "World ID map exceeds addressable memory.", MAAGE_STATUS_BUFFER_OVERFLOW, MAAGE_OPERATION_SAVE, trace);
    capacity += store->map_length;
    if (capacity > MAAGE_STORE_METADATA_MAX) return maage_store_fail(error, "Entity checkpoint exceeds file encoding capacity.", MAAGE_STATUS_BUFFER_OVERFLOW, MAAGE_OPERATION_SAVE, trace);
    data = (unsigned char *) malloc(capacity);
    if (!data) return maage_store_fail(error, "Not enough memory to encode player metadata.", MAAGE_STATUS_ALLOCATION_FAILED, MAAGE_OPERATION_SAVE, trace);
    bytes.data = data;
    bytes.length = capacity;
    bytes.position = 0;
    bytes.failed = 0;
    bytes.failure = 0;
    maage_store_text_put(&bytes, "MGWRLD12", 8);
    maage_store_put(&bytes, (unsigned long) store->map_length, 4);
    maage_store_text_put(&bytes, (const char *) store->map_bytes, store->map_length);
    maage_store_put(&bytes, record->seed.low, 4);
    maage_store_put(&bytes, record->seed.high, 4);
    maage_store_put(&bytes, strlen(record->id), 1);
    maage_store_put(&bytes, strlen(record->name), 1);
    maage_store_put(&bytes, strlen(record->created_utc), 1);
    maage_store_put(&bytes, record->new_world ? 1 : 0, 1);
    maage_store_text_put(&bytes, record->id, strlen(record->id));
    maage_store_text_put(&bytes, record->name, strlen(record->name));
    maage_store_text_put(&bytes, record->created_utc, strlen(record->created_utc));
    for (i = 0; i < 3; ++i)
        maage_store_double_put(&bytes, record->position[i]);
    maage_store_double_put(&bytes, record->yaw);
    maage_store_double_put(&bytes, record->pitch);
    maage_store_gameplay_put(&bytes, &record->gameplay, store);
    maage_store_entities_put(&bytes, &record->entities, store);
    maage_store_put(&bytes, (unsigned long) record->generation_policy.sea_level, 4);
    maage_store_put(&bytes, (unsigned long) record->generation_policy.spawn_search_radius, 4);
    maage_store_put(&bytes, (unsigned long) record->generation_policy.spawn_search_step, 4);
    maage_store_put(&bytes, record->environment.ticks_low, 4);
    maage_store_put(&bytes, record->environment.ticks_high, 4);
    maage_store_put(&bytes, record->environment.day_tick, 4);
    maage_store_put(&bytes, record->environment.day_low, 4);
    maage_store_put(&bytes, record->environment.day_high, 4);
    maage_store_put(&bytes, record->environment.random, 4);
    maage_store_put(&bytes, record->environment.rain_ticks, 4);
    maage_store_put(&bytes, record->environment.thunder_ticks, 4);
    maage_store_put(&bytes, record->environment.raining, 1);
    maage_store_put(&bytes, record->environment.thundering, 1);
    maage_store_double_put(&bytes, record->environment.rain_intensity);
    maage_store_double_put(&bytes, record->environment.thunder_intensity);
    maage_store_put(&bytes, record->random_ticks.selection, 4);
    maage_store_put(&bytes, record->random_ticks.random, 4);
    maage_store_checksum_put(&bytes);
    if (bytes.failed) {
        free(data);
        if (maage_status_failed(bytes.failure)) return MAAGE_STATUS_PROPAGATE(trace, bytes.failure, MAAGE_CONTEXT_WORLD_IO, MAAGE_OPERATION_SAVE);
        return maage_store_fail(error, "Could not encode world metadata.", MAAGE_STATUS_MALFORMED_INPUT, MAAGE_OPERATION_SAVE, trace);
    }
    if (maage_status_failed(maage_storage_join(path, sizeof(path), store->directory, "world.bin"))) {
        free(data);
        return maage_store_fail(error, "World metadata path is too long.", MAAGE_STATUS_BUFFER_OVERFLOW, MAAGE_OPERATION_SAVE, trace);
    }
    {
        maage_status result = maage_store_write(path, data, bytes.position, error, trace);
        free(data);
        return MAAGE_STATUS_PROPAGATE(trace, result, MAAGE_CONTEXT_WORLD_IO, MAAGE_OPERATION_SAVE);
    }
}
static maage_status maage_store_metadata_load(maage_world_store *store, const char *expected_id, maage_world_record *record, char error[256], maage_status_trace *trace) {
    char path[MAAGE_PATH_CAPACITY];
    maage_store_bytes bytes;
    unsigned long lengths[3], value;
    size_t i, j;
    char *texts[3];
    int ok, gameplay_read = 0;
    maage_status result;
    if (maage_status_failed(maage_storage_join(path, sizeof(path), store->directory, "world.bin"))) return maage_store_fail(error, "World metadata path is too long.", MAAGE_STATUS_BUFFER_OVERFLOW, MAAGE_OPERATION_LOAD, trace);
    result = maage_store_read(path, MAAGE_STORE_METADATA_MAX, &bytes.data, &bytes.length, error, trace);
    if (maage_status_failed(result)) return MAAGE_STATUS_PROPAGATE(trace, result, MAAGE_CONTEXT_WORLD_IO, MAAGE_OPERATION_LOAD);
    bytes.position = 0;
    bytes.failed = 0;
    bytes.failure = 0;
    if (bytes.length >= 8 && memcmp(bytes.data, "MGWRLD12", 8)) {
        free(bytes.data);
        maage_store_error(error, "World was created using an incompatible development metadata format; recreate this development world.");
        return MAAGE_STATUS_TRACE(trace, maage_status_make(MAAGE_STATUS_MALFORMED_INPUT, MAAGE_CONTEXT_WORLD_IO, MAAGE_OPERATION_LOAD, MAAGE_DETAIL_WORLD_FORMAT), MAAGE_CONTEXT_WORLD_IO, MAAGE_OPERATION_LOAD);
    }
    ok = maage_store_checksum(&bytes) && maage_store_magic(&bytes, "MGWRLD12");
    if (ok) {
        unsigned long map_length = maage_store_get(&bytes, 4);
        if (bytes.failed || map_length > bytes.length - bytes.position) ok = 0;
        else {
            result = maage_store_map_open(store, bytes.data + bytes.position, (size_t) map_length, expected_id);
            if (maage_status_failed(result)) {
                bytes.failure = result;
                ok = 0;
            }
            bytes.position += (size_t) map_length;
        }
    }
    if (ok) {
        record->seed.low = maage_store_get(&bytes, 4);
        record->seed.high = maage_store_get(&bytes, 4);
        for (i = 0; i < 3; ++i)
            lengths[i] = maage_store_get(&bytes, 1);
        record->new_world = (int) maage_store_get(&bytes, 1);
        ok = record->new_world <= 1 && lengths[0] == 36 && lengths[1] > 0 && lengths[1] < sizeof(record->name) && lengths[2] == 20;
    }
    if (ok) {
        texts[0] = record->id;
        texts[1] = record->name;
        texts[2] = record->created_utc;
        for (i = 0; i < 3; ++i) {
            for (j = 0; j < lengths[i]; ++j)
                texts[i][j] = (char) maage_store_get(&bytes, 1);
            texts[i][lengths[i]] = 0;
            if (strlen(texts[i]) != lengths[i]) ok = 0;
        }
        for (i = 0; i < 3; ++i)
            record->position[i] = maage_store_double_get(&bytes);
        record->yaw = maage_store_double_get(&bytes);
        record->pitch = maage_store_double_get(&bytes);
        maage_store_gameplay_get(&bytes, &record->gameplay, store);
        maage_store_entities_get(&bytes, &record->entities, store);
        gameplay_read = 1;
        value = maage_store_get(&bytes, 4);
        if (value > INT_MAX) bytes.failed = 1;
        record->generation_policy.sea_level = (int) value;
        value = maage_store_get(&bytes, 4);
        if (value > INT_MAX) bytes.failed = 1;
        record->generation_policy.spawn_search_radius = (int) value;
        value = maage_store_get(&bytes, 4);
        if (value > INT_MAX) bytes.failed = 1;
        record->generation_policy.spawn_search_step = (int) value;
        record->environment.ticks_low = maage_store_get(&bytes, 4);
        record->environment.ticks_high = maage_store_get(&bytes, 4);
        record->environment.day_tick = maage_store_get(&bytes, 4);
        record->environment.day_low = maage_store_get(&bytes, 4);
        record->environment.day_high = maage_store_get(&bytes, 4);
        record->environment.random = maage_store_get(&bytes, 4);
        record->environment.rain_ticks = maage_store_get(&bytes, 4);
        record->environment.thunder_ticks = maage_store_get(&bytes, 4);
        record->environment.raining = (unsigned int) maage_store_get(&bytes, 1);
        record->environment.thundering = (unsigned int) maage_store_get(&bytes, 1);
        record->environment.rain_intensity = maage_store_double_get(&bytes);
        record->environment.thunder_intensity = maage_store_double_get(&bytes);
        record->random_ticks.selection = maage_store_get(&bytes, 4);
        record->random_ticks.random = maage_store_get(&bytes, 4);
        ok = ok && !bytes.failed && maage_generation_policy_valid(&record->generation_policy) &&
             maage_environment_state_valid(&record->environment) && maage_random_tick_state_valid(&record->random_ticks) &&
             bytes.position == bytes.length && maage_storage_uuid_valid(record->id) &&
             !strcmp(record->id, expected_id) && maage_store_name(record->name) && maage_store_player(record);
        for (i = 0; i < 20; ++i) {
            char expected = (i == 4 || i == 7) ? '-' : (i == 10 ? 'T' : ((i == 13 || i == 16) ? ':' : (i == 19 ? 'Z' : 0)));
            if (expected ? record->created_utc[i] != expected : (record->created_utc[i] < '0' || record->created_utc[i] > '9')) ok = 0;
        }
    }
    free(bytes.data);
    if (!ok && gameplay_read) {
        maage_gameplay_save_clear(&record->gameplay);
        maage_entity_save_clear(&record->entities);
    }
    if (ok) return maage_status_make(MAAGE_STATUS_SUCCESS, 0, MAAGE_OPERATION_LOAD, 0);
    if (maage_status_failed(bytes.failure)) {
        maage_store_error(error, "World metadata could not be decoded; original data preserved.");
        return MAAGE_STATUS_PROPAGATE(trace, bytes.failure, MAAGE_CONTEXT_WORLD_IO, MAAGE_OPERATION_LOAD);
    }
    return maage_store_fail(error, "World metadata is corrupt or uses an unsupported format.", MAAGE_STATUS_MALFORMED_INPUT, MAAGE_OPERATION_LOAD, trace);
}
maage_status maage_world_store_open(const char *instance_root, const char *id, int create, const char *name, const maage_world_seed *seed, const maage_generation_policy *generation_policy, const maage_content *content, maage_world_store **output, maage_world_record *record, char error[256], maage_status_trace *trace) {
    char worlds[MAAGE_PATH_CAPACITY], config[MAAGE_PATH_CAPACITY], path[MAAGE_PATH_CAPACITY];
    char last[MAAGE_PATH_CAPACITY], chosen[37];
    maage_world_store *store = NULL;
    maage_storage_lock *selection = NULL;
    unsigned char *selected, random_seed[8];
    size_t length, i;
    time_t now;
    struct tm *utc;
    int kind;
    maage_status result;
    if (output) *output = NULL;
    if (error) error[0] = 0;
    if (!instance_root || !output || !record || !content) return maage_store_fail(error, "Missing world storage initialization arguments.", MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_OPERATION_LOAD, trace);
    if ((!create && id && seed) || (name && !create && !seed)) return maage_store_fail(error, "A seed or world name requires world creation; saved worlds cannot be overridden.", MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_OPERATION_LOAD, trace);
    if (generation_policy && !maage_generation_policy_valid(generation_policy)) return maage_store_fail(error, "Invalid generation policy.", MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_OPERATION_LOAD, trace);
    if (!maage_item_registry_is_finalized(maage_content_items(content))) return maage_store_fail(error, "World storage requires finalized item definitions.", MAAGE_STATUS_INVALID_STATE, MAAGE_OPERATION_LOAD, trace);
    if ((id && !maage_storage_uuid_valid(id)) || (name && !maage_store_name(name))) return maage_store_fail(error, "World IDs must be canonical UUIDs; world names are separate display metadata.", MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_OPERATION_LOAD, trace);
    if (!maage_folder_link_managed_path(instance_root, 0)) return maage_store_fail(error, "World storage requires a physical instance root.", MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_OPERATION_LOAD, trace);
#define MAAGE_STORE_STEP(expression, message)    \
    do {                                         \
        result = (expression);                   \
        if (maage_status_failed(result)) {       \
            maage_store_error(error, (message)); \
            goto done;                           \
        }                                        \
    } while (0)
    MAAGE_STORE_STEP(maage_storage_join(path, sizeof(path), instance_root, "instance.json"), "Instance manifest path is too long.");
    if (maage_storage_status(path) != 1) {
        result = maage_store_fail(error, "World storage requires a physical instance root, never the global data directory.", MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_OPERATION_LOAD, trace);
        goto done;
    }
    MAAGE_STORE_STEP(maage_storage_join(worlds, sizeof(worlds), instance_root, "worlds"), "World directory path is too long.");
    MAAGE_STORE_STEP(maage_storage_directories(worlds), "Could not prepare world directories.");
    MAAGE_STORE_STEP(maage_storage_join(config, sizeof(config), instance_root, "config"), "Instance config path is too long.");
    MAAGE_STORE_STEP(maage_storage_directories(config), "Could not prepare instance configuration.");
    MAAGE_STORE_STEP(maage_storage_join(last, sizeof(last), config, "last-world"), "World selection path is too long.");
    MAAGE_STORE_STEP(maage_storage_join(path, sizeof(path), worlds, ".selection.lock"), "World selection lock path is too long.");
    MAAGE_STORE_STEP(maage_storage_acquire(path, &selection, trace), "Another process is selecting or creating a world.");
    if (id) strcpy(chosen, id);
    else if (create || seed) {
        create = 1;
        MAAGE_STORE_STEP(maage_storage_uuid_generate(chosen), "Could not obtain secure randomness for world creation.");
    } else {
        kind = maage_storage_status(last);
        if (!kind) {
            create = 1;
            MAAGE_STORE_STEP(maage_storage_uuid_generate(chosen), "Could not obtain secure randomness for world creation.");
        } else {
            MAAGE_STORE_STEP(maage_store_read(last, 37, &selected, &length, error, trace), "Could not read last-world selection.");
            if (length && selected[length - 1] == '\n') --length;
            if (length != 36 || memchr(selected, 0, length)) {
                free(selected);
                result = maage_store_fail(error, "The last-world selection is corrupt.", MAAGE_STATUS_MALFORMED_INPUT, MAAGE_OPERATION_LOAD, trace);
                goto done;
            }
            memcpy(chosen, selected, length);
            chosen[length] = 0;
            free(selected);
            if (!maage_storage_uuid_valid(chosen)) {
                result = maage_store_fail(error, "The last-world selection is invalid.", MAAGE_STATUS_MALFORMED_INPUT, MAAGE_OPERATION_LOAD, trace);
                goto done;
            }
        }
    }
    store = (maage_world_store *) calloc(1, sizeof(*store));
    if (!store) {
        result = maage_store_fail(error, "Not enough memory to open world storage.", MAAGE_STATUS_ALLOCATION_FAILED, MAAGE_OPERATION_LOAD, trace);
        goto done;
    }
    store->content = content;
    store->blocks = maage_content_blocks(content);
    store->biomes = maage_content_biomes(content);
    store->items = maage_content_items(content);
    MAAGE_STORE_STEP(maage_storage_join(store->directory, sizeof(store->directory), worlds, chosen), "World path is too long.");
    kind = maage_storage_status(store->directory);
    if (create ? kind != 0 : kind != 2) {
        result = maage_store_fail(error, create ? "The requested world already exists or cannot be safely created." : "The requested world does not exist or is inaccessible.",
                                  create ? MAAGE_STATUS_INVALID_STATE : MAAGE_STATUS_NOT_FOUND, MAAGE_OPERATION_LOAD, trace);
        goto done;
    }
    MAAGE_STORE_STEP(maage_storage_directories(store->directory), "Could not prepare world directory.");
    MAAGE_STORE_STEP(maage_storage_join(path, sizeof(path), store->directory, ".world.lock"), "World lock path is too long.");
    MAAGE_STORE_STEP(maage_storage_acquire(path, &store->lock, trace), "This world is already open in another process.");
    if (!create && id && generation_policy) {
        result = maage_store_fail(error, "Cannot override a saved world's generation policy.", MAAGE_STATUS_INVALID_STATE, MAAGE_OPERATION_LOAD, trace);
        goto done;
    }
    if (create) {
        if (generation_policy) store->record.generation_policy = *generation_policy;
        else
            MAAGE_STORE_STEP(maage_generation_policy_default(&store->record.generation_policy), "Cannot initialize world generation policy.");
        strcpy(store->record.id, chosen);
        strcpy(store->record.name, name ? name : "New World");
        store->record.new_world = 1;
        store->record.position[1] = 128;
        maage_store_gameplay_default(&store->record.gameplay);
        store->record.entities.next_id = 1;
        store->record.entities.random = store->record.seed.low ^ store->record.seed.high;
        if (seed) store->record.seed = *seed;
        else {
            MAAGE_STORE_STEP(maage_storage_random(random_seed, sizeof(random_seed)), "Could not obtain secure randomness for world creation.");
            for (i = 0; i < 4; ++i) {
                store->record.seed.low |= (unsigned long) random_seed[i] << (i * 8);
                store->record.seed.high |= (unsigned long) random_seed[i + 4] << (i * 8);
            }
        }
        store->record.seed.high &= MAAGE_STORE_WORD;
        store->record.seed.low &= MAAGE_STORE_WORD;
        store->record.entities.random = store->record.seed.low ^ store->record.seed.high;
        now = time(NULL);
        utc = gmtime(&now);
        if (now == (time_t) -1 || !utc || strftime(store->record.created_utc, sizeof(store->record.created_utc), "%Y-%m-%dT%H:%M:%SZ", utc) != 20) {
            result = maage_store_fail(error, "Could not determine the world's creation time.", MAAGE_STATUS_IO_ERROR, MAAGE_OPERATION_LOAD, trace);
            goto done;
        }
        MAAGE_STORE_STEP(maage_environment_default(store->record.seed, &store->record.environment), "Could not initialize environment.");
        MAAGE_STORE_STEP(maage_random_tick_default(store->record.seed, &store->record.random_ticks), "Could not initialize random-tick streams.");
        MAAGE_STORE_STEP(maage_store_map_open(store, NULL, 0, chosen), "Could not initialize World ID map.");
        MAAGE_STORE_STEP(maage_store_metadata_write(store, &store->record, error, trace), "Could not save new world metadata.");
    } else
        MAAGE_STORE_STEP(maage_store_metadata_load(store, chosen, &store->record, error, trace), "Could not load world metadata.");
    if (store->map_extended) MAAGE_STORE_STEP(maage_store_metadata_write(store, &store->record, error, trace), "Could not extend World ID map safely.");
    MAAGE_STORE_STEP(maage_storage_join(store->regions_dir, sizeof(store->regions_dir), store->directory, "regions"), "Region storage path is too long.");
    MAAGE_STORE_STEP(maage_storage_directories(store->regions_dir), "Could not prepare region storage.");
    MAAGE_STORE_STEP(maage_region_store_create(store->regions_dir, store->map_id, &store->regions), "Could not initialize indexed region storage.");
    length = strlen(chosen);
    chosen[length++] = '\n';
    MAAGE_STORE_STEP(maage_store_write(last, (const unsigned char *) chosen, length, error, trace), "Could not persist world selection.");
    *record = store->record;
    *output = store;
    store = NULL;
    result = maage_status_make(MAAGE_STATUS_SUCCESS, 0, MAAGE_OPERATION_LOAD, 0);
done:
    maage_world_store_close(store);
    maage_storage_release(selection);
    return MAAGE_STATUS_PROPAGATE(trace, result, MAAGE_CONTEXT_WORLD_IO, MAAGE_OPERATION_LOAD);
#undef MAAGE_STORE_STEP
}

maage_status maage_world_store_save(maage_world_store *store, const double position[3], double yaw, double pitch, const maage_gameplay *game, const maage_world *world, const maage_environment_state *environment, char error[256], maage_status_trace *trace) {
    maage_world_record record;
    maage_status result;
    if (!store || !position || !game || !world || !environment) return maage_store_fail(error, "Missing world snapshot.", MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_OPERATION_SAVE, trace);
    record = store->record;
    memcpy(record.position, position, sizeof(record.position));
    record.yaw = yaw;
    record.pitch = pitch;
    record.environment = *environment;
    record.new_world = 0;
    result = maage_world_random_tick_snapshot(world, &record.random_ticks);
    if (maage_status_failed(result)) return MAAGE_STATUS_PROPAGATE(trace, result, MAAGE_CONTEXT_WORLD_IO, MAAGE_OPERATION_SAVE);
    if (!maage_store_player(&record) || !maage_environment_state_valid(&record.environment)) return maage_store_fail(error, "World snapshot is invalid.", MAAGE_STATUS_MALFORMED_INPUT, MAAGE_OPERATION_SAVE, trace);
    result = maage_gameplay_snapshot(game, &record.gameplay);
    if (maage_status_failed(result)) {
        maage_store_error(error, "Could not snapshot player gameplay.");
        return MAAGE_STATUS_PROPAGATE(trace, result, MAAGE_CONTEXT_WORLD_IO, MAAGE_OPERATION_SAVE);
    }
    result = maage_entities_snapshot(maage_world_entities(world), &record.entities);
    if (!maage_status_failed(result)) result = maage_store_metadata_write(store, &record, error, trace);
    maage_entity_save_clear(&record.entities);
    maage_gameplay_save_clear(&record.gameplay);
    return MAAGE_STATUS_PROPAGATE(trace, result, MAAGE_CONTEXT_WORLD_IO, MAAGE_OPERATION_SAVE);
}
maage_status maage_world_store_close(maage_world_store *store) {
    maage_status result;
    if (!store) return maage_status_make(MAAGE_STATUS_NOOP, 0, MAAGE_OPERATION_CLOSE, 0);
    maage_entity_save_clear(&store->record.entities);
    maage_gameplay_save_clear(&store->record.gameplay);
    result = maage_region_store_destroy(store->regions);
    {
        maage_status released = maage_storage_release(store->lock);
        if (!maage_status_failed(result)) result = released;
    }
    maage_store_map_clear(store);
    free(store);
    return maage_status_failed(result) ? result : maage_status_make(MAAGE_STATUS_SUCCESS, 0, MAAGE_OPERATION_CLOSE, 0);
}
static int maage_store_coordinates(int x, int z) {
    int limit = MAAGE_WORLD_LIMIT / MAAGE_CHUNK_EDGE;
    return x >= -limit && x <= limit && z >= -limit && z <= limit;
}
static int maage_store_state_compare(const void *left, const void *right) {
    maage_block_state_id a = *(const maage_block_state_id *) left;
    maage_block_state_id b = *(const maage_block_state_id *) right;
    return a < b ? -1 : (a > b ? 1 : 0);
}
static int maage_store_biome_compare(const void *left, const void *right) {
    maage_biome_id a = *(const maage_biome_id *) left;
    maage_biome_id b = *(const maage_biome_id *) right;
    return a < b ? -1 : (a > b ? 1 : 0);
}
static size_t maage_store_state_index(const maage_block_state_id *states, size_t count, maage_block_state_id value) {
    size_t left = 0, right = count, middle;
    while (left < right) {
        middle = left + (right - left) / 2;
        if (states[middle] < value) left = middle + 1;
        else
            right = middle;
    }
    return left < count && states[left] == value ? left : count;
}
static size_t maage_store_biome_index(const maage_biome_id *biomes, size_t count, maage_biome_id value) {
    size_t left = 0, right = count, middle;
    while (left < right) {
        middle = left + (right - left) / 2;
        if (biomes[middle] < value) left = middle + 1;
        else
            right = middle;
    }
    return left < count && biomes[left] == value ? left : count;
}
typedef struct maage_store_entity_codec {
    maage_store_bytes *bytes;
    const maage_world_store *store;
    int writing;
} maage_store_entity_codec;
static maage_status maage_store_entity_word(void *context, unsigned long *word, unsigned int width) {
    maage_store_entity_codec *codec = (maage_store_entity_codec *) context;
    if (!width || width > 4) return maage_status_make(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_CONTEXT_WORLD_IO, codec->writing ? MAAGE_OPERATION_WRITE : MAAGE_OPERATION_DECODE, 0);
    if (codec->writing && *word > (width == 4 ? MAAGE_STORE_WORD : ((1UL << (width * 8U)) - 1UL))) return maage_status_make(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_CONTEXT_WORLD_IO, MAAGE_OPERATION_WRITE, 0);
    if (codec->writing) maage_store_put(codec->bytes, *word, width);
    else *word = maage_store_get(codec->bytes, width);
    if (maage_status_failed(codec->bytes->failure)) return codec->bytes->failure;
    return maage_status_make(codec->bytes->failed ? MAAGE_STATUS_MALFORMED_INPUT : MAAGE_STATUS_SUCCESS, MAAGE_CONTEXT_WORLD_IO, codec->writing ? MAAGE_OPERATION_WRITE : MAAGE_OPERATION_DECODE, 0);
}
static maage_status maage_store_entity_stack(void *context, maage_item_stack *stack) {
    maage_store_entity_codec *codec = (maage_store_entity_codec *) context;
    if (codec->writing) maage_store_stack_put(codec->bytes, stack, codec->store);
    else maage_store_stack_get(codec->bytes, stack, codec->store);
    if (maage_status_failed(codec->bytes->failure)) return codec->bytes->failure;
    return maage_status_make(codec->bytes->failed ? MAAGE_STATUS_MALFORMED_INPUT : MAAGE_STATUS_SUCCESS, MAAGE_CONTEXT_WORLD_IO, codec->writing ? MAAGE_OPERATION_WRITE : MAAGE_OPERATION_DECODE, 0);
}
static const maage_block_entity_handler *maage_store_entity_handler(const maage_block_registry *blocks, const maage_block_state_definition *state) {
    const maage_block_definition *block = state ? maage_block_registry_block(blocks, state->block_id) : NULL;
    return block ? block->entity : NULL;
}
static int maage_store_chunk_simulation_get(maage_store_bytes *bytes, maage_chunk *chunk, const maage_block_registry *blocks, const maage_world_store *store) {
    const maage_block_entity_handler *handler;
    maage_store_entity_codec adapter;
    maage_entity_codec codec;
    maage_status result;
    unsigned long saved_block;
    size_t end;
    const maage_registry_bindings *ids = maage_block_registry_bindings(blocks);
    maage_scheduled_update update;
    maage_block_entity *entity;
    unsigned char occupied[8192];
    unsigned long count, i, length, stages;
    const maage_block_state_definition *state;
    size_t before;
    memset(occupied, 0, sizeof(occupied));
    stages = maage_store_get(bytes, 4);
    if (stages > 255UL) return 0;
    chunk->generation_stages = (unsigned int) stages;
    for (i = 0; i < 8; ++i)
        chunk->generation_features[i] = maage_store_get(bytes, 4);
    chunk->environment_initialized = (int) maage_store_get(bytes, 1);
    count = maage_store_get(bytes, 4);
    if (bytes->failed || chunk->environment_initialized > 1 || count > 196608UL || count > (bytes->length - bytes->position) / 14UL) return 0;
    for (i = 0; i < count; ++i) {
        update.kind = (unsigned int) maage_store_get(bytes, 4);
        if (update.kind != MAAGE_BLOCK_UPDATE_FLUID && update.kind != MAAGE_BLOCK_UPDATE_SUPPORT && update.kind != MAAGE_BLOCK_UPDATE_CONTENT) return 0;
        update.cell = (unsigned int) maage_store_get(bytes, 2);
        update.due_low = maage_store_get(bytes, 4);
        update.due_high = maage_store_get(bytes, 4);
        before = chunk->update_count;
        if (bytes->failed) return 0;
        bytes->failure = maage_chunk_schedule_update(chunk, &update);
        if (maage_status_failed(bytes->failure) || chunk->update_count != before + 1) return 0;
    }
    count = maage_store_get(bytes, 4);
    if (bytes->failed || count > 65536UL || count > (bytes->length - bytes->position) / 14UL || count > (size_t) - 1 / sizeof(*chunk->entities)) return 0;
    if (count) {
        chunk->entities = (maage_block_entity *) calloc((size_t) count, sizeof(*chunk->entities));
        if (!chunk->entities) {
            bytes->failure = maage_status_make(MAAGE_STATUS_ALLOCATION_FAILED, MAAGE_CONTEXT_WORLD_IO, MAAGE_OPERATION_DECODE, 0);
            return 0;
        }
        chunk->entity_capacity = (size_t) count;
    }
    for (i = 0; i < count; ++i) {
        entity = &chunk->entities[chunk->entity_count++];
        entity->cell = (unsigned int) maage_store_get(bytes, 2);
        saved_block = maage_store_runtime(store, MAAGE_STORE_BLOCK, maage_store_get(bytes, 4), bytes);
        entity->version = maage_store_get(bytes, 4);
        length = maage_store_get(bytes, 4);
        if (bytes->failed || !entity->version || length > 65536UL || length > bytes->length - bytes->position || (occupied[entity->cell / 8] & (1U << (entity->cell % 8)))) return 0;
        state = maage_block_registry_state(blocks, maage_chunk_get(chunk, (int) (entity->cell % 16), (int) (entity->cell / 256), (int) ((entity->cell / 16) % 16)));
        if (!state || state->block_id == ids->block_air) return 0;
        occupied[entity->cell / 8] |= (unsigned char) (1U << (entity->cell % 8));
        handler = maage_store_entity_handler(blocks, state);
        if (!handler || state->block_id != saved_block || entity->version != handler->version || length != handler->encoded_size) return 0;
        entity->type = handler->id;
        entity->length = handler->size;
        entity->data = (unsigned char *) calloc(1, handler->size);
        if (!entity->data) {
            bytes->failure = maage_status_make(MAAGE_STATUS_ALLOCATION_FAILED, MAAGE_CONTEXT_WORLD_IO, MAAGE_OPERATION_DECODE, 0);
            return 0;
        }
        end = bytes->position + (size_t) length;
        adapter.bytes = bytes;
        adapter.store = store;
        adapter.writing = 0;
        codec.context = &adapter;
        codec.word = maage_store_entity_word;
        codec.stack = maage_store_entity_stack;
        result = handler->decode(entity->data, &codec);
        if (maage_status_failed(result)) {
            bytes->failure = result;
            return 0;
        }
        if (bytes->failed || bytes->position != end || !handler->valid(handler, state, store->items, entity->data)) return 0;
    }
    chunk->updates_relative = 1;
    return !bytes->failed;
}
static int maage_store_chunk_simulation_size(const maage_chunk *chunk, const maage_block_registry *blocks, const maage_world_store *store, size_t *length) {
    const maage_block_entity_handler *handler;
    const maage_registry_bindings *ids = maage_block_registry_bindings(blocks);
    unsigned char occupied[24576];
    const maage_block_entity *entity;
    const maage_block_state_definition *state;
    size_t i, size = 45UL;
    if (chunk->update_count > 196608UL || chunk->entity_count > 65536UL || chunk->generation_stages > 255U || (chunk->environment_initialized != 0 && chunk->environment_initialized != 1)) return 0;
    size += chunk->update_count * 14UL + chunk->entity_count * 14UL;
    memset(occupied, 0, sizeof(occupied));
    for (i = 0; i < chunk->update_count; ++i) {
        const maage_scheduled_update *update = &chunk->updates[i];
        if ((update->kind != MAAGE_BLOCK_UPDATE_FLUID && update->kind != MAAGE_BLOCK_UPDATE_SUPPORT && update->kind != MAAGE_BLOCK_UPDATE_CONTENT) || update->cell > 65535U ||
            update->due_low > MAAGE_STORE_WORD || update->due_high > MAAGE_STORE_WORD ||
            (occupied[(update->kind - 1U) * 8192U + update->cell / 8] & (1U << (update->cell % 8)))) return 0;
        occupied[(update->kind - 1U) * 8192U + update->cell / 8] |= (unsigned char) (1U << (update->cell % 8));
    }
    memset(occupied, 0, sizeof(occupied));
    for (i = 0; i < chunk->entity_count; ++i) {
        entity = &chunk->entities[i];
        if (entity->cell > 65535U || !entity->type || entity->type > MAAGE_STORE_WORD || !entity->version ||
            entity->version > MAAGE_STORE_WORD || entity->length > 65536UL || (entity->length && !entity->data) ||
            (occupied[entity->cell / 8] & (1U << (entity->cell % 8))) || entity->length > MAAGE_STORE_CHUNK_MAX - size) return 0;
        state = maage_block_registry_state(blocks, maage_chunk_get(chunk, (int) (entity->cell % 16), (int) (entity->cell / 256), (int) ((entity->cell / 16) % 16)));
        if (!state || state->block_id == ids->block_air) return 0;
        occupied[entity->cell / 8] |= (unsigned char) (1U << (entity->cell % 8));
        handler = maage_store_entity_handler(blocks, state);
        if (!handler || entity->type != handler->id || entity->version != handler->version || entity->length != handler->size || !handler->valid(handler, state, store->items, entity->data) || handler->encoded_size > MAAGE_STORE_CHUNK_MAX - size) return 0;
        size += handler->encoded_size;
    }
    for (i = 0; i < 8; ++i)
        if (chunk->generation_features[i] > MAAGE_STORE_WORD) return 0;
    *length = size;
    return 1;
}
static void maage_store_chunk_simulation_put(maage_store_bytes *bytes, const maage_chunk *chunk, const maage_world_store *store, unsigned long now_low, unsigned long now_high) {
    size_t i, end;
    const maage_block_state_definition *state;
    const maage_block_entity_handler *handler;
    maage_store_entity_codec adapter;
    maage_entity_codec codec;
    maage_status result;
    unsigned long low, high;
    const maage_scheduled_update *update;
    const maage_block_entity *entity;
    maage_store_put(bytes, chunk->generation_stages, 4);
    for (i = 0; i < 8; ++i)
        maage_store_put(bytes, chunk->generation_features[i], 4);
    maage_store_put(bytes, chunk->environment_initialized ? 1UL : 0UL, 1);
    maage_store_put(bytes, (unsigned long) chunk->update_count, 4);
    for (i = 0; i < chunk->update_count; ++i) {
        update = &chunk->updates[i];
        low = high = 0;
        if (chunk->updates_relative) {
            low = update->due_low;
            high = update->due_high;
        } else if (update->due_high > now_high || (update->due_high == now_high && update->due_low > now_low)) {
            low = (update->due_low - now_low) & MAAGE_STORE_WORD;
            high = (update->due_high - now_high - (update->due_low < now_low ? 1UL : 0UL)) & MAAGE_STORE_WORD;
        }
        maage_store_put(bytes, update->kind, 4);
        maage_store_put(bytes, update->cell, 2);
        maage_store_put(bytes, low, 4);
        maage_store_put(bytes, high, 4);
    }
    maage_store_put(bytes, (unsigned long) chunk->entity_count, 4);
    for (i = 0; i < chunk->entity_count; ++i) {
        entity = &chunk->entities[i];
        maage_store_put(bytes, entity->cell, 2);
        state = maage_block_registry_state(store->blocks, maage_chunk_get(chunk, entity->cell % 16, entity->cell / 256, (entity->cell / 16) % 16));
        handler = maage_store_entity_handler(store->blocks, state);
        if (!handler) {
            bytes->failed = 1;
            return;
        }
        maage_store_put(bytes, maage_store_saved(store, MAAGE_STORE_BLOCK, state->block_id, bytes), 4);
        maage_store_put(bytes, entity->version, 4);
        maage_store_put(bytes, (unsigned long) handler->encoded_size, 4);
        adapter.bytes = bytes;
        adapter.store = store;
        adapter.writing = 1;
        codec.context = &adapter;
        codec.word = maage_store_entity_word;
        codec.stack = maage_store_entity_stack;
        end = bytes->position + handler->encoded_size;
        result = handler->encode(entity->data, &codec);
        if (maage_status_failed(result)) {
            bytes->failure = result;
            bytes->failed = 1;
            return;
        }
        if (bytes->position != end) {
            bytes->failed = 1;
            return;
        }
    }
}
static maage_status maage_store_chunk_decode(const maage_world_store *store, unsigned char *data, size_t length, int x, int z, const maage_block_registry *blocks, maage_chunk **output, maage_status_trace *trace) {
    maage_store_bytes bytes;
    maage_chunk *chunk = NULL;
    struct maage_store_palette_entry {
        maage_block_state_id state;
        int entity;
    } *palette = NULL;
    maage_biome_id biome_palette[256];
    unsigned long mask, stored_x, stored_z, expected_x, expected_z, value;
    unsigned long state_count, biome_count;
    size_t i, section, payload_length = 768UL + 45UL;
    size_t active_cells = 0, entity_cells = 0, entity_count = 0;
    const maage_registry_bindings *ids = maage_block_registry_bindings(blocks);
    maage_status result = maage_status_make(MAAGE_STATUS_MALFORMED_INPUT, MAAGE_CONTEXT_WORLD_IO, MAAGE_OPERATION_DECODE, 0);
    *output = NULL;
    bytes.data = data;
    bytes.length = length;
    bytes.position = 0;
    bytes.failed = 0;
    bytes.failure = 0;
    if (length < 845UL || !maage_store_checksum(&bytes) || !maage_store_magic(&bytes, "MGCHNK9\0")) return MAAGE_STATUS_PROPAGATE(trace, result, MAAGE_CONTEXT_WORLD_IO, MAAGE_OPERATION_DECODE);
    stored_x = maage_store_get(&bytes, 4);
    stored_z = maage_store_get(&bytes, 4);
    expected_x = (unsigned long) x & MAAGE_STORE_WORD;
    expected_z = (unsigned long) z & MAAGE_STORE_WORD;
    if (stored_x != expected_x || stored_z != expected_z) return MAAGE_STATUS_PROPAGATE(trace, result, MAAGE_CONTEXT_WORLD_IO, MAAGE_OPERATION_DECODE);
    mask = maage_store_get(&bytes, 2);
    if (maage_store_get(&bytes, 2) != 0 || bytes.failed) return MAAGE_STATUS_PROPAGATE(trace, result, MAAGE_CONTEXT_WORLD_IO, MAAGE_OPERATION_DECODE);
    for (section = 0; section < 16; ++section)
        if (mask & (1UL << section)) {
            active_cells += MAAGE_SECTION_VOLUME;
            payload_length += MAAGE_SECTION_VOLUME * 3UL;
        }
    state_count = maage_store_get(&bytes, 4);
    biome_count = maage_store_get(&bytes, 2);
    if (maage_store_get(&bytes, 2) != 0 || bytes.failed ||
        state_count > active_cells || (active_cells && !state_count) ||
        biome_count == 0 || biome_count > 256 ||
        bytes.length - bytes.position < payload_length + state_count * 4UL + biome_count * 4UL)
        return MAAGE_STATUS_PROPAGATE(trace, result, MAAGE_CONTEXT_WORLD_IO, MAAGE_OPERATION_DECODE);
    if (state_count) {
        if (state_count > (size_t) - 1 / sizeof(*palette)) return MAAGE_STATUS_PROPAGATE(trace, result, MAAGE_CONTEXT_WORLD_IO, MAAGE_OPERATION_DECODE);
        palette = (struct maage_store_palette_entry *) malloc((size_t) state_count * sizeof(*palette));
        if (!palette) {
            result = maage_status_make(MAAGE_STATUS_ALLOCATION_FAILED, MAAGE_CONTEXT_WORLD_IO, MAAGE_OPERATION_DECODE, 0);
            return MAAGE_STATUS_PROPAGATE(trace, result, MAAGE_CONTEXT_WORLD_IO, MAAGE_OPERATION_DECODE);
        }
    }
    for (i = 0; i < state_count; ++i) {
        const maage_block_state_definition *state;
        palette[i].state = maage_store_runtime(store, MAAGE_STORE_STATE, maage_store_get(&bytes, 4), &bytes);
        state = maage_block_registry_state(blocks, palette[i].state);
        if (!state) {
            bytes.failed = 1;
            break;
        }
        palette[i].entity = maage_store_entity_handler(blocks, state) != NULL;
    }
    for (i = 0; i < biome_count; ++i)
        biome_palette[i] = maage_store_runtime(store, MAAGE_STORE_BIOME, maage_store_get(&bytes, 4), &bytes);
    if (bytes.failed || bytes.length - bytes.position < payload_length) goto failed;
    result = maage_chunk_create(x, z, maage_block_registry_bindings(blocks)->state_air, &chunk);
    if (maage_status_failed(result)) goto failed;
    result = maage_status_make(MAAGE_STATUS_MALFORMED_INPUT, MAAGE_CONTEXT_WORLD_IO, MAAGE_OPERATION_DECODE, 0);
    for (i = 0; i < 256; ++i) {
        value = maage_store_get(&bytes, 1);
        if (value >= biome_count) goto failed;
        chunk->biomes[i] = biome_palette[value];
    }
    for (i = 0; i < 256; ++i) {
        value = maage_store_get(&bytes, 2);
        if (value > MAAGE_WORLD_HEIGHT) goto failed;
        chunk->heightmap[i] = (unsigned short) value;
    }
    for (section = 0; section < 16; ++section) {
        if (!(mask & (1UL << section))) continue;
        chunk->sections[section] = maage_section_create(chunk->air);
        if (!chunk->sections[section]) {
            result = maage_status_make(MAAGE_STATUS_ALLOCATION_FAILED, MAAGE_CONTEXT_WORLD_IO, MAAGE_OPERATION_DECODE, 0);
            goto failed;
        }
        /* Bulk-validate the remaining payload before decoding section
         * indices. Lighting is already a byte-for-byte serialized array. */
        if (bytes.position > bytes.length || bytes.length - bytes.position < MAAGE_SECTION_VOLUME * 3UL) goto failed;
        for (i = 0; i < MAAGE_SECTION_VOLUME; ++i) {
            size_t offset = bytes.position + i * 2UL;
            value = (unsigned long) bytes.data[offset] | ((unsigned long) bytes.data[offset + 1] << 8);
            if (value >= state_count) goto failed;
            chunk->sections[section]->states[i] = palette[value].state;
            entity_cells += palette[value].entity;
        }
        bytes.position += MAAGE_SECTION_VOLUME * 2UL;
        memcpy(chunk->sections[section]->light, bytes.data + bytes.position, MAAGE_SECTION_VOLUME);
        bytes.position += MAAGE_SECTION_VOLUME;
    }
    if (!maage_store_chunk_simulation_get(&bytes, chunk, blocks, store) ||
        chunk->generation_stages != (1U << MAAGE_GENERATION_STAGE_COUNT) - 1U ||
        bytes.failed || bytes.position != bytes.length) goto failed;
    for (i = 0; i < chunk->entity_count; ++i)
        if (maage_block_registry_entity(blocks, chunk->entities[i].type)) ++entity_count;
    if (entity_cells != entity_count) goto failed;
    free(palette);
    *output = chunk;
    return maage_status_make(MAAGE_STATUS_SUCCESS, 0, MAAGE_OPERATION_DECODE, 0);
failed:
    if (maage_status_failed(bytes.failure)) result = bytes.failure;
    free(palette);
    maage_chunk_destroy(chunk);
    return MAAGE_STATUS_PROPAGATE(trace, result, MAAGE_CONTEXT_WORLD_IO, MAAGE_OPERATION_DECODE);
}
maage_status maage_world_store_load_chunk(maage_world_store *store, int x, int z, const maage_block_registry *blocks, const maage_biome_registry *biomes, maage_chunk **chunk, char error[256], maage_status_trace *trace) {
    unsigned char *data;
    size_t length;
    maage_status result;
    if (chunk) *chunk = NULL;
    if (!chunk || !store || !blocks || !biomes || blocks != store->blocks || biomes != store->biomes) return maage_store_fail(error, "Missing chunk load arguments.", MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_OPERATION_LOAD, trace);
    if (!maage_store_coordinates(x, z)) return maage_store_fail(error, "Chunk coordinates exceed the world format bounds.", MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_OPERATION_LOAD, trace);
    result = maage_region_store_load(store->regions, x, z, &data, &length);
    if (maage_status_code(result) == MAAGE_STATUS_NOT_FOUND) return result;
    if (maage_status_failed(result)) {
        maage_store_error(error, "Region entry could not be read or decompressed; authoritative data preserved.");
        return MAAGE_STATUS_PROPAGATE(trace, result, MAAGE_CONTEXT_WORLD_IO, MAAGE_OPERATION_LOAD);
    }
    result = maage_store_chunk_decode(store, data, length, x, z, blocks, chunk, trace);
    free(data);
    if (maage_status_failed(result)) {
        maage_store_error(error, "Chunk data is corrupt, unmapped, incompatible, or could not be allocated; the save was preserved.");
        return MAAGE_STATUS_PROPAGATE(trace, result, MAAGE_CONTEXT_WORLD_IO, MAAGE_OPERATION_LOAD);
    }
    return maage_status_make(MAAGE_STATUS_SUCCESS, 0, MAAGE_OPERATION_LOAD, 0);
}

maage_status maage_world_store_save_chunk_at(maage_world_store *store, const maage_chunk *chunk, const maage_block_registry *blocks, const maage_biome_registry *biomes, unsigned long tick_low, unsigned long tick_high, char error[256], maage_status_trace *trace) {
    maage_store_bytes bytes;
    maage_block_state_id *states = NULL;
    size_t *state_slots = NULL, slot_capacity = 1;
    unsigned char *encoded = NULL;
    maage_biome_id biome_ids[256];
    unsigned long mask = 0;
    size_t i, section, j, unique = 0, biome_count = 0, cells = 0;
    size_t capacity = 800UL, simulation_length, entity_cells = 0, entity_count = 0;
    const maage_registry_bindings *ids = maage_block_registry_bindings(blocks);
    maage_status result;
    if (!store || !chunk || !blocks || !biomes || blocks != store->blocks || biomes != store->biomes || !maage_store_coordinates(chunk->x, chunk->z)) {
        maage_store_error(error, "Chunk coordinates or path exceed the world format bounds.");
        return MAAGE_STATUS_TRACE(trace, maage_status_make(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_CONTEXT_WORLD_IO, MAAGE_OPERATION_SAVE, !store ? 0 : (!chunk ? 1 : (!blocks ? 2 : (!biomes ? 3 : 1)))), MAAGE_CONTEXT_WORLD_IO, MAAGE_OPERATION_SAVE);
    }
    /* Persist only fully finalized feature-capable columns. A failed or
     * cancelled generation stage must never become a durable partial world. */
    if (chunk->generation_stages != (1U << MAAGE_GENERATION_STAGE_COUNT) - 1U) {
        maage_store_error(error, "Refusing to persist a column with incomplete generation stages.");
        return MAAGE_STATUS_TRACE(trace, maage_status_make(MAAGE_STATUS_INVARIANT_VIOLATION, MAAGE_CONTEXT_WORLD_IO, MAAGE_OPERATION_SAVE, 0), MAAGE_CONTEXT_WORLD_IO, MAAGE_OPERATION_SAVE);
    }
    if (tick_low > MAAGE_STORE_WORD || tick_high > MAAGE_STORE_WORD || !maage_store_chunk_simulation_size(chunk, blocks, store, &simulation_length)) goto incompatible;
    capacity += simulation_length;
    for (section = 0; section < 16; ++section)
        if (chunk->sections[section]) {
            mask |= 1UL << section;
            cells += MAAGE_SECTION_VOLUME;
        }
    if (cells) {
        if (cells > (size_t) - 1 / sizeof(*states) || cells > (size_t) - 1 / 2U) goto incompatible;
        while (slot_capacity < cells * 2U) {
            if (slot_capacity > (size_t) - 1 / 2U) goto incompatible;
            slot_capacity *= 2U;
        }
        if (slot_capacity > (size_t) - 1 / sizeof(*state_slots)) goto incompatible;
        states = (maage_block_state_id *) malloc(cells * sizeof(*states));
        state_slots = (size_t *) calloc(slot_capacity, sizeof(*state_slots));
        if (!states || !state_slots) {
            free(states);
            free(state_slots);
            maage_store_error(error, "Not enough memory for the chunk identity palette.");
            return MAAGE_STATUS_TRACE(trace, maage_status_make(MAAGE_STATUS_ALLOCATION_FAILED, MAAGE_CONTEXT_WORLD_IO, MAAGE_OPERATION_SAVE, 0), MAAGE_CONTEXT_WORLD_IO, MAAGE_OPERATION_SAVE);
        }
    }
    /* Hash only unique identities; sorting up to 65,536 repeated block
     * values per save added unnecessary O(n log n) CPU work. The final sorted
     * palette remains byte-for-byte deterministic across platforms. */
    for (section = 0; section < 16; ++section) {
        if (!chunk->sections[section]) continue;
        for (i = 0; i < MAAGE_SECTION_VOLUME; ++i) {
            maage_block_state_id id = maage_section_get(chunk->sections[section], i);
            size_t bucket = (size_t) ((id ^ (id >> 16)) * 2654435761UL) & (slot_capacity - 1U);
            const maage_block_state_definition *state = maage_block_registry_state(blocks, id);
            if (!state) goto incompatible;
            if (maage_store_entity_handler(blocks, state) != NULL) ++entity_cells;
            while (state_slots[bucket] && states[state_slots[bucket] - 1U] != id)
                bucket = (bucket + 1U) & (slot_capacity - 1U);
            if (!state_slots[bucket]) {
                states[unique] = id;
                state_slots[bucket] = ++unique;
            }
        }
    }
    for (i = 0; i < chunk->entity_count; ++i)
        if (maage_block_registry_entity(blocks, chunk->entities[i].type)) ++entity_count;
    if (entity_cells != entity_count) goto incompatible;
    free(state_slots);
    state_slots = NULL;
    if (unique) qsort(states, unique, sizeof(*states), maage_store_state_compare);
    for (i = 0; i < 256; ++i) {
        if (!maage_biome_registry_get(biomes, chunk->biomes[i])) goto incompatible;
        biome_ids[i] = chunk->biomes[i];
    }
    qsort(biome_ids, 256, sizeof(biome_ids[0]), maage_store_biome_compare);
    for (i = 0; i < 256; ++i)
        if (!biome_count || biome_ids[i] != biome_ids[biome_count - 1])
            biome_ids[biome_count++] = biome_ids[i];
    capacity += cells * 3UL + unique * 4UL + biome_count * 4UL;
    if (capacity > MAAGE_STORE_CHUNK_MAX) goto incompatible;
    encoded = (unsigned char *) malloc(capacity);
    bytes.data = encoded;
    if (!bytes.data) {
        free(states);
        maage_store_error(error, "Not enough memory to encode a chunk.");
        return MAAGE_STATUS_TRACE(trace, maage_status_make(MAAGE_STATUS_ALLOCATION_FAILED, MAAGE_CONTEXT_WORLD_IO, MAAGE_OPERATION_SAVE, 0), MAAGE_CONTEXT_WORLD_IO, MAAGE_OPERATION_SAVE);
    }
    bytes.length = capacity;
    bytes.position = 0;
    bytes.failed = 0;
    bytes.failure = 0;
    maage_store_text_put(&bytes, "MGCHNK9\0", 8);
    maage_store_put(&bytes, (unsigned long) chunk->x, 4);
    maage_store_put(&bytes, (unsigned long) chunk->z, 4);
    maage_store_put(&bytes, mask, 2);
    maage_store_put(&bytes, 0, 2);
    maage_store_put(&bytes, (unsigned long) unique, 4);
    maage_store_put(&bytes, (unsigned long) biome_count, 2);
    maage_store_put(&bytes, 0, 2);
    for (i = 0; i < unique; ++i)
        maage_store_put(&bytes, maage_store_saved(store, MAAGE_STORE_STATE, states[i], &bytes), 4);
    for (i = 0; i < biome_count; ++i)
        maage_store_put(&bytes, maage_store_saved(store, MAAGE_STORE_BIOME, biome_ids[i], &bytes), 4);
    for (i = 0; i < 256; ++i) {
        j = maage_store_biome_index(biome_ids, biome_count, chunk->biomes[i]);
        if (j == biome_count) goto incompatible;
        maage_store_put(&bytes, (unsigned long) j, 1);
    }
    for (i = 0; i < 256; ++i) {
        if (chunk->heightmap[i] > MAAGE_WORLD_HEIGHT) goto incompatible;
        maage_store_put(&bytes, chunk->heightmap[i], 2);
    }
    for (section = 0; section < 16; ++section) {
        if (!chunk->sections[section]) continue;
        for (i = 0; i < MAAGE_SECTION_VOLUME; ++i) {
            j = maage_store_state_index(states, unique, maage_section_get(chunk->sections[section], i));
            if (j == unique || j > 65535UL) goto incompatible;
            maage_store_put(&bytes, (unsigned long) j, 2);
        }
        for (i = 0; i < MAAGE_SECTION_VOLUME; ++i)
            maage_store_put(&bytes, chunk->sections[section]->light[i], 1);
    }
    maage_store_chunk_simulation_put(&bytes, chunk, store, tick_low, tick_high);
    maage_store_checksum_put(&bytes);
    result = !bytes.failed && bytes.position == capacity ? maage_region_store_save(store->regions, chunk->x, chunk->z, bytes.data, bytes.position) : maage_status_make(MAAGE_STATUS_INVARIANT_VIOLATION, MAAGE_CONTEXT_WORLD_IO, MAAGE_OPERATION_SAVE, 0);
    free(encoded);
    free(states);
    if (maage_status_failed(result)) maage_store_error(error, "Chunk save failed; the previous file was preserved.");
    return MAAGE_STATUS_PROPAGATE(trace, result, MAAGE_CONTEXT_WORLD_IO, MAAGE_OPERATION_SAVE);
incompatible:
    free(encoded);
    free(states);
    free(state_slots);
    maage_store_error(error, "Chunk has invalid heights or unresolved registered content identities.");
    return MAAGE_STATUS_TRACE(trace, maage_status_make(MAAGE_STATUS_INVALID_STATE, MAAGE_CONTEXT_WORLD_IO, MAAGE_OPERATION_SAVE, 0), MAAGE_CONTEXT_WORLD_IO, MAAGE_OPERATION_SAVE);
}

/* Catalog reads use the same current-format decoder as authoritative startup. */
typedef struct maage_store_catalog {
    const char *root;
    const maage_content *content;
    maage_world_summary *entries;
    size_t count, capacity;
    maage_status result;
} maage_store_catalog;
static int maage_store_catalog_entry(const char *name, maage_folder_link_kind kind, void *context) {
    maage_store_catalog *catalog = (maage_store_catalog *) context;
    maage_world_store *store;
    maage_world_summary *grown, *entry;
    char error[256];
    size_t capacity;
    if (kind != MAAGE_FOLDER_LINK_DIRECTORY || !maage_storage_uuid_valid(name)) return 1;
    if (catalog->count == catalog->capacity) {
        capacity = catalog->capacity ? catalog->capacity * 2 : 16;
        if (capacity < catalog->capacity || capacity > (size_t) - 1 / sizeof(*grown)) goto memory;
        grown = (maage_world_summary *) realloc(catalog->entries, capacity * sizeof(*grown));
        if (!grown) goto memory;
        catalog->entries = grown;
        catalog->capacity = capacity;
    }
    store = (maage_world_store *) calloc(1, sizeof(*store));
    if (!store) goto memory;
    store->content = catalog->content;
    store->blocks = maage_content_blocks(store->content);
    store->biomes = maage_content_biomes(store->content);
    store->items = maage_content_items(store->content);
    entry = &catalog->entries[catalog->count++];
    memset(entry, 0, sizeof(*entry));
    strcpy(entry->id, name);
    strcpy(entry->name, "Unreadable world");
    entry->status = maage_storage_join(store->directory, sizeof(store->directory), catalog->root, name);
    if (!maage_status_failed(entry->status)) entry->status = maage_store_metadata_load(store, name, &store->record, error, NULL);
    if (!maage_status_failed(entry->status)) {
        strcpy(entry->name, store->record.name);
        strcpy(entry->created_utc, store->record.created_utc);
        entry->game_mode = store->record.gameplay.mode;
    }
    maage_entity_save_clear(&store->record.entities);
    maage_gameplay_save_clear(&store->record.gameplay);
    maage_store_map_clear(store);
    free(store);
    return 1;
memory:
    catalog->result = maage_status_make(MAAGE_STATUS_ALLOCATION_FAILED, MAAGE_CONTEXT_WORLD_IO, MAAGE_OPERATION_LOAD, 0);
    return 0;
}
static int maage_store_catalog_order(const void *left, const void *right) {
    const maage_world_summary *a = (const maage_world_summary *) left;
    const maage_world_summary *b = (const maage_world_summary *) right;
    int order = strcmp(b->created_utc, a->created_utc);
    return order ? order : strcmp(a->id, b->id);
}
maage_status maage_world_store_list(const char *instance_root, const maage_content *content, maage_world_summary **entries, size_t *count, char error[256], maage_status_trace *trace) {
    maage_store_catalog catalog;
    char path[MAAGE_PATH_CAPACITY];
    maage_status result;
    if (!entries || !count || !content || !maage_item_registry_is_finalized(maage_content_items(content)) || !maage_folder_link_managed_path(instance_root, 0)) return maage_store_fail(error, "Invalid world catalog arguments.", MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_OPERATION_LOAD, trace);
    *entries = NULL;
    *count = 0;
    result = maage_storage_join(path, sizeof(path), instance_root, "instance.json");
    if (maage_status_failed(result)) return result;
    if (maage_storage_status(path) != 1) return maage_store_fail(error, "A physical instance is required.", MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_OPERATION_LOAD, trace);
    result = maage_storage_join(path, sizeof(path), instance_root, "worlds");
    if (maage_status_failed(result)) return result;
    if (!maage_storage_status(path)) return maage_status_make(MAAGE_STATUS_SUCCESS, 0, MAAGE_OPERATION_LOAD, 0);
    memset(&catalog, 0, sizeof(catalog));
    catalog.root = path;
    catalog.content = content;
    result = maage_folder_link_list(path, maage_store_catalog_entry, &catalog);
    if (maage_status_failed(catalog.result)) result = catalog.result;
    if (maage_status_failed(result)) {
        free(catalog.entries);
        return MAAGE_STATUS_PROPAGATE(trace, result, MAAGE_CONTEXT_WORLD_IO, MAAGE_OPERATION_LOAD);
    }
    if (catalog.count > 1) qsort(catalog.entries, catalog.count, sizeof(*catalog.entries), maage_store_catalog_order);
    *entries = catalog.entries;
    *count = catalog.count;
    return result;
}
maage_status maage_world_store_rename(const char *instance_root, const char *id, const char *name, const maage_content *content, char error[256], maage_status_trace *trace) {
    maage_world_store *store;
    char worlds[MAAGE_PATH_CAPACITY], path[MAAGE_PATH_CAPACITY];
    maage_storage_lock *selection = NULL;
    maage_status result, closed;
    if (!name || !maage_store_name(name) || !id || !maage_storage_uuid_valid(id) ||
        !content || !maage_item_registry_is_finalized(maage_content_items(content)) ||
        !maage_folder_link_managed_path(instance_root, 0))
        return maage_store_fail(error, "Invalid world rename arguments.", MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_OPERATION_SAVE, trace);
    store = (maage_world_store *) calloc(1, sizeof(*store));
    if (!store) return maage_store_fail(error, "Cannot allocate world metadata.", MAAGE_STATUS_ALLOCATION_FAILED, MAAGE_OPERATION_SAVE, trace);
    store->content = content;
    store->blocks = maage_content_blocks(content);
    store->biomes = maage_content_biomes(content);
    store->items = maage_content_items(content);
    result = maage_storage_join(path, sizeof(path), instance_root, "instance.json");
    if (!maage_status_failed(result) && maage_storage_status(path) != 1) result = maage_status_make(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_CONTEXT_WORLD_IO, MAAGE_OPERATION_VALIDATE, 0);
    if (!maage_status_failed(result)) result = maage_storage_join(worlds, sizeof(worlds), instance_root, "worlds");
    if (!maage_status_failed(result)) result = maage_storage_join(store->directory, sizeof(store->directory), worlds, id);
    if (!maage_status_failed(result) && !maage_folder_link_managed_path(store->directory, 0)) result = maage_status_make(MAAGE_STATUS_ACCESS_DENIED, MAAGE_CONTEXT_WORLD_IO, MAAGE_OPERATION_SAVE, 0);
    if (!maage_status_failed(result)) result = maage_storage_join(path, sizeof(path), worlds, ".selection.lock");
    if (!maage_status_failed(result)) result = maage_storage_acquire(path, &selection, trace);
    if (!maage_status_failed(result)) result = maage_storage_join(path, sizeof(path), store->directory, ".world.lock");
    if (!maage_status_failed(result)) result = maage_storage_acquire(path, &store->lock, trace);
    if (!maage_status_failed(result)) result = maage_store_metadata_load(store, id, &store->record, error, trace);
    if (!maage_status_failed(result)) {
        strcpy(store->record.name, name);
        result = maage_store_metadata_write(store, &store->record, error, trace);
    }
    closed = maage_world_store_close(store);
    if (!maage_status_failed(result)) result = closed;
    closed = maage_storage_release(selection);
    return maage_status_failed(result) ? result : closed;
}

typedef struct maage_store_remove {
    const char *path;
    int inside_storage;
    maage_status result;
} maage_store_remove;
static int maage_store_remove_entry(const char *name, maage_folder_link_kind kind, void *context) {
    maage_store_remove *remove = (maage_store_remove *) context, child;
    char path[MAAGE_PATH_CAPACITY];
    if (!strcmp(name, ".world.lock")) return 1;
    remove->result = maage_storage_join(path, sizeof(path), remove->path, name);
    if (maage_status_failed(remove->result)) return 0;
    if (kind == MAAGE_FOLDER_LINK_FILE) remove->result = maage_storage_remove(path);
    else if (!remove->inside_storage && kind == MAAGE_FOLDER_LINK_DIRECTORY && (!strcmp(name, "regions") || !strcmp(name, "chunks"))) {
        /* A world may contain the current region directory or disposable
         * pre-alpha chunk files. Neither format may be loaded interchangeably,
         * but deletion must safely remove the entire selected world. Only one
         * level of named storage is traversable; links/nested dirs are denied. */
        child.path = path;
        child.inside_storage = 1;
        child.result = 0;
        remove->result = maage_folder_link_list(path, maage_store_remove_entry, &child);
        if (maage_status_failed(child.result)) remove->result = child.result;
        if (!maage_status_failed(remove->result)) remove->result = maage_storage_remove_directory(path);
    } else
        remove->result = maage_status_make(MAAGE_STATUS_ACCESS_DENIED, MAAGE_CONTEXT_WORLD_IO, MAAGE_OPERATION_REMOVE, 0);
    return !maage_status_failed(remove->result);
}
maage_status maage_world_store_delete(const char *instance_root, const char *id, char error[256], maage_status_trace *trace) {
    char worlds[MAAGE_PATH_CAPACITY], directory[MAAGE_PATH_CAPACITY], path[MAAGE_PATH_CAPACITY];
    maage_storage_lock *selection = NULL, *lock = NULL;
    maage_store_remove remove;
    maage_status result, closed;
    if (!id || !maage_storage_uuid_valid(id) || !maage_folder_link_managed_path(instance_root, 0)) return maage_store_fail(error, "Invalid world deletion arguments.", MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_OPERATION_REMOVE, trace);
    result = maage_storage_join(path, sizeof(path), instance_root, "instance.json");
    if (!maage_status_failed(result) && maage_storage_status(path) != 1) result = maage_status_make(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_CONTEXT_WORLD_IO, MAAGE_OPERATION_VALIDATE, 0);
    if (!maage_status_failed(result)) result = maage_storage_join(worlds, sizeof(worlds), instance_root, "worlds");
    if (!maage_status_failed(result)) result = maage_storage_join(directory, sizeof(directory), worlds, id);
    if (!maage_status_failed(result) && !maage_folder_link_managed_path(directory, 0)) result = maage_status_make(MAAGE_STATUS_ACCESS_DENIED, MAAGE_CONTEXT_WORLD_IO, MAAGE_OPERATION_REMOVE, 0);
    if (!maage_status_failed(result)) result = maage_storage_join(path, sizeof(path), worlds, ".selection.lock");
    if (!maage_status_failed(result)) result = maage_storage_acquire(path, &selection, trace);
    if (!maage_status_failed(result)) result = maage_storage_join(path, sizeof(path), directory, ".world.lock");
    if (!maage_status_failed(result)) result = maage_storage_acquire(path, &lock, trace);
    if (!maage_status_failed(result)) {
        remove.path = directory;
        remove.inside_storage = 0;
        remove.result = 0;
        result = maage_folder_link_list(directory, maage_store_remove_entry, &remove);
        if (maage_status_failed(remove.result)) result = remove.result;
    }
    closed = maage_storage_release(lock);
    if (!maage_status_failed(result)) result = closed;
    if (!maage_status_failed(result)) result = maage_storage_remove(path);
    if (!maage_status_failed(result)) result = maage_storage_remove_directory(directory);
    if (!maage_status_failed(result)) {
        unsigned char *selected = NULL;
        size_t length = 0;
        result = maage_storage_join(path, sizeof(path), instance_root, "config/last-world");
        if (!maage_status_failed(result) && maage_storage_status(path) == 1) {
            result = maage_store_read(path, 37, &selected, &length, error, trace);
            if (!maage_status_failed(result) && (length == 36 || (length == 37 && selected[36] == '\n')) && !memcmp(selected, id, 36)) result = maage_storage_remove(path);
            free(selected);
        }
    }
    closed = maage_storage_release(selection);
    if (!maage_status_failed(result)) result = closed;
    return MAAGE_STATUS_PROPAGATE(trace, result, MAAGE_CONTEXT_WORLD_IO, MAAGE_OPERATION_REMOVE);
}
