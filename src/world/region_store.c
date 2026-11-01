#include "region_store.h"
#include "maage/storage.h"
#include "maage/thread.h"
#include "maage/log.h"
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zlib.h>

/* Private append-only region files. Each record is independently compressed
 * and committed by its trailing marker; a partial final append is ignored on
 * reopen and truncated before the next write. No old format is recognized. */
#define MAAGE_REGION_EDGE 32
#define MAAGE_REGION_SLOTS 1024
#define MAAGE_REGION_HEADER 16L
#define MAAGE_REGION_RECORD 24L
#define MAAGE_REGION_FOOTER 4L
#define MAAGE_REGION_CHUNK_MAX 18000000UL
#define MAAGE_REGION_MAGIC "MGRGN1\0"
#define MAAGE_REGION_RECORD_MAGIC "MGR1"
#define MAAGE_REGION_END "DONE"

typedef struct maage_region_entry {
    long offset;
    unsigned long raw_size, compressed_size, raw_crc, compressed_crc;
} maage_region_entry;
typedef struct maage_region {
    int x, z, initialized, broken, needs_trim;
    FILE *file;
    long valid_end;
    maage_mutex *mutex;
    maage_region_entry entries[MAAGE_REGION_SLOTS];
    struct maage_region *next;
} maage_region;
struct maage_region_store {
    char directory[MAAGE_PATH_CAPACITY];
    maage_mutex *mutex;
    maage_region *regions;
};

static maage_status region_status(unsigned char code, unsigned char operation) {
    return maage_status_make(code, MAAGE_CONTEXT_WORLD_IO, operation, 0);
}
static unsigned long region_word(const unsigned char *data, unsigned count) {
    unsigned long value = 0;
    unsigned i;
    for (i = 0; i < count; ++i) value |= (unsigned long) data[i] << (i * 8U);
    return value;
}
static void region_put(unsigned char *data, unsigned long value, unsigned count) {
    unsigned i;
    for (i = 0; i < count; ++i) {
        data[i] = (unsigned char) (value & 255UL);
        value >>= 8;
    }
}
static unsigned long region_crc(const unsigned char *data, size_t length) {
    uLong crc = crc32(0L, Z_NULL, 0);
    while (length) {
        uInt step = length > (size_t) UINT_MAX ? UINT_MAX : (uInt) length;
        crc = crc32(crc, data, step);
        data += step;
        length -= step;
    }
    return (unsigned long) crc & 0xffffffffUL;
}
static int region_coordinates(int coordinate) {
    return coordinate >= 0 ? coordinate / MAAGE_REGION_EDGE : -1 - (-1 - coordinate) / MAAGE_REGION_EDGE;
}
static unsigned region_slot(int x, int z, int rx, int rz) {
    return (unsigned) ((z - rz * MAAGE_REGION_EDGE) * MAAGE_REGION_EDGE + x - rx * MAAGE_REGION_EDGE);
}
static maage_status region_get(maage_region_store *store, int x, int z, maage_region **output) {
    maage_region *region;
    maage_status result;
    int rx = region_coordinates(x), rz = region_coordinates(z);
    maage_mutex_lock(store->mutex);
    for (region = store->regions; region; region = region->next)
        if (region->x == rx && region->z == rz) break;
    if (!region) {
        region = (maage_region *) calloc(1, sizeof(*region));
        if (!region) {
            maage_mutex_unlock(store->mutex);
            return region_status(MAAGE_STATUS_ALLOCATION_FAILED, MAAGE_OPERATION_CREATE);
        }
        region->x = rx;
        region->z = rz;
        result = maage_mutex_create(&region->mutex);
        if (maage_status_failed(result)) {
            free(region);
            maage_mutex_unlock(store->mutex);
            return result;
        }
        region->next = store->regions;
        store->regions = region;
    }
    *output = region;
    maage_mutex_unlock(store->mutex);
    return region_status(MAAGE_STATUS_SUCCESS, MAAGE_OPERATION_LOOKUP);
}
static maage_status region_path(const maage_region_store *store, const maage_region *region, char path[MAAGE_PATH_CAPACITY]) {
    char filename[96];
    sprintf(filename, "r.%d.%d.mgr", region->x, region->z);
    return maage_storage_join(path, MAAGE_PATH_CAPACITY, store->directory, filename);
}
static maage_status region_scan(maage_region *region) {
    unsigned char header[MAAGE_REGION_RECORD], footer[MAAGE_REGION_FOOTER];
    long end, position;
    unsigned slot;
    unsigned long raw_size, zip_size;
    unsigned long maximum_zip = (unsigned long) compressBound((uLong) MAAGE_REGION_CHUNK_MAX);
    maage_region_entry *entry;
    if (fseek(region->file, 0L, SEEK_END) || (end = ftell(region->file)) < MAAGE_REGION_HEADER ||
        fseek(region->file, 0L, SEEK_SET) || fread(header, 1, MAAGE_REGION_HEADER, region->file) != MAAGE_REGION_HEADER)
        return region_status(MAAGE_STATUS_MALFORMED_INPUT, MAAGE_OPERATION_LOAD);
    if (memcmp(header, MAAGE_REGION_MAGIC, 8) ||
        region_word(header + 8, 4) != ((unsigned long) region->x & 0xffffffffUL) ||
        region_word(header + 12, 4) != ((unsigned long) region->z & 0xffffffffUL))
        return region_status(MAAGE_STATUS_MALFORMED_INPUT, MAAGE_OPERATION_LOAD);
    position = MAAGE_REGION_HEADER;
    while (position < end) {
        if (end - position < MAAGE_REGION_RECORD) break; /* torn final append */
        if (fseek(region->file, position, SEEK_SET) ||
            fread(header, 1, MAAGE_REGION_RECORD, region->file) != MAAGE_REGION_RECORD)
            return region_status(MAAGE_STATUS_IO_ERROR, MAAGE_OPERATION_READ);
        slot = (unsigned) region_word(header + 4, 2);
        raw_size = region_word(header + 8, 4);
        zip_size = region_word(header + 12, 4);
        if (memcmp(header, MAAGE_REGION_RECORD_MAGIC, 4) || slot >= MAAGE_REGION_SLOTS ||
            region_word(header + 6, 2) || !raw_size || raw_size > MAAGE_REGION_CHUNK_MAX ||
            !zip_size || zip_size > maximum_zip)
            return region_status(MAAGE_STATUS_MALFORMED_INPUT, MAAGE_OPERATION_LOAD);
        if (zip_size > (unsigned long) (end - position - MAAGE_REGION_RECORD) ||
            end - position - MAAGE_REGION_RECORD - (long) zip_size < MAAGE_REGION_FOOTER) break;
        if (fseek(region->file, position + MAAGE_REGION_RECORD + (long) zip_size, SEEK_SET) ||
            fread(footer, 1, MAAGE_REGION_FOOTER, region->file) != MAAGE_REGION_FOOTER)
            return region_status(MAAGE_STATUS_IO_ERROR, MAAGE_OPERATION_READ);
        if (memcmp(footer, MAAGE_REGION_END, MAAGE_REGION_FOOTER))
            return region_status(MAAGE_STATUS_MALFORMED_INPUT, MAAGE_OPERATION_LOAD);
        entry = &region->entries[slot];
        entry->offset = position + MAAGE_REGION_RECORD;
        entry->raw_size = raw_size;
        entry->compressed_size = zip_size;
        entry->raw_crc = region_word(header + 16, 4);
        entry->compressed_crc = region_word(header + 20, 4);
        position += MAAGE_REGION_RECORD + (long) zip_size + MAAGE_REGION_FOOTER;
    }
    region->valid_end = position;
    region->needs_trim = position < end;
    return region_status(MAAGE_STATUS_SUCCESS, MAAGE_OPERATION_LOAD);
}
static maage_status region_open(maage_region_store *store, maage_region *region, int create) {
    unsigned char header[MAAGE_REGION_HEADER];
    char path[MAAGE_PATH_CAPACITY];
    maage_status result;
    int status;
    if (region->broken) return region_status(MAAGE_STATUS_IO_ERROR, MAAGE_OPERATION_OPEN);
    if (region->initialized && region->file) return region_status(MAAGE_STATUS_SUCCESS, MAAGE_OPERATION_OPEN);
    if (region->initialized && !create) return region_status(MAAGE_STATUS_NOT_FOUND, MAAGE_OPERATION_LOAD);
    result = region_path(store, region, path);
    if (maage_status_failed(result)) return result;
    status = maage_storage_status(path);
    if (status < 0 || status == 2) return region_status(MAAGE_STATUS_IO_ERROR, MAAGE_OPERATION_OPEN);
    if (!status && !create) {
        region->initialized = 1;
        return region_status(MAAGE_STATUS_NOT_FOUND, MAAGE_OPERATION_LOAD);
    }
    region->file = maage_storage_open(path, status ? "r+b" : "w+b");
    if (!region->file) return region_status(MAAGE_STATUS_IO_ERROR, MAAGE_OPERATION_OPEN);
    if (!status) {
        memset(header, 0, sizeof(header));
        memcpy(header, MAAGE_REGION_MAGIC, 8);
        region_put(header + 8, (unsigned long) region->x & 0xffffffffUL, 4);
        region_put(header + 12, (unsigned long) region->z & 0xffffffffUL, 4);
        if (fwrite(header, 1, sizeof(header), region->file) != sizeof(header)) {
            fclose(region->file);
            region->file = NULL;
            return region_status(MAAGE_STATUS_IO_ERROR, MAAGE_OPERATION_WRITE);
        }
        result = maage_storage_private_file(path);
        if (maage_status_failed(result)) {
            fclose(region->file);
            region->file = NULL;
            return result;
        }
        region->valid_end = MAAGE_REGION_HEADER;
    } else {
        result = region_scan(region);
        if (maage_status_failed(result)) {
            fclose(region->file);
            region->file = NULL;
            return result;
        }
    }
    region->initialized = 1;
    return region_status(MAAGE_STATUS_SUCCESS, MAAGE_OPERATION_OPEN);
}
maage_status maage_region_store_create(const char *directory, maage_region_store **output) {
    maage_region_store *store;
    maage_status result;
    if (output) *output = NULL;
    if (!directory || !output || strlen(directory) >= MAAGE_PATH_CAPACITY) return region_status(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_OPERATION_CREATE);
    store = (maage_region_store *) calloc(1, sizeof(*store));
    if (!store) return region_status(MAAGE_STATUS_ALLOCATION_FAILED, MAAGE_OPERATION_CREATE);
    strcpy(store->directory, directory);
    result = maage_mutex_create(&store->mutex);
    if (maage_status_failed(result)) {
        free(store);
        return result;
    }
    *output = store;
    return region_status(MAAGE_STATUS_SUCCESS, MAAGE_OPERATION_CREATE);
}
/* Copy only the latest committed record of each slot to a fresh private
 * region. Compaction runs after all World I/O jobs have drained; the old file
 * stays intact until the complete, synced replacement is atomically promoted.
 * Rewriting the region on each chunk save would defeat incremental storage. */
static maage_status region_compact_on_close(const maage_region_store *store, maage_region *region) {
    char path[MAAGE_PATH_CAPACITY], temporary[MAAGE_PATH_CAPACITY];
    unsigned char header[MAAGE_REGION_HEADER], *record = NULL;
    FILE *output = NULL;
    maage_status result;
    size_t length;
    unsigned slot;
    long live = MAAGE_REGION_HEADER;

    if (!region->file || region->broken) return region_status(MAAGE_STATUS_NOOP, MAAGE_OPERATION_CLOSE);
    for (slot = 0; slot < MAAGE_REGION_SLOTS; ++slot) {
        const maage_region_entry *entry = &region->entries[slot];
        if (!entry->offset) continue;
        if (entry->compressed_size > (unsigned long) LONG_MAX - (unsigned long) live - MAAGE_REGION_RECORD - MAAGE_REGION_FOOTER)
            return region_status(MAAGE_STATUS_BUFFER_OVERFLOW, MAAGE_OPERATION_SAVE);
        live += MAAGE_REGION_RECORD + (long) entry->compressed_size + MAAGE_REGION_FOOTER;
    }
    /* Compact when stale records exceed both live data and a modest seek/
     * synchronization threshold; this is not a storage capacity limit. */
    if (region->valid_end - live <= live || region->valid_end - live <= 1048576L)
        return region_status(MAAGE_STATUS_NOOP, MAAGE_OPERATION_CLOSE);
    result = region_path(store, region, path);
    if (maage_status_failed(result)) return result;
    if (strlen(path) + 5U > sizeof(temporary)) return region_status(MAAGE_STATUS_BUFFER_OVERFLOW, MAAGE_OPERATION_SAVE);
    strcpy(temporary, path);
    strcat(temporary, ".tmp");
    result = maage_storage_remove(temporary);
    if (maage_status_failed(result)) return result;
    output = maage_storage_open(temporary, "wb");
    if (!output) return region_status(MAAGE_STATUS_IO_ERROR, MAAGE_OPERATION_OPEN);
    memset(header, 0, sizeof(header));
    memcpy(header, MAAGE_REGION_MAGIC, 8);
    region_put(header + 8, (unsigned long) region->x & 0xffffffffUL, 4);
    region_put(header + 12, (unsigned long) region->z & 0xffffffffUL, 4);
    if (fwrite(header, 1, sizeof(header), output) != sizeof(header)) {
        result = region_status(MAAGE_STATUS_IO_ERROR, MAAGE_OPERATION_WRITE);
        goto failed;
    }
    for (slot = 0; slot < MAAGE_REGION_SLOTS; ++slot) {
        const maage_region_entry *entry = &region->entries[slot];
        if (!entry->offset) continue;
        length = (size_t) entry->compressed_size + MAAGE_REGION_RECORD + MAAGE_REGION_FOOTER;
        record = (unsigned char *) malloc(length);
        if (!record) {
            result = region_status(MAAGE_STATUS_ALLOCATION_FAILED, MAAGE_OPERATION_SAVE);
            goto failed;
        }
        if (fseek(region->file, entry->offset - MAAGE_REGION_RECORD, SEEK_SET) ||
            fread(record, 1, length, region->file) != length) {
            result = region_status(MAAGE_STATUS_IO_ERROR, MAAGE_OPERATION_READ);
            goto failed;
        }
        if (memcmp(record, MAAGE_REGION_RECORD_MAGIC, 4) ||
            region_word(record + 4, 2) != slot ||
            region_word(record + 8, 4) != entry->raw_size ||
            region_word(record + 12, 4) != entry->compressed_size ||
            memcmp(record + length - MAAGE_REGION_FOOTER, MAAGE_REGION_END, MAAGE_REGION_FOOTER) ||
            region_crc(record + MAAGE_REGION_RECORD, (size_t) entry->compressed_size) != entry->compressed_crc) {
            result = region_status(MAAGE_STATUS_MALFORMED_INPUT, MAAGE_OPERATION_SAVE);
            goto failed;
        }
        if (fwrite(record, 1, length, output) != length) {
            result = region_status(MAAGE_STATUS_IO_ERROR, MAAGE_OPERATION_WRITE);
            goto failed;
        }
        free(record);
        record = NULL;
    }
    result = maage_storage_sync(output);
    if (fclose(output) && !maage_status_failed(result)) result = region_status(MAAGE_STATUS_IO_ERROR, MAAGE_OPERATION_CLOSE);
    output = NULL;
    if (maage_status_failed(result)) goto failed;
    result = maage_storage_private_file(temporary);
    if (maage_status_failed(result)) goto failed;
    if (fclose(region->file)) {
        region->file = NULL;
        result = region_status(MAAGE_STATUS_IO_ERROR, MAAGE_OPERATION_CLOSE);
        goto failed;
    }
    region->file = NULL;
    result = maage_storage_replace(temporary, path);
    if (maage_status_failed(result)) goto failed;
    return region_status(MAAGE_STATUS_SUCCESS, MAAGE_OPERATION_SAVE);
failed:
    free(record);
    if (output) fclose(output);
    (void) maage_storage_remove(temporary);
    /* A failed replacement never discards the previously committed region.
     * Once the old descriptor is closed, no further I/O uses this instance. */
    return result;
}
maage_status maage_region_store_destroy(maage_region_store *store) {
    maage_region *region, *next;
    maage_status result = region_status(MAAGE_STATUS_SUCCESS, MAAGE_OPERATION_CLOSE);
    if (!store) return region_status(MAAGE_STATUS_NOOP, MAAGE_OPERATION_CLOSE);
    for (region = store->regions; region; region = next) {
        maage_status operation;
        next = region->next;
        operation = region_compact_on_close(store, region);
        if (maage_status_failed(operation)) {
            maage_log_status(MAAGE_LOG_WARNING, "maage.world.storage", operation, "Region compaction could not complete; the previously committed region remains authoritative.");
            if (!maage_status_failed(result)) result = operation;
        }
        if (region->file && fclose(region->file)) result = region_status(MAAGE_STATUS_IO_ERROR, MAAGE_OPERATION_CLOSE);
        maage_mutex_destroy(region->mutex);
        free(region);
    }
    maage_mutex_destroy(store->mutex);
    free(store);
    return result;
}
maage_status maage_region_store_load(maage_region_store *store, int x, int z, unsigned char **output, size_t *length) {
    maage_region *region;
    maage_region_entry *entry;
    maage_status result;
    unsigned char *compressed = NULL, *raw = NULL;
    uLongf raw_size;
    unsigned slot;
    if (output) *output = NULL;
    if (length) *length = 0;
    if (!store || !output || !length) return region_status(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_OPERATION_LOAD);
    result = region_get(store, x, z, &region);
    if (maage_status_failed(result)) return result;
    maage_mutex_lock(region->mutex);
    result = region_open(store, region, 0);
    if (maage_status_code(result) != MAAGE_STATUS_SUCCESS) goto done;
    slot = region_slot(x, z, region->x, region->z);
    entry = &region->entries[slot];
    if (!entry->offset) {
        result = region_status(MAAGE_STATUS_NOT_FOUND, MAAGE_OPERATION_LOAD);
        goto done;
    }
    compressed = (unsigned char *) malloc((size_t) entry->compressed_size);
    raw = (unsigned char *) malloc((size_t) entry->raw_size);
    if (!compressed || !raw) {
        result = region_status(MAAGE_STATUS_ALLOCATION_FAILED, MAAGE_OPERATION_LOAD);
        goto done;
    }
    if (fseek(region->file, entry->offset, SEEK_SET) ||
        fread(compressed, 1, (size_t) entry->compressed_size, region->file) != (size_t) entry->compressed_size) {
        result = region_status(MAAGE_STATUS_IO_ERROR, MAAGE_OPERATION_READ);
        goto done;
    }
    raw_size = (uLongf) entry->raw_size;
    if (region_crc(compressed, (size_t) entry->compressed_size) != entry->compressed_crc ||
        uncompress(raw, &raw_size, compressed, (uLong) entry->compressed_size) != Z_OK ||
        raw_size != (uLongf) entry->raw_size || region_crc(raw, (size_t) raw_size) != entry->raw_crc) {
        result = region_status(MAAGE_STATUS_MALFORMED_INPUT, MAAGE_OPERATION_DECODE);
        goto done;
    }
    *output = raw;
    *length = (size_t) raw_size;
    raw = NULL;
    result = region_status(MAAGE_STATUS_SUCCESS, MAAGE_OPERATION_LOAD);
done:
    free(compressed);
    free(raw);
    maage_mutex_unlock(region->mutex);
    return result;
}
maage_status maage_region_store_save(maage_region_store *store, int x, int z, const unsigned char *data, size_t length) {
    maage_region *region;
    maage_region_entry *entry;
    maage_status result;
    unsigned char *compressed = NULL, header[MAAGE_REGION_RECORD];
    unsigned long raw_crc, zip_crc;
    uLongf compressed_size, capacity;
    long position;
    unsigned slot;
    if (!store || !data || !length || length > MAAGE_REGION_CHUNK_MAX) return region_status(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_OPERATION_SAVE);
    capacity = compressBound((uLong) length);
    /* Compression can add framing overhead to an incompressible maximal
     * raw chunk. Bound stored size by the format's uint32 length field,
     * not the uncompressed payload ceiling. */
    if (capacity > 0xffffffffUL) return region_status(MAAGE_STATUS_BUFFER_OVERFLOW, MAAGE_OPERATION_SAVE);
    compressed = (unsigned char *) malloc((size_t) capacity);
    if (!compressed) return region_status(MAAGE_STATUS_ALLOCATION_FAILED, MAAGE_OPERATION_SAVE);
    compressed_size = capacity;
    if (compress2(compressed, &compressed_size, data, (uLong) length, Z_BEST_SPEED) != Z_OK) {
        free(compressed);
        return region_status(MAAGE_STATUS_IO_ERROR, MAAGE_OPERATION_COMPRESS);
    }
    raw_crc = region_crc(data, length);
    zip_crc = region_crc(compressed, (size_t) compressed_size);
    result = region_get(store, x, z, &region);
    if (maage_status_failed(result)) {
        free(compressed);
        return result;
    }
    maage_mutex_lock(region->mutex);
    result = region_open(store, region, 1);
    if (maage_status_code(result) != MAAGE_STATUS_SUCCESS) goto done;
    position = region->valid_end;
    if ((unsigned long) position > (unsigned long) LONG_MAX - MAAGE_REGION_RECORD - MAAGE_REGION_FOOTER - (unsigned long) compressed_size) {
        result = region_status(MAAGE_STATUS_BUFFER_OVERFLOW, MAAGE_OPERATION_SAVE);
        goto done;
    }
    /* Only a torn final append needs a filesystem truncate. Normal appends
     * seek to the indexed end without an unnecessary metadata/sync operation. */
    if (region->needs_trim) {
        result = maage_storage_truncate(region->file, position);
        if (maage_status_failed(result)) {
            region->broken = 1;
            goto done;
        }
        region->needs_trim = 0;
    } else if (fseek(region->file, position, SEEK_SET)) {
        result = region_status(MAAGE_STATUS_IO_ERROR, MAAGE_OPERATION_WRITE);
        goto done;
    }
    memset(header, 0, sizeof(header));
    memcpy(header, MAAGE_REGION_RECORD_MAGIC, 4);
    slot = region_slot(x, z, region->x, region->z);
    region_put(header + 4, slot, 2);
    region_put(header + 8, (unsigned long) length, 4);
    region_put(header + 12, (unsigned long) compressed_size, 4);
    region_put(header + 16, raw_crc, 4);
    region_put(header + 20, zip_crc, 4);
    if (fwrite(header, 1, sizeof(header), region->file) != sizeof(header) ||
        fwrite(compressed, 1, (size_t) compressed_size, region->file) != (size_t) compressed_size ||
        fwrite(MAAGE_REGION_END, 1, MAAGE_REGION_FOOTER, region->file) != MAAGE_REGION_FOOTER ||
        maage_status_failed(maage_storage_sync(region->file))) {
        result = region_status(MAAGE_STATUS_IO_ERROR, MAAGE_OPERATION_WRITE);
        clearerr(region->file);
        if (maage_status_failed(maage_storage_truncate(region->file, position))) region->broken = 1;
        goto done;
    }
    entry = &region->entries[slot];
    entry->offset = position + MAAGE_REGION_RECORD;
    entry->raw_size = (unsigned long) length;
    entry->compressed_size = (unsigned long) compressed_size;
    entry->raw_crc = raw_crc;
    entry->compressed_crc = zip_crc;
    region->valid_end = position + MAAGE_REGION_RECORD + (long) compressed_size + MAAGE_REGION_FOOTER;
    result = region_status(MAAGE_STATUS_SUCCESS, MAAGE_OPERATION_SAVE);
done:
    free(compressed);
    maage_mutex_unlock(region->mutex);
    return result;
}
