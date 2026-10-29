#include "world_internal.h"
#include "maage/world_store.h"
#include "maage/storage.h"
#include "maage/folder_link.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <time.h>
#include <limits.h>
#include <errno.h>

/* Chunk palettes contain at most 65536 namespaced state identities.
 * Reads allocate only the actual validated file size, not this maximum. */
#define MAAGE_STORE_CHUNK_MAX 18000000UL
/* Explicit file/address-space bound, not an entity-content limit. */
#define MAAGE_STORE_METADATA_MAX 4294967295UL
#define MAAGE_STORE_WORD 4294967295UL
struct maage_world_store {
    char directory[MAAGE_PATH_CAPACITY], chunks[MAAGE_PATH_CAPACITY];
    maage_storage_lock *lock;
    maage_world_record record;
    const maage_item_registry *items; /* borrowed after finalize */
};
typedef struct maage_store_bytes {
    unsigned char *data;
    size_t length, position;
    int failed;
    maage_status failure;
} maage_store_bytes;

static int maage_store_error(char error[256], const char *message)
{
    if (error) strcpy(error, message);
    return 0;
}
static unsigned long maage_store_crc(const unsigned char *data, size_t length)
{
    unsigned long crc = MAAGE_STORE_WORD;
    size_t i;
    unsigned int bit;
    for (i = 0; i < length; ++i) {
        crc ^= data[i];
        for (bit = 0; bit < 8; ++bit)
            crc = (crc >> 1) ^ ((crc & 1) ? 3988292384UL : 0UL);
    }
    return (crc ^ MAAGE_STORE_WORD) & MAAGE_STORE_WORD;
}
static void maage_store_put(maage_store_bytes *bytes, unsigned long value, size_t count)
{
    size_t i;
    if (count > bytes->length - bytes->position) { bytes->failed = 1; return; }
    for (i = 0; i < count; ++i) {
        bytes->data[bytes->position++] = (unsigned char)(value & 255UL);
        value >>= 8;
    }
}
static unsigned long maage_store_get(maage_store_bytes *bytes, size_t count)
{
    unsigned long value = 0;
    size_t i;
    if (count > bytes->length - bytes->position) { bytes->failed = 1; return 0; }
    for (i = 0; i < count; ++i)
        value |= (unsigned long)bytes->data[bytes->position++] << (i * 8);
    return value;
}
static void maage_store_text_put(maage_store_bytes *bytes, const char *text, size_t length)
{
    size_t i;
    for (i = 0; i < length; ++i) maage_store_put(bytes, (unsigned char)text[i], 1);
}
static int maage_store_magic(maage_store_bytes *bytes, const char *magic)
{
    size_t i;
    for (i = 0; i < 8; ++i)
        if (maage_store_get(bytes, 1) != (unsigned char)magic[i]) return 0;
    return !bytes->failed;
}
/* Encode IEEE binary64 explicitly; never serialize host-endian double objects
 * or rely on native integer widths, padding, or floating-point byte layout. */
static void maage_store_double_put(maage_store_bytes *bytes, double value)
{
    unsigned long high = 0, low;
    double fraction, mantissa;
    int exponent;
    if (value < 0) { high = 2147483648UL; value = -value; }
    if (!value) { maage_store_put(bytes, 0, 4); maage_store_put(bytes, high, 4); return; }
    fraction = frexp(value, &exponent);
    if (exponent < -1021) mantissa = ldexp(value, 1074);
    else {
        high |= (unsigned long)(exponent + 1022) << 20;
        mantissa = (fraction * 2.0 - 1.0) * 4503599627370496.0;
    }
    high |= (unsigned long)floor(mantissa / 4294967296.0);
    low = (unsigned long)fmod(mantissa, 4294967296.0);
    maage_store_put(bytes, low, 4); maage_store_put(bytes, high, 4);
}
static double maage_store_double_get(maage_store_bytes *bytes)
{
    unsigned long low = maage_store_get(bytes, 4), high = maage_store_get(bytes, 4);
    unsigned int exponent = (unsigned int)((high >> 20) & 2047UL);
    double value = (double)(high & 1048575UL) * 4294967296.0 + low;
    if (exponent == 2047) { bytes->failed = 1; return 0; }
    if (!exponent) value = ldexp(value, -1074);
    else value = ldexp(1.0 + value / 4503599627370496.0, (int)exponent - 1023);
    return high & 2147483648UL ? -value : value;
}
static int maage_store_name(const char *name)
{
    const unsigned char *p = (const unsigned char *)name;
    unsigned long point, minimum;
    unsigned int remaining;
    size_t length = strlen(name);
    if (!length || length >= 128) return 0;
    while (*p) {
        if (*p < 32 || *p == 127) return 0;
        if (*p < 128) { ++p; continue; }
        if (*p >= 194 && *p <= 223) { point = *p & 31; remaining = 1; minimum = 128; }
        else if (*p >= 224 && *p <= 239) { point = *p & 15; remaining = 2; minimum = 2048; }
        else if (*p >= 240 && *p <= 244) { point = *p & 7; remaining = 3; minimum = 65536UL; }
        else return 0;
        ++p;
        while (remaining--) {
            if (*p < 128 || *p > 191) return 0;
            point = (point << 6) | (*p++ & 63);
        }
        if (point < minimum || (point >= 55296UL && point <= 57343UL) || point > 1114111UL) return 0;
    }
    return 1;
}
static int maage_store_player(const maage_world_record *record)
{
    unsigned int i;
    for (i = 0; i < 3; ++i)
        if (!(record->position[i] > -MAAGE_WORLD_LIMIT && record->position[i] < MAAGE_WORLD_LIMIT)) return 0;
    if (!(record->position[1] >= 0 && record->position[1] < MAAGE_WORLD_LIMIT)) return 0;
    return record->yaw >= -1000000.0 && record->yaw <= 1000000.0 &&
        record->pitch >= -1.571 && record->pitch <= 1.571;
}
static maage_status maage_store_fail(char error[256], const char *message,
    unsigned char code, unsigned char operation, maage_status_trace *trace)
{
    maage_store_error(error, message);
    return MAAGE_STATUS_TRACE(trace, maage_status_make(code, MAAGE_CONTEXT_WORLD_IO,
        operation, 0), MAAGE_CONTEXT_WORLD_IO, operation);
}
static maage_status maage_store_native_error(int native, unsigned char operation,
    maage_status_trace *trace)
{
    maage_status result = native ? maage_status_from_errno(native, MAAGE_CONTEXT_STORAGE, operation) :
        maage_status_make(MAAGE_STATUS_IO_ERROR, MAAGE_CONTEXT_STORAGE, operation, 0);
    return maage_status_trace_record(trace, result, MAAGE_CONTEXT_STORAGE, operation,
        __FILE__, __LINE__, (long)native);
}
static maage_status maage_store_read(const char *path, size_t maximum,
    unsigned char **output, size_t *length, char error[256], maage_status_trace *trace)
{
    unsigned char *data;
    FILE *file;
    long file_length;
    int native, failed;
    maage_status result;
    if (!output || !length || !path)
        return maage_store_fail(error, "Missing world file read arguments.", MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_OPERATION_READ, trace);
    *output = NULL; *length = 0;
    if (maage_storage_status(path) != 1)
        return maage_store_fail(error, "World file is missing, unsafe, or inaccessible.", MAAGE_STATUS_IO_ERROR, MAAGE_OPERATION_READ, trace);
    errno = 0;
    file = maage_storage_open(path, "rb");
    if (!file) {
        native = errno;
        result = native ? maage_status_from_errno(native, MAAGE_CONTEXT_STORAGE, MAAGE_OPERATION_OPEN) :
            maage_status_make(MAAGE_STATUS_IO_ERROR, MAAGE_CONTEXT_STORAGE, MAAGE_OPERATION_OPEN, 0);
        maage_store_error(error, "Could not open world file.");
        maage_status_trace_record(trace, result, MAAGE_CONTEXT_STORAGE, MAAGE_OPERATION_OPEN,
            __FILE__, (unsigned long)__LINE__, (long)native);
        return MAAGE_STATUS_PROPAGATE(trace, result, MAAGE_CONTEXT_WORLD_IO, MAAGE_OPERATION_READ);
    }
    if (fseek(file, 0, SEEK_END) || (file_length = ftell(file)) < 0 || fseek(file, 0, SEEK_SET)) {
        native = errno; fclose(file);
        maage_store_error(error, "World file could not be measured or read.");
        result = maage_store_native_error(native, MAAGE_OPERATION_READ, trace);
        return MAAGE_STATUS_PROPAGATE(trace, result, MAAGE_CONTEXT_WORLD_IO, MAAGE_OPERATION_READ);
    }
    if ((unsigned long)file_length > maximum || (long)(size_t)file_length != file_length) {
        fclose(file);
        return maage_store_fail(error, "World file exceeds its format bound.", MAAGE_STATUS_MALFORMED_INPUT, MAAGE_OPERATION_READ, trace);
    }
    *length = (size_t)file_length;
    data = (unsigned char *)malloc(*length ? *length : 1);
    if (!data) {
        fclose(file);
        return maage_store_fail(error, "Not enough memory to read world file.", MAAGE_STATUS_ALLOCATION_FAILED, MAAGE_OPERATION_READ, trace);
    }
    errno = 0;
    failed = fread(data, 1, *length, file) != *length || ferror(file); native = errno;
    if (fclose(file) && !failed) { failed = 1; native = errno; }
    if (failed) {
        free(data); *length = 0;
        maage_store_error(error, "World file could not be read.");
        result = maage_store_native_error(native, MAAGE_OPERATION_READ, trace);
        return MAAGE_STATUS_PROPAGATE(trace, result, MAAGE_CONTEXT_WORLD_IO, MAAGE_OPERATION_READ);
    }
    *output = data;
    return maage_status_make(MAAGE_STATUS_SUCCESS, 0, MAAGE_OPERATION_READ, 0);
}
static maage_status maage_store_write(const char *path, const unsigned char *data,
    size_t length, char error[256], maage_status_trace *trace)
{
    char temporary[MAAGE_PATH_CAPACITY];
    FILE *file;
    maage_status result;
    int native;
    if (strlen(path) + 5 > sizeof(temporary))
        return maage_store_fail(error, "World file path is too long.", MAAGE_STATUS_BUFFER_OVERFLOW, MAAGE_OPERATION_WRITE, trace);
    strcpy(temporary, path); strcat(temporary, ".tmp");
    result = maage_storage_remove(temporary);
    if (maage_status_failed(result)) {
        maage_store_error(error, "Could not remove an abandoned temporary world file.");
        return MAAGE_STATUS_PROPAGATE(trace, result, MAAGE_CONTEXT_WORLD_IO, MAAGE_OPERATION_WRITE);
    }
    errno = 0; file = maage_storage_open(temporary, "wb");
    if (!file) {
        native = errno;
        result = native ? maage_status_from_errno(native, MAAGE_CONTEXT_STORAGE, MAAGE_OPERATION_OPEN) :
            maage_status_make(MAAGE_STATUS_IO_ERROR, MAAGE_CONTEXT_STORAGE, MAAGE_OPERATION_OPEN, 0);
        maage_store_error(error, "Could not create a temporary world file.");
        maage_status_trace_record(trace, result, MAAGE_CONTEXT_STORAGE, MAAGE_OPERATION_OPEN,
            __FILE__, (unsigned long)__LINE__, (long)native);
        return MAAGE_STATUS_PROPAGATE(trace, result, MAAGE_CONTEXT_WORLD_IO, MAAGE_OPERATION_WRITE);
    }
    errno = 0;
    result = fwrite(data, 1, length, file) == length ? maage_storage_sync(file) :
        maage_store_native_error(errno, MAAGE_OPERATION_WRITE, trace);
    if (fclose(file) && !maage_status_failed(result))
        result = maage_store_native_error(errno, MAAGE_OPERATION_CLOSE, trace);
    if (!maage_status_failed(result)) result = maage_storage_private_file(temporary);
    if (!maage_status_failed(result)) result = maage_storage_replace(temporary, path);
    if (maage_status_failed(result)) {
        maage_storage_remove(temporary);
        maage_store_error(error, "World save failed; the previous file was preserved.");
        return MAAGE_STATUS_PROPAGATE(trace, result, MAAGE_CONTEXT_WORLD_IO, MAAGE_OPERATION_WRITE);
    }
    return maage_status_make(MAAGE_STATUS_SUCCESS, 0, MAAGE_OPERATION_WRITE, 0);
}
static int maage_store_checksum(maage_store_bytes *bytes)
{
    unsigned long expected;
    if (bytes->length < 4) return 0;
    bytes->position = bytes->length - 4;
    expected = maage_store_get(bytes, 4); bytes->position = 0;
    bytes->length -= 4;
    return expected == maage_store_crc(bytes->data, bytes->length);
}
static void maage_store_checksum_put(maage_store_bytes *bytes)
{
    unsigned long crc = maage_store_crc(bytes->data, bytes->position);
    maage_store_put(bytes, crc, 4);
}
static void maage_store_gameplay_default(maage_gameplay_save *save)
{
    memset(save, 0, sizeof(*save)); save->mode = MAAGE_GAME_MODE_CREATIVE;
    save->health = save->hunger = 20; save->saturation = 5;
}
static int maage_store_gameplay_valid(const maage_gameplay_save *save,
    const maage_item_registry *items)
{
    unsigned int i;
    if (save->mode > MAAGE_GAME_MODE_CREATIVE || save->health > 20 || save->hunger > 20 ||
        save->inventory.selected >= MAAGE_HOTBAR_SLOTS ||
        !(save->saturation >= 0 && save->saturation <= save->hunger) ||
        !(save->exhaustion >= 0 && save->exhaustion <= 4) ||
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
/* Persist complete stable item names rather than transient numeric IDs.
 * Names are resolved only at import/save boundaries; no gameplay string work. */
static void maage_store_stack_put(maage_store_bytes *bytes,
    const maage_item_stack *stack, const maage_item_registry *items)
{
    const char *name = stack->count ? maage_item_registry_name(items, stack->item_id) : NULL;
    size_t length = name ? strlen(name) : 0;
    if (stack->count && (!name || !length || length > 255)) {
        bytes->failed = 1; return;
    }
    if ((unsigned long)stack->metadata > MAAGE_STORE_WORD ||
        (unsigned long)stack->damage > MAAGE_STORE_WORD) {
        bytes->failed = 1; return;
    }
    maage_store_put(bytes, (unsigned long)length, 1);
    if (length) maage_store_text_put(bytes, name, length);
    maage_store_put(bytes, stack->count, 1);
    maage_store_put(bytes, stack->metadata, 4);
    maage_store_put(bytes, stack->damage, 4);
}
static void maage_store_stack_get(maage_store_bytes *bytes,
    maage_item_stack *stack, const maage_item_registry *items)
{
    unsigned long length, value;
    char name[256];
    size_t i;
    memset(stack, 0, sizeof(*stack));
    length = maage_store_get(bytes, 1);
    if (bytes->failed || length > bytes->length - bytes->position) {
        bytes->failed = 1; return;
    }
    if (length) {
        for (i = 0; i < length; ++i) {
            name[i] = (char)maage_store_get(bytes, 1);
            if (!name[i]) bytes->failed = 1;
        }
        name[length] = 0;
        if (bytes->failed) return;
        bytes->failure = maage_item_registry_resolve_name(items, name, &stack->item_id);
        if (maage_status_failed(bytes->failure)) { bytes->failed = 1; return; }
    }
    stack->count = (unsigned int)maage_store_get(bytes, 1);
    value = maage_store_get(bytes, 4); stack->metadata = (unsigned int)value;
    if ((unsigned long)stack->metadata != value) bytes->failed = 1;
    value = maage_store_get(bytes, 4); stack->damage = (unsigned int)value;
    if ((unsigned long)stack->damage != value || !maage_item_stack_valid(items, stack))
        bytes->failed = 1;
}
static void maage_store_gameplay_put(maage_store_bytes *bytes,
    const maage_gameplay_save *save, const maage_item_registry *items)
{
    unsigned int i;

    maage_store_put(bytes, save->mode, 1); maage_store_put(bytes, save->health, 1);
    maage_store_put(bytes, save->hunger, 1); maage_store_put(bytes, save->flying ? 1 : 0, 1);
    maage_store_put(bytes, save->inventory.selected, 1);
    maage_store_double_put(bytes, save->saturation); maage_store_double_put(bytes, save->exhaustion);
    maage_store_double_put(bytes, save->fall_distance);
    for (i = 0; i < MAAGE_INVENTORY_SLOTS; ++i)
        maage_store_stack_put(bytes, &save->inventory.slots[i], items);
    maage_store_stack_put(bytes, &save->inventory.cursor, items);
    for (i = 0; i < MAAGE_CRAFTING_CELLS; ++i)
        maage_store_stack_put(bytes, &save->crafting_inputs[i], items);
}
static void maage_store_gameplay_get(maage_store_bytes *bytes, maage_gameplay_save *save,
    const maage_item_registry *items)
{
    unsigned int i;
    memset(save, 0, sizeof(*save));
    save->mode = (unsigned int)maage_store_get(bytes, 1); save->health = (unsigned int)maage_store_get(bytes, 1);
    save->hunger = (unsigned int)maage_store_get(bytes, 1); save->flying = (int)maage_store_get(bytes, 1);
    save->inventory.selected = (unsigned int)maage_store_get(bytes, 1);
    save->saturation = maage_store_double_get(bytes); save->exhaustion = maage_store_double_get(bytes);
    save->fall_distance = maage_store_double_get(bytes);
    for (i = 0; i < MAAGE_INVENTORY_SLOTS; ++i)
        maage_store_stack_get(bytes, &save->inventory.slots[i], items);
    maage_store_stack_get(bytes, &save->inventory.cursor, items);
    for (i = 0; i < MAAGE_CRAFTING_CELLS; ++i)
        maage_store_stack_get(bytes, &save->crafting_inputs[i], items);
    if (!maage_store_gameplay_valid(save, items)) bytes->failed = 1;
}
static void maage_store_entities_put(maage_store_bytes *bytes, const maage_entity_save *save,
    const maage_item_registry *items)
{
    size_t i;
    unsigned int axis;
    unsigned long previous = 0;
    const maage_entity *entity;
    if (!save->next_id || save->next_id > MAAGE_STORE_WORD || save->count > MAAGE_STORE_WORD ||
        (save->count && !save->entities)) { bytes->failed = 1; return; }
    maage_store_put(bytes, save->next_id, 4); maage_store_put(bytes, save->random, 4);
    maage_store_put(bytes, (unsigned long)save->count, 4);
    for (i = 0; i < save->count; ++i) {
        entity = &save->entities[i];
        if (!maage_entity_valid(entity, items) || entity->id <= previous || entity->id >= save->next_id) {
            bytes->failed = 1; return;
        }
        previous = entity->id;
        maage_store_put(bytes, entity->id, 4); maage_store_put(bytes, entity->type, 1);
        maage_store_put(bytes, entity->age, 4); maage_store_put(bytes, entity->pickup_delay, 2);
        maage_store_put(bytes, entity->grounded ? 1 : 0, 1);
        for (axis = 0; axis < 3; ++axis) maage_store_double_put(bytes, entity->position[axis]);
        for (axis = 0; axis < 3; ++axis) maage_store_double_put(bytes, entity->velocity[axis]);
        maage_store_stack_put(bytes, &entity->payload.item, items);
    }
}
static void maage_store_entities_get(maage_store_bytes *bytes, maage_entity_save *save,
    const maage_item_registry *items)
{
    unsigned long count, previous = 0;
    size_t i;
    unsigned int axis;
    maage_entity *entity;
    memset(save, 0, sizeof(*save));
    save->next_id = maage_store_get(bytes, 4); save->random = maage_store_get(bytes, 4);
    count = maage_store_get(bytes, 4);
    /* Validate counts against actual bytes before allocating. */
    if (bytes->failed || !save->next_id || count > (bytes->length-bytes->position)/70 ||
        count > (size_t)-1 / sizeof(*save->entities)) { bytes->failed = 1; return; }
    if (count) {
        save->entities = (maage_entity *)calloc((size_t)count, sizeof(*save->entities));
        if (!save->entities) {
            bytes->failed = 1; bytes->failure = maage_status_make(MAAGE_STATUS_ALLOCATION_FAILED,
                MAAGE_CONTEXT_WORLD_IO, MAAGE_OPERATION_DECODE, 0); return;
        }
    }
    save->count = (size_t)count;
    for (i = 0; i < save->count; ++i) {
        entity = &save->entities[i];
        entity->id = maage_store_get(bytes, 4); entity->type = (unsigned int)maage_store_get(bytes, 1);
        entity->age = maage_store_get(bytes, 4); entity->pickup_delay = (unsigned int)maage_store_get(bytes, 2);
        entity->grounded = (int)maage_store_get(bytes, 1);
        for (axis = 0; axis < 3; ++axis) entity->position[axis] = maage_store_double_get(bytes);
        for (axis = 0; axis < 3; ++axis) entity->velocity[axis] = maage_store_double_get(bytes);
        memcpy(entity->previous, entity->position, sizeof(entity->previous));
        maage_store_stack_get(bytes, &entity->payload.item, items);
        if (!maage_entity_valid(entity, items) || entity->id <= previous || entity->id >= save->next_id) bytes->failed = 1;
        previous = entity->id;
    }
}
static maage_status maage_store_metadata_write(const maage_world_store *store,
    const maage_world_record *record, char error[256], maage_status_trace *trace)
{
    unsigned char *data;
    maage_store_bytes bytes;
    size_t capacity;
    char path[MAAGE_PATH_CAPACITY];
    unsigned int i;
    if (!maage_generation_policy_valid(&record->generation_policy))
        return maage_store_fail(error, "World generation policy is invalid.", MAAGE_STATUS_MALFORMED_INPUT, MAAGE_OPERATION_SAVE, trace);
    if (!maage_store_gameplay_valid(&record->gameplay, store->items))
        return maage_store_fail(error, "Player gameplay identity or pickup data is invalid.", MAAGE_STATUS_MALFORMED_INPUT, MAAGE_OPERATION_SAVE, trace);
    if (!maage_random_tick_state_valid(&record->random_ticks))
        return maage_store_fail(error, "World random-tick checkpoint is invalid.", MAAGE_STATUS_MALFORMED_INPUT, MAAGE_OPERATION_SAVE, trace);
    if (!maage_environment_state_valid(&record->environment))
        return maage_store_fail(error, "World environment snapshot is invalid.", MAAGE_STATUS_MALFORMED_INPUT, MAAGE_OPERATION_SAVE, trace);
    /* Each entity uses at most 325 bytes including its 255-byte item identity.
     * The inventory/header reserve is independent of world entity count. */
    if (record->entities.count > ((size_t)-1 - 16384UL) / 325UL)
        return maage_store_fail(error, "Entity checkpoint exceeds addressable memory.", MAAGE_STATUS_BUFFER_OVERFLOW, MAAGE_OPERATION_SAVE, trace);
    capacity = 16384UL + record->entities.count * 325UL;
    if (capacity > MAAGE_STORE_METADATA_MAX)
        return maage_store_fail(error, "Entity checkpoint exceeds file encoding capacity.", MAAGE_STATUS_BUFFER_OVERFLOW, MAAGE_OPERATION_SAVE, trace);
    data = (unsigned char *)malloc(capacity);
    if (!data) return maage_store_fail(error, "Not enough memory to encode player metadata.", MAAGE_STATUS_ALLOCATION_FAILED, MAAGE_OPERATION_SAVE, trace);
    bytes.data = data; bytes.length = capacity; bytes.position = 0; bytes.failed = 0; bytes.failure = 0;
    maage_store_text_put(&bytes, "MGWORLD6", 8);
    maage_store_put(&bytes, record->seed.low, 4); maage_store_put(&bytes, record->seed.high, 4);
    maage_store_put(&bytes, strlen(record->id), 1); maage_store_put(&bytes, strlen(record->name), 1);
    maage_store_put(&bytes, strlen(record->created_utc), 1); maage_store_put(&bytes, record->new_world ? 1 : 0, 1);
    maage_store_text_put(&bytes, record->id, strlen(record->id));
    maage_store_text_put(&bytes, record->name, strlen(record->name));
    maage_store_text_put(&bytes, record->created_utc, strlen(record->created_utc));
    for (i = 0; i < 3; ++i) maage_store_double_put(&bytes, record->position[i]);
    maage_store_double_put(&bytes, record->yaw); maage_store_double_put(&bytes, record->pitch);
    maage_store_gameplay_put(&bytes, &record->gameplay, store->items);
    maage_store_entities_put(&bytes, &record->entities, store->items);
    maage_store_put(&bytes, (unsigned long)record->generation_policy.sea_level, 4);
    maage_store_put(&bytes, (unsigned long)record->generation_policy.spawn_search_radius, 4);
    maage_store_put(&bytes, (unsigned long)record->generation_policy.spawn_search_step, 4);
    maage_store_put(&bytes,record->environment.ticks_low,4);
    maage_store_put(&bytes,record->environment.ticks_high,4);
    maage_store_put(&bytes,record->environment.day_tick,4);
    maage_store_put(&bytes,record->environment.day_low,4);
    maage_store_put(&bytes,record->environment.day_high,4);
    maage_store_put(&bytes,record->environment.random,4);
    maage_store_put(&bytes,record->environment.rain_ticks,4);
    maage_store_put(&bytes,record->environment.thunder_ticks,4);
    maage_store_put(&bytes,record->environment.raining,1);
    maage_store_put(&bytes,record->environment.thundering,1);
    maage_store_double_put(&bytes,record->environment.rain_intensity);
    maage_store_double_put(&bytes,record->environment.thunder_intensity);
    maage_store_put(&bytes,record->random_ticks.selection,4);
    maage_store_put(&bytes,record->random_ticks.random,4);
    maage_store_checksum_put(&bytes);
    if (bytes.failed) {
        free(data);
        if (maage_status_failed(bytes.failure)) return MAAGE_STATUS_PROPAGATE(trace, bytes.failure, MAAGE_CONTEXT_WORLD_IO, MAAGE_OPERATION_SAVE);
        return maage_store_fail(error, "Could not encode world metadata.", MAAGE_STATUS_MALFORMED_INPUT, MAAGE_OPERATION_SAVE, trace);
    }
    if (maage_status_failed(maage_storage_join(path, sizeof(path), store->directory, "world.bin"))) {
        free(data); return maage_store_fail(error, "World metadata path is too long.", MAAGE_STATUS_BUFFER_OVERFLOW, MAAGE_OPERATION_SAVE, trace);
    }
    {
        maage_status result = maage_store_write(path, data, bytes.position, error, trace);
        free(data);
        return MAAGE_STATUS_PROPAGATE(trace, result, MAAGE_CONTEXT_WORLD_IO, MAAGE_OPERATION_SAVE);
    }
}
static maage_status maage_store_metadata_load(const maage_world_store *store,
    const char *expected_id, maage_world_record *record, char error[256], maage_status_trace *trace)
{
    char path[MAAGE_PATH_CAPACITY];
    maage_store_bytes bytes;
    unsigned long lengths[3], value;
    size_t i, j;
    char *texts[3];
    int ok, gameplay_read = 0;
    maage_status result;
    if (maage_status_failed(maage_storage_join(path, sizeof(path), store->directory, "world.bin")))
        return maage_store_fail(error, "World metadata path is too long.", MAAGE_STATUS_BUFFER_OVERFLOW, MAAGE_OPERATION_LOAD, trace);
    result = maage_store_read(path, MAAGE_STORE_METADATA_MAX, &bytes.data, &bytes.length, error, trace);
    if (maage_status_failed(result)) return MAAGE_STATUS_PROPAGATE(trace, result, MAAGE_CONTEXT_WORLD_IO, MAAGE_OPERATION_LOAD);
    bytes.position = 0; bytes.failed = 0; bytes.failure = 0;
    if (bytes.length >= 8 && memcmp(bytes.data, "MGWORLD6", 8)) {
        free(bytes.data);
        maage_store_error(error, "World was created using an incompatible development metadata format; recreate this development world.");
        return MAAGE_STATUS_TRACE(trace, maage_status_make(MAAGE_STATUS_MALFORMED_INPUT,
            MAAGE_CONTEXT_WORLD_IO, MAAGE_OPERATION_LOAD, MAAGE_DETAIL_WORLD_FORMAT), MAAGE_CONTEXT_WORLD_IO, MAAGE_OPERATION_LOAD);
    }
    ok = maage_store_checksum(&bytes) && maage_store_magic(&bytes, "MGWORLD6");
    if (ok) {
        record->seed.low = maage_store_get(&bytes, 4); record->seed.high = maage_store_get(&bytes, 4);
        for (i = 0; i < 3; ++i) lengths[i] = maage_store_get(&bytes, 1);
        record->new_world = (int)maage_store_get(&bytes, 1);
        ok = record->new_world <= 1 && lengths[0] == 36 &&
            lengths[1] > 0 && lengths[1] < sizeof(record->name) && lengths[2] == 20;
    }
    if (ok) {
        texts[0] = record->id; texts[1] = record->name; texts[2] = record->created_utc;
        for (i = 0; i < 3; ++i) {
            for (j = 0; j < lengths[i]; ++j) texts[i][j] = (char)maage_store_get(&bytes, 1);
            texts[i][lengths[i]] = 0;
            if (strlen(texts[i]) != lengths[i]) ok = 0;
        }
        for (i = 0; i < 3; ++i) record->position[i] = maage_store_double_get(&bytes);
        record->yaw = maage_store_double_get(&bytes); record->pitch = maage_store_double_get(&bytes);
        maage_store_gameplay_get(&bytes, &record->gameplay, store->items);
        maage_store_entities_get(&bytes, &record->entities, store->items);
        gameplay_read = 1;
        value = maage_store_get(&bytes, 4);
        if (value > INT_MAX) bytes.failed = 1;
        record->generation_policy.sea_level = (int)value;
        value = maage_store_get(&bytes, 4);
        if (value > INT_MAX) bytes.failed = 1;
        record->generation_policy.spawn_search_radius = (int)value;
        value = maage_store_get(&bytes, 4);
        if (value > INT_MAX) bytes.failed = 1;
        record->generation_policy.spawn_search_step = (int)value;
        record->environment.ticks_low=maage_store_get(&bytes,4);
        record->environment.ticks_high=maage_store_get(&bytes,4);
        record->environment.day_tick=maage_store_get(&bytes,4);
        record->environment.day_low=maage_store_get(&bytes,4);
        record->environment.day_high=maage_store_get(&bytes,4);
        record->environment.random=maage_store_get(&bytes,4);
        record->environment.rain_ticks=maage_store_get(&bytes,4);
        record->environment.thunder_ticks=maage_store_get(&bytes,4);
        record->environment.raining=(unsigned int)maage_store_get(&bytes,1);
        record->environment.thundering=(unsigned int)maage_store_get(&bytes,1);
        record->environment.rain_intensity=maage_store_double_get(&bytes);
        record->environment.thunder_intensity=maage_store_double_get(&bytes);
        record->random_ticks.selection=maage_store_get(&bytes,4);
        record->random_ticks.random=maage_store_get(&bytes,4);
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
    if (!ok && gameplay_read) { maage_gameplay_save_clear(&record->gameplay); maage_entity_save_clear(&record->entities); }
    if (ok) return maage_status_make(MAAGE_STATUS_SUCCESS, 0, MAAGE_OPERATION_LOAD, 0);
    if (maage_status_failed(bytes.failure)) {
        maage_store_error(error, "World metadata could not be decoded; original data preserved.");
        return MAAGE_STATUS_PROPAGATE(trace, bytes.failure, MAAGE_CONTEXT_WORLD_IO, MAAGE_OPERATION_LOAD);
    }
    return maage_store_fail(error, "World metadata is corrupt or uses an unsupported format.",
        MAAGE_STATUS_MALFORMED_INPUT, MAAGE_OPERATION_LOAD, trace);
}
maage_status maage_world_store_open(const char *instance_root, const char *id,
    int create, const char *name, const maage_world_seed *seed,
    const maage_generation_policy *generation_policy, const maage_item_registry *items,
    maage_world_store **output, maage_world_record *record, char error[256], maage_status_trace *trace)
{
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
    if (!instance_root || !output || !record || !items)
        return maage_store_fail(error, "Missing world storage initialization arguments.", MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_OPERATION_LOAD, trace);
    if ((!create && id && seed) || (name && !create && !seed))
        return maage_store_fail(error, "A seed or world name requires world creation; saved worlds cannot be overridden.", MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_OPERATION_LOAD, trace);
    if (generation_policy && !maage_generation_policy_valid(generation_policy))
        return maage_store_fail(error, "Invalid generation policy.", MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_OPERATION_LOAD, trace);
    if (!maage_item_registry_is_finalized(items))
        return maage_store_fail(error, "World storage requires finalized item definitions.", MAAGE_STATUS_INVALID_STATE, MAAGE_OPERATION_LOAD, trace);
    if ((id && !maage_storage_uuid_valid(id)) ||
        (name && !maage_store_name(name)))
        return maage_store_fail(error, "World IDs must be canonical UUIDs; world names are separate display metadata.", MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_OPERATION_LOAD, trace);
    if (!maage_folder_link_managed_path(instance_root, 0))
        return maage_store_fail(error, "World storage requires a physical instance root.", MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_OPERATION_LOAD, trace);
#define MAAGE_STORE_STEP(expression, message) do { \
    result = (expression); \
    if (maage_status_failed(result)) { maage_store_error(error, (message)); goto done; } \
} while (0)
    MAAGE_STORE_STEP(maage_storage_join(path, sizeof(path), instance_root, "instance.json"), "Instance manifest path is too long.");
    if (maage_storage_status(path) != 1) {
        result = maage_store_fail(error, "World storage requires a physical instance root, never the global data directory.", MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_OPERATION_LOAD, trace); goto done;
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
                free(selected); result = maage_store_fail(error, "The last-world selection is corrupt.", MAAGE_STATUS_MALFORMED_INPUT, MAAGE_OPERATION_LOAD, trace); goto done;
            }
            memcpy(chosen, selected, length); chosen[length] = 0; free(selected);
            if (!maage_storage_uuid_valid(chosen)) {
                result = maage_store_fail(error, "The last-world selection is invalid.", MAAGE_STATUS_MALFORMED_INPUT, MAAGE_OPERATION_LOAD, trace); goto done;
            }
        }
    }
    store = (maage_world_store *)calloc(1, sizeof(*store));
    if (!store) { result = maage_store_fail(error, "Not enough memory to open world storage.", MAAGE_STATUS_ALLOCATION_FAILED, MAAGE_OPERATION_LOAD, trace); goto done; }
    store->items = items;
    MAAGE_STORE_STEP(maage_storage_join(store->directory, sizeof(store->directory), worlds, chosen), "World path is too long.");
    kind = maage_storage_status(store->directory);
    if (create ? kind != 0 : kind != 2) {
        result = maage_store_fail(error, create ? "The requested world already exists or cannot be safely created." : "The requested world does not exist or is inaccessible.",
            create ? MAAGE_STATUS_INVALID_STATE : MAAGE_STATUS_NOT_FOUND, MAAGE_OPERATION_LOAD, trace); goto done;
    }
    MAAGE_STORE_STEP(maage_storage_directories(store->directory), "Could not prepare world directory.");
    MAAGE_STORE_STEP(maage_storage_join(path, sizeof(path), store->directory, ".world.lock"), "World lock path is too long.");
    MAAGE_STORE_STEP(maage_storage_acquire(path, &store->lock, trace), "This world is already open in another process.");
    if (!create && id && generation_policy) {
        result = maage_store_fail(error, "Cannot override a saved world's generation policy.", MAAGE_STATUS_INVALID_STATE, MAAGE_OPERATION_LOAD, trace); goto done;
    }
    if (create) {
        if (generation_policy) store->record.generation_policy = *generation_policy;
        else MAAGE_STORE_STEP(maage_generation_policy_default(&store->record.generation_policy), "Cannot initialize world generation policy.");
        strcpy(store->record.id, chosen); strcpy(store->record.name, name ? name : "New World");
        store->record.new_world = 1; store->record.position[1] = 128;
        maage_store_gameplay_default(&store->record.gameplay);
        store->record.entities.next_id = 1; store->record.entities.random = store->record.seed.low ^ store->record.seed.high;
        if (seed) store->record.seed = *seed;
        else {
            MAAGE_STORE_STEP(maage_storage_random(random_seed, sizeof(random_seed)), "Could not obtain secure randomness for world creation.");
            for (i = 0; i < 4; ++i) {
                store->record.seed.low |= (unsigned long)random_seed[i] << (i * 8);
                store->record.seed.high |= (unsigned long)random_seed[i + 4] << (i * 8);
            }
        }
        store->record.seed.high &= MAAGE_STORE_WORD; store->record.seed.low &= MAAGE_STORE_WORD;
        store->record.entities.random = store->record.seed.low ^ store->record.seed.high;
        now = time(NULL); utc = gmtime(&now);
        if (now == (time_t)-1 || !utc || strftime(store->record.created_utc, sizeof(store->record.created_utc), "%Y-%m-%dT%H:%M:%SZ", utc) != 20) {
            result = maage_store_fail(error, "Could not determine the world's creation time.", MAAGE_STATUS_IO_ERROR, MAAGE_OPERATION_LOAD, trace); goto done;
        }
        MAAGE_STORE_STEP(maage_environment_default(store->record.seed, &store->record.environment), "Could not initialize environment.");
        MAAGE_STORE_STEP(maage_random_tick_default(store->record.seed, &store->record.random_ticks), "Could not initialize random-tick streams.");
        MAAGE_STORE_STEP(maage_store_metadata_write(store, &store->record, error, trace), "Could not save new world metadata.");
    } else MAAGE_STORE_STEP(maage_store_metadata_load(store, chosen, &store->record, error, trace), "Could not load world metadata.");
    MAAGE_STORE_STEP(maage_storage_join(store->chunks, sizeof(store->chunks), store->directory, "chunks"), "Chunk path is too long.");
    MAAGE_STORE_STEP(maage_storage_directories(store->chunks), "Could not prepare chunk storage.");
    length = strlen(chosen); chosen[length++] = '\n';
    MAAGE_STORE_STEP(maage_store_write(last, (const unsigned char *)chosen, length, error, trace), "Could not persist world selection.");
    *record = store->record; *output = store; store = NULL;
    result = maage_status_make(MAAGE_STATUS_SUCCESS, 0, MAAGE_OPERATION_LOAD, 0);
done:
    maage_world_store_close(store); maage_storage_release(selection);
    return MAAGE_STATUS_PROPAGATE(trace, result, MAAGE_CONTEXT_WORLD_IO, MAAGE_OPERATION_LOAD);
#undef MAAGE_STORE_STEP
}

maage_status maage_world_store_save(maage_world_store *store,
    const double position[3], double yaw, double pitch, const maage_gameplay *game, const maage_world *world,
    const maage_environment_state *environment, char error[256], maage_status_trace *trace)
{
    maage_world_record record;
    maage_status result;
    if (!store || !position || !game || !world || !environment)
        return maage_store_fail(error, "Missing world snapshot.", MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_OPERATION_SAVE, trace);
    record = store->record;
    memcpy(record.position, position, sizeof(record.position)); record.yaw = yaw; record.pitch = pitch;
    record.environment = *environment; record.new_world = 0;
    result = maage_world_random_tick_snapshot(world, &record.random_ticks);
    if (maage_status_failed(result)) return MAAGE_STATUS_PROPAGATE(trace, result, MAAGE_CONTEXT_WORLD_IO, MAAGE_OPERATION_SAVE);
    if (!maage_store_player(&record) || !maage_environment_state_valid(&record.environment))
        return maage_store_fail(error, "World snapshot is invalid.", MAAGE_STATUS_MALFORMED_INPUT, MAAGE_OPERATION_SAVE, trace);
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
maage_status maage_world_store_close(maage_world_store *store)
{
    maage_status result;
    if (!store) return maage_status_make(MAAGE_STATUS_NOOP, 0, MAAGE_OPERATION_CLOSE, 0);
    maage_entity_save_clear(&store->record.entities);
    maage_gameplay_save_clear(&store->record.gameplay);
    result = maage_storage_release(store->lock); free(store);
    return maage_status_failed(result) ? result : maage_status_make(MAAGE_STATUS_SUCCESS, 0, MAAGE_OPERATION_CLOSE, 0);
}
static int maage_store_coordinates(int x, int z)
{
    int limit = MAAGE_WORLD_LIMIT / MAAGE_CHUNK_EDGE;
    return x >= -limit && x <= limit && z >= -limit && z <= limit;
}
static int maage_store_chunk_path(maage_world_store *store, int x, int z, char path[MAAGE_PATH_CAPACITY])
{
    char relative[80];
    if (!maage_store_coordinates(x, z)) return 0;
    sprintf(relative, "%d_%d.chunk", x, z);
    return !maage_status_failed(maage_storage_join(path, MAAGE_PATH_CAPACITY, store->chunks, relative));
}
static int maage_store_state_compare(const void *left, const void *right)
{
    maage_block_state_id a = *(const maage_block_state_id *)left;
    maage_block_state_id b = *(const maage_block_state_id *)right;
    return a < b ? -1 : (a > b ? 1 : 0);
}
static int maage_store_biome_compare(const void *left, const void *right)
{
    maage_biome_id a = *(const maage_biome_id *)left;
    maage_biome_id b = *(const maage_biome_id *)right;
    return a < b ? -1 : (a > b ? 1 : 0);
}
static size_t maage_store_state_index(const maage_block_state_id *states,
    size_t count, maage_block_state_id value)
{
    size_t left = 0, right = count, middle;
    while (left < right) {
        middle = left + (right - left) / 2;
        if (states[middle] < value) left = middle + 1;
        else right = middle;
    }
    return left < count && states[left] == value ? left : count;
}
static size_t maage_store_biome_index(const maage_biome_id *biomes,
    size_t count, maage_biome_id value)
{
    size_t left = 0, right = count, middle;
    while (left < right) {
        middle = left + (right - left) / 2;
        if (biomes[middle] < value) left = middle + 1;
        else right = middle;
    }
    return left < count && biomes[left] == value ? left : count;
}
static int maage_store_identity_get(maage_store_bytes *bytes, char name[256])
{
    unsigned long length = maage_store_get(bytes, 1);
    size_t i;
    if (!length || bytes->failed || length > bytes->length - bytes->position) {
        bytes->failed = 1; return 0;
    }
    for (i = 0; i < length; ++i) {
        name[i] = (char)maage_store_get(bytes, 1);
        if (!name[i]) bytes->failed = 1;
    }
    name[length] = 0;
    return !bytes->failed;
}
static void maage_store_identity_put(maage_store_bytes *bytes, const char *name)
{
    size_t length;
    if (!name) { bytes->failed = 1; return; }
    length = strlen(name);
    if (!length || length > 255) { bytes->failed = 1; return; }
    maage_store_put(bytes, (unsigned long)length, 1);
    maage_store_text_put(bytes, name, length);
}
static int maage_store_chunk_simulation_get(maage_store_bytes *bytes, maage_chunk *chunk,
    const maage_block_registry *blocks)
{
    maage_scheduled_update update;
    maage_block_entity *entity;
    unsigned char occupied[8192];
    unsigned long count, i, length, stages;
    const maage_block_state_definition *state;
    size_t before;
    memset(occupied,0,sizeof(occupied));
    stages=maage_store_get(bytes,4);
    if (stages>255UL) return 0;
    chunk->generation_stages=(unsigned int)stages;
    for (i=0;i<8;++i) chunk->generation_features[i]=maage_store_get(bytes,4);
    chunk->environment_initialized=(int)maage_store_get(bytes,1);
    count=maage_store_get(bytes,4);
    if (bytes->failed || chunk->environment_initialized>1 || count>131072UL || count>(bytes->length-bytes->position)/14UL) return 0;
    for (i=0;i<count;++i) {
        update.kind=(unsigned int)maage_store_get(bytes,4);
        if(update.kind!=MAAGE_BLOCK_UPDATE_FLUID&&update.kind!=MAAGE_BLOCK_UPDATE_SUPPORT)return 0;
        update.cell=(unsigned int)maage_store_get(bytes,2);
        update.due_low=maage_store_get(bytes,4); update.due_high=maage_store_get(bytes,4);
        before=chunk->update_count;
        if (bytes->failed) return 0;
        bytes->failure = maage_chunk_schedule_update(chunk,&update);
        if (maage_status_failed(bytes->failure) || chunk->update_count!=before+1) return 0;
    }
    count=maage_store_get(bytes,4);
    if (bytes->failed || count>65536UL || count>(bytes->length-bytes->position)/14UL ||
        count>(size_t)-1/sizeof(*chunk->entities)) return 0;
    if (count) {
        chunk->entities=(maage_block_entity *)calloc((size_t)count,sizeof(*chunk->entities));
        if (!chunk->entities) { bytes->failure = maage_status_make(MAAGE_STATUS_ALLOCATION_FAILED, MAAGE_CONTEXT_WORLD_IO, MAAGE_OPERATION_DECODE, 0); return 0; }
        chunk->entity_capacity=(size_t)count;
    }
    for (i=0;i<count;++i) {
        entity=&chunk->entities[chunk->entity_count++];
        entity->cell=(unsigned int)maage_store_get(bytes,2);
        entity->type=maage_store_get(bytes,4); entity->version=maage_store_get(bytes,4);
        length=maage_store_get(bytes,4);
        if (bytes->failed || !entity->type || !entity->version || length>65536UL ||
            length>bytes->length-bytes->position || (occupied[entity->cell/8]&(1U<<(entity->cell%8)))) return 0;
        state=maage_block_registry_state(blocks,maage_chunk_get(chunk,(int)(entity->cell%16),
            (int)(entity->cell/256),(int)((entity->cell/16)%16)));
        if (!state || state->block_id==MAAGE_BLOCK_AIR) return 0;
        occupied[entity->cell/8]|=(unsigned char)(1U<<(entity->cell%8));
        entity->length=(size_t)length;
        if (length) {
            entity->data=(unsigned char *)malloc((size_t)length);
            if (!entity->data) { bytes->failure = maage_status_make(MAAGE_STATUS_ALLOCATION_FAILED, MAAGE_CONTEXT_WORLD_IO, MAAGE_OPERATION_DECODE, 0); return 0; }
            memcpy(entity->data,bytes->data+bytes->position,(size_t)length); bytes->position+=(size_t)length;
        }
    }
    chunk->updates_relative=1;
    return !bytes->failed;
}
static int maage_store_chunk_simulation_size(const maage_chunk *chunk,
    const maage_block_registry *blocks, size_t *length)
{
    unsigned char occupied[16384];
    const maage_block_entity *entity;
    const maage_block_state_definition *state;
    size_t i, size=45UL;
    if (chunk->update_count>131072UL || chunk->entity_count>65536UL ||
        chunk->generation_stages>255U || (chunk->environment_initialized!=0 && chunk->environment_initialized!=1)) return 0;
    size+=chunk->update_count*14UL+chunk->entity_count*14UL;
    memset(occupied,0,sizeof(occupied));
    for (i=0;i<chunk->update_count;++i) {
        const maage_scheduled_update *update=&chunk->updates[i];
        if ((update->kind!=MAAGE_BLOCK_UPDATE_FLUID&&update->kind!=MAAGE_BLOCK_UPDATE_SUPPORT) || update->cell>65535U ||
            update->due_low>MAAGE_STORE_WORD || update->due_high>MAAGE_STORE_WORD ||
            (occupied[(update->kind-1U)*8192U+update->cell/8]&(1U<<(update->cell%8)))) return 0;
        occupied[(update->kind-1U)*8192U+update->cell/8]|=(unsigned char)(1U<<(update->cell%8));
    }
    memset(occupied,0,sizeof(occupied));
    for (i=0;i<chunk->entity_count;++i) {
        entity=&chunk->entities[i];
        if (entity->cell>65535U || !entity->type || entity->type>MAAGE_STORE_WORD || !entity->version ||
            entity->version>MAAGE_STORE_WORD || entity->length>65536UL || (entity->length && !entity->data) ||
            (occupied[entity->cell/8]&(1U<<(entity->cell%8))) || entity->length>MAAGE_STORE_CHUNK_MAX-size) return 0;
        state=maage_block_registry_state(blocks,maage_chunk_get(chunk,(int)(entity->cell%16),
            (int)(entity->cell/256),(int)((entity->cell/16)%16)));
        if (!state || state->block_id==MAAGE_BLOCK_AIR) return 0;
        occupied[entity->cell/8]|=(unsigned char)(1U<<(entity->cell%8)); size+=entity->length;
    }
    for (i=0;i<8;++i) if (chunk->generation_features[i]>MAAGE_STORE_WORD) return 0;
    *length=size; return 1;
}
static void maage_store_chunk_simulation_put(maage_store_bytes *bytes, const maage_chunk *chunk,
    unsigned long now_low, unsigned long now_high)
{
    size_t i,j;
    unsigned long low, high;
    const maage_scheduled_update *update;
    const maage_block_entity *entity;
    maage_store_put(bytes,chunk->generation_stages,4);
    for (i=0;i<8;++i) maage_store_put(bytes,chunk->generation_features[i],4);
    maage_store_put(bytes,chunk->environment_initialized ? 1UL : 0UL,1);
    maage_store_put(bytes,(unsigned long)chunk->update_count,4);
    for (i=0;i<chunk->update_count;++i) {
        update=&chunk->updates[i]; low=high=0;
        if (chunk->updates_relative) { low=update->due_low; high=update->due_high; }
        else if (update->due_high>now_high || (update->due_high==now_high && update->due_low>now_low)) {
            low=(update->due_low-now_low)&MAAGE_STORE_WORD;
            high=(update->due_high-now_high-(update->due_low<now_low ? 1UL : 0UL))&MAAGE_STORE_WORD;
        }
        maage_store_put(bytes,update->kind,4); maage_store_put(bytes,update->cell,2);
        maage_store_put(bytes,low,4); maage_store_put(bytes,high,4);
    }
    maage_store_put(bytes,(unsigned long)chunk->entity_count,4);
    for (i=0;i<chunk->entity_count;++i) {
        entity=&chunk->entities[i];
        maage_store_put(bytes,entity->cell,2); maage_store_put(bytes,entity->type,4);
        maage_store_put(bytes,entity->version,4); maage_store_put(bytes,(unsigned long)entity->length,4);
        for (j=0;j<entity->length;++j) maage_store_put(bytes,entity->data[j],1);
    }
}
static maage_status maage_store_chunk_decode(unsigned char *data, size_t length, int x, int z,
    const maage_block_registry *blocks, const maage_biome_registry *biomes,
    maage_chunk **output, maage_status_trace *trace)
{
    maage_store_bytes bytes;
    maage_chunk *chunk = NULL;
    maage_block_state_id *palette = NULL, state;
    maage_biome_id biome_palette[256], biome;
    maage_block_id block;
    unsigned long mask, stored_x, stored_z, expected_x, expected_z, value;
    unsigned long state_count, biome_count;
    size_t i, section, payload_length = 768UL + 45UL;
    size_t active_cells = 0;
    unsigned int metadata;
    char name[256];
    maage_status result = maage_status_make(MAAGE_STATUS_MALFORMED_INPUT, MAAGE_CONTEXT_WORLD_IO, MAAGE_OPERATION_DECODE, 0);
    *output = NULL;
    bytes.data = data; bytes.length = length; bytes.position = 0; bytes.failed = 0; bytes.failure = 0;
    if (length < 845UL || !maage_store_checksum(&bytes) ||
        !maage_store_magic(&bytes, "MGCHNK5\0"))
        return MAAGE_STATUS_PROPAGATE(trace, result, MAAGE_CONTEXT_WORLD_IO, MAAGE_OPERATION_DECODE);
    stored_x = maage_store_get(&bytes, 4); stored_z = maage_store_get(&bytes, 4);
    expected_x = (unsigned long)x & MAAGE_STORE_WORD;
    expected_z = (unsigned long)z & MAAGE_STORE_WORD;
    if (stored_x != expected_x || stored_z != expected_z)
        return MAAGE_STATUS_PROPAGATE(trace, result, MAAGE_CONTEXT_WORLD_IO, MAAGE_OPERATION_DECODE);
    mask = maage_store_get(&bytes, 2);
    if (maage_store_get(&bytes, 2) != 0 || bytes.failed)
        return MAAGE_STATUS_PROPAGATE(trace, result, MAAGE_CONTEXT_WORLD_IO, MAAGE_OPERATION_DECODE);
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
        bytes.length - bytes.position < payload_length + state_count * 6UL + biome_count * 2UL)
        return MAAGE_STATUS_PROPAGATE(trace, result, MAAGE_CONTEXT_WORLD_IO, MAAGE_OPERATION_DECODE);
    if (state_count) {
        if (state_count > (size_t)-1 / sizeof(*palette))
            return MAAGE_STATUS_PROPAGATE(trace, result, MAAGE_CONTEXT_WORLD_IO, MAAGE_OPERATION_DECODE);
        palette = (maage_block_state_id *)malloc((size_t)state_count * sizeof(*palette));
        if (!palette) {
            result = maage_status_make(MAAGE_STATUS_ALLOCATION_FAILED, MAAGE_CONTEXT_WORLD_IO, MAAGE_OPERATION_DECODE, 0);
            return MAAGE_STATUS_PROPAGATE(trace, result, MAAGE_CONTEXT_WORLD_IO, MAAGE_OPERATION_DECODE);
        }
    }
    for (i = 0; i < state_count; ++i) {
        if (!maage_store_identity_get(&bytes, name)) goto failed;
        value = maage_store_get(&bytes, 4);
        metadata = (unsigned int)value;
        if ((unsigned long)metadata != value) goto failed;
        result = maage_block_registry_resolve_name(blocks, name, &block);
        if (maage_status_failed(result)) goto failed;
        result = maage_block_registry_resolve_state(blocks, block, metadata, &state);
        if (maage_status_failed(result)) goto failed;
        result = maage_status_make(MAAGE_STATUS_MALFORMED_INPUT, MAAGE_CONTEXT_WORLD_IO, MAAGE_OPERATION_DECODE, 0);
        palette[i] = state;
    }
    for (i = 0; i < biome_count; ++i) {
        if (!maage_store_identity_get(&bytes, name)) goto failed;
        result = maage_biome_registry_resolve_name(biomes, name, &biome);
        if (maage_status_failed(result)) goto failed;
        result = maage_status_make(MAAGE_STATUS_MALFORMED_INPUT, MAAGE_CONTEXT_WORLD_IO, MAAGE_OPERATION_DECODE, 0);
        biome_palette[i] = biome;
    }
    if (bytes.failed || bytes.length - bytes.position < payload_length) goto failed;
    result = maage_chunk_create(x, z, &chunk);
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
        chunk->heightmap[i] = (unsigned short)value;
    }
    for (section = 0; section < 16; ++section) {
        if (!(mask & (1UL << section))) continue;
        chunk->sections[section] = (maage_section *)malloc(sizeof(maage_section));
        if (!chunk->sections[section]) {
            result = maage_status_make(MAAGE_STATUS_ALLOCATION_FAILED, MAAGE_CONTEXT_WORLD_IO, MAAGE_OPERATION_DECODE, 0);
            goto failed;
        }
        for (i = 0; i < MAAGE_SECTION_VOLUME; ++i) {
            value = maage_store_get(&bytes, 2);
            if (value >= state_count) goto failed;
            chunk->sections[section]->states[i] = palette[value];
        }
        for (i = 0; i < MAAGE_SECTION_VOLUME; ++i)
            chunk->sections[section]->light[i] = (unsigned char)maage_store_get(&bytes, 1);
    }
    if (!maage_store_chunk_simulation_get(&bytes, chunk, blocks) ||
        chunk->generation_stages != (1U << MAAGE_GENERATION_STAGE_COUNT) - 1U ||
        bytes.failed || bytes.position != bytes.length) goto failed;
    free(palette);
    *output = chunk;
    return maage_status_make(MAAGE_STATUS_SUCCESS, 0, MAAGE_OPERATION_DECODE, 0);
failed:
    if (maage_status_failed(bytes.failure)) result = bytes.failure;
    free(palette);
    maage_chunk_destroy(chunk);
    return MAAGE_STATUS_PROPAGATE(trace, result, MAAGE_CONTEXT_WORLD_IO, MAAGE_OPERATION_DECODE);
}
maage_status maage_world_store_load_chunk(maage_world_store *store, int x, int z,
    const maage_block_registry *blocks, const maage_biome_registry *biomes,
    maage_chunk **chunk, char error[256], maage_status_trace *trace)
{
    char path[MAAGE_PATH_CAPACITY];
    unsigned char *data;
    size_t length;
    maage_status result;
    if (chunk) *chunk = NULL;
    if (!chunk || !store || !blocks || !biomes)
        return maage_store_fail(error, "Missing chunk load arguments.", MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_OPERATION_LOAD, trace);
    if (!maage_store_chunk_path(store, x, z, path))
        return maage_store_fail(error, "Chunk coordinates or path exceed the world format bounds.", MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_OPERATION_LOAD, trace);
    if (maage_storage_status(path) == 0)
        return maage_status_make(MAAGE_STATUS_NOT_FOUND, MAAGE_CONTEXT_WORLD_IO, MAAGE_OPERATION_LOAD, 0);
    result = maage_store_read(path, MAAGE_STORE_CHUNK_MAX, &data, &length, error, trace);
    if (maage_status_failed(result)) return MAAGE_STATUS_PROPAGATE(trace, result, MAAGE_CONTEXT_WORLD_IO, MAAGE_OPERATION_LOAD);
    result = maage_store_chunk_decode(data, length, x, z, blocks, biomes, chunk, trace);
    free(data);
    if (maage_status_failed(result)) {
        maage_store_error(error, "Chunk data is corrupt, unmapped, incompatible, or could not be allocated; the save was preserved.");
        return MAAGE_STATUS_PROPAGATE(trace, result, MAAGE_CONTEXT_WORLD_IO, MAAGE_OPERATION_LOAD);
    }
    return maage_status_make(MAAGE_STATUS_SUCCESS, 0, MAAGE_OPERATION_LOAD, 0);
}

maage_status maage_world_store_save_chunk_at(maage_world_store *store, const maage_chunk *chunk,
    const maage_block_registry *blocks, const maage_biome_registry *biomes,
    unsigned long tick_low, unsigned long tick_high, char error[256], maage_status_trace *trace)
{
    char path[MAAGE_PATH_CAPACITY];
    maage_store_bytes bytes;
    maage_chunk *existing = NULL;
    maage_block_state_id *states = NULL;
    unsigned char *encoded = NULL;
    maage_biome_id biome_ids[256];
    const maage_block_state_definition *definition;
    const char *name;
    unsigned long mask = 0;
    size_t i, section, j, count = 0, unique = 0, biome_count = 0, cells = 0;
    size_t capacity = 800UL, simulation_length;
    maage_status result, load_result;
    if (!store || !chunk || !blocks || !biomes ||
        !maage_store_chunk_path(store, chunk->x, chunk->z, path))
    {
        maage_store_error(error, "Chunk coordinates or path exceed the world format bounds.");
        return MAAGE_STATUS_TRACE(trace, maage_status_make(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_CONTEXT_WORLD_IO, MAAGE_OPERATION_SAVE,
            !store ? 0 : (!chunk ? 1 : (!blocks ? 2 : (!biomes ? 3 : 1)))), MAAGE_CONTEXT_WORLD_IO, MAAGE_OPERATION_SAVE);
    }
    /* Persist only fully finalized feature-capable columns. A failed or
     * cancelled generation stage must never become a durable partial world. */
    if (chunk->generation_stages != (1U << MAAGE_GENERATION_STAGE_COUNT) - 1U) {
        maage_store_error(error, "Refusing to persist a column with incomplete generation stages.");
        return MAAGE_STATUS_TRACE(trace,
            maage_status_make(MAAGE_STATUS_INVARIANT_VIOLATION, MAAGE_CONTEXT_WORLD_IO,
                MAAGE_OPERATION_SAVE, 0), MAAGE_CONTEXT_WORLD_IO, MAAGE_OPERATION_SAVE);
    }
    if (maage_storage_status(path) != 0) {
        load_result = maage_world_store_load_chunk(store, chunk->x, chunk->z, blocks, biomes, &existing, error, trace);
        if (maage_status_failed(load_result)) return MAAGE_STATUS_PROPAGATE(trace, load_result, MAAGE_CONTEXT_WORLD_IO, MAAGE_OPERATION_SAVE);
        maage_chunk_destroy(existing);
    }
    if (tick_low>MAAGE_STORE_WORD || tick_high>MAAGE_STORE_WORD ||
        !maage_store_chunk_simulation_size(chunk,blocks,&simulation_length)) goto incompatible;
    capacity+=simulation_length;
    for (section = 0; section < 16; ++section)
        if (chunk->sections[section]) { mask |= 1UL << section; cells += MAAGE_SECTION_VOLUME; }
    if (cells) {
        states = (maage_block_state_id *)malloc(cells * sizeof(*states));
        if (!states) {
            maage_store_error(error, "Not enough memory for the chunk identity palette.");
            return MAAGE_STATUS_TRACE(trace, maage_status_make(MAAGE_STATUS_ALLOCATION_FAILED, MAAGE_CONTEXT_WORLD_IO, MAAGE_OPERATION_SAVE, 0), MAAGE_CONTEXT_WORLD_IO, MAAGE_OPERATION_SAVE);
        }
    }
    for (section = 0; section < 16; ++section) {
        if (!chunk->sections[section]) continue;
        for (i = 0; i < MAAGE_SECTION_VOLUME; ++i) {
            maage_block_state_id id = chunk->sections[section]->states[i];
            if (!maage_block_registry_state(blocks, id)) goto incompatible;
            states[count++] = id;
        }
    }
    if (count) {
        qsort(states, count, sizeof(*states), maage_store_state_compare);
        for (i = 0; i < count; ++i)
            if (!unique || states[i] != states[unique - 1])
                states[unique++] = states[i];
    }
    for (i = 0; i < 256; ++i) {
        if (!maage_biome_registry_get(biomes, chunk->biomes[i])) goto incompatible;
        biome_ids[i] = chunk->biomes[i];
    }
    qsort(biome_ids, 256, sizeof(biome_ids[0]), maage_store_biome_compare);
    for (i = 0; i < 256; ++i)
        if (!biome_count || biome_ids[i] != biome_ids[biome_count - 1])
            biome_ids[biome_count++] = biome_ids[i];
    /* Size this save from actual palette names and occupied sections. */
    capacity += cells * 3UL;
    for (i = 0; i < unique; ++i) {
        definition = maage_block_registry_state(blocks, states[i]);
        name = definition ? maage_block_registry_block_name(blocks, definition->block_id) : NULL;
        if (!name || !strlen(name) || strlen(name) > 255 ||
            (unsigned long)definition->metadata > MAAGE_STORE_WORD) goto incompatible;
        capacity += strlen(name) + 5UL;
    }
    for (i = 0; i < biome_count; ++i) {
        name = maage_biome_registry_name(biomes, biome_ids[i]);
        if (!name || !strlen(name) || strlen(name) > 255) goto incompatible;
        capacity += strlen(name) + 1UL;
    }
    if (capacity > MAAGE_STORE_CHUNK_MAX) goto incompatible;
    encoded = (unsigned char *)malloc(capacity);
    bytes.data = encoded;
    if (!bytes.data) {
        free(states); maage_store_error(error, "Not enough memory to encode a chunk.");
        return MAAGE_STATUS_TRACE(trace, maage_status_make(MAAGE_STATUS_ALLOCATION_FAILED, MAAGE_CONTEXT_WORLD_IO, MAAGE_OPERATION_SAVE, 0), MAAGE_CONTEXT_WORLD_IO, MAAGE_OPERATION_SAVE);
    }
    bytes.length = capacity; bytes.position = 0; bytes.failed = 0; bytes.failure = 0;
    maage_store_text_put(&bytes, "MGCHNK5\0", 8);
    maage_store_put(&bytes, (unsigned long)chunk->x, 4);
    maage_store_put(&bytes, (unsigned long)chunk->z, 4);
    maage_store_put(&bytes, mask, 2); maage_store_put(&bytes, 0, 2);
    maage_store_put(&bytes, (unsigned long)unique, 4);
    maage_store_put(&bytes, (unsigned long)biome_count, 2);
    maage_store_put(&bytes, 0, 2);
    for (i = 0; i < unique; ++i) {
        definition = maage_block_registry_state(blocks, states[i]);
        if (!definition) goto incompatible;
        name = maage_block_registry_block_name(blocks, definition->block_id);
        if (!name || (unsigned long)definition->metadata > MAAGE_STORE_WORD) goto incompatible;
        maage_store_identity_put(&bytes, name);
        maage_store_put(&bytes, (unsigned long)definition->metadata, 4);
    }
    for (i = 0; i < biome_count; ++i)
        maage_store_identity_put(&bytes, maage_biome_registry_name(biomes, biome_ids[i]));
    for (i = 0; i < 256; ++i) {
        j = maage_store_biome_index(biome_ids, biome_count, chunk->biomes[i]);
        if (j == biome_count) goto incompatible;
        maage_store_put(&bytes, (unsigned long)j, 1);
    }
    for (i = 0; i < 256; ++i) {
        if (chunk->heightmap[i] > MAAGE_WORLD_HEIGHT) goto incompatible;
        maage_store_put(&bytes, chunk->heightmap[i], 2);
    }
    for (section = 0; section < 16; ++section) {
        if (!chunk->sections[section]) continue;
        for (i = 0; i < MAAGE_SECTION_VOLUME; ++i) {
            j = maage_store_state_index(states, unique, chunk->sections[section]->states[i]);
            if (j == unique || j > 65535UL) goto incompatible;
            maage_store_put(&bytes, (unsigned long)j, 2);
        }
        for (i = 0; i < MAAGE_SECTION_VOLUME; ++i)
            maage_store_put(&bytes, chunk->sections[section]->light[i], 1);
    }
    maage_store_chunk_simulation_put(&bytes,chunk,tick_low,tick_high);
    maage_store_checksum_put(&bytes);
    result = !bytes.failed && bytes.position == capacity ?
        maage_store_write(path, bytes.data, bytes.position, error, trace) :
        maage_status_make(MAAGE_STATUS_INVARIANT_VIOLATION, MAAGE_CONTEXT_WORLD_IO, MAAGE_OPERATION_SAVE, 0);
    free(encoded); free(states);
    if (maage_status_failed(result)) maage_store_error(error, "Chunk save failed; the previous file was preserved.");
    return MAAGE_STATUS_PROPAGATE(trace, result, MAAGE_CONTEXT_WORLD_IO, MAAGE_OPERATION_SAVE);
incompatible:
    free(encoded); free(states);
    maage_store_error(error, "Chunk has invalid heights or unresolved registered content identities.");
    return MAAGE_STATUS_TRACE(trace, maage_status_make(MAAGE_STATUS_INVALID_STATE, MAAGE_CONTEXT_WORLD_IO, MAAGE_OPERATION_SAVE, 0), MAAGE_CONTEXT_WORLD_IO, MAAGE_OPERATION_SAVE);
}

/* Catalog reads use the same current-format decoder as authoritative startup. */
typedef struct maage_store_catalog {
    const char *root;
    const maage_item_registry *items;
    maage_world_summary *entries;
    size_t count, capacity;
    maage_status result;
} maage_store_catalog;
static int maage_store_catalog_entry(const char *name, maage_folder_link_kind kind, void *context)
{
    maage_store_catalog *catalog = (maage_store_catalog *)context;
    maage_world_store *store;
    maage_world_summary *grown, *entry;
    char error[256];
    size_t capacity;
    if (kind != MAAGE_FOLDER_LINK_DIRECTORY || !maage_storage_uuid_valid(name)) return 1;
    if (catalog->count == catalog->capacity) {
        capacity = catalog->capacity ? catalog->capacity * 2 : 16;
        if (capacity < catalog->capacity || capacity > (size_t)-1 / sizeof(*grown)) goto memory;
        grown = (maage_world_summary *)realloc(catalog->entries, capacity * sizeof(*grown));
        if (!grown) goto memory;
        catalog->entries = grown; catalog->capacity = capacity;
    }
    store = (maage_world_store *)calloc(1, sizeof(*store));
    if (!store) goto memory;
    store->items = catalog->items;
    entry = &catalog->entries[catalog->count++]; memset(entry, 0, sizeof(*entry));
    strcpy(entry->id, name); strcpy(entry->name, "Unreadable world");
    entry->status = maage_storage_join(store->directory, sizeof(store->directory), catalog->root, name);
    if (!maage_status_failed(entry->status))
        entry->status = maage_store_metadata_load(store, name, &store->record, error, NULL);
    if (!maage_status_failed(entry->status)) {
        strcpy(entry->name, store->record.name); strcpy(entry->created_utc, store->record.created_utc);
        entry->game_mode = store->record.gameplay.mode;
    }
    maage_entity_save_clear(&store->record.entities);
    maage_gameplay_save_clear(&store->record.gameplay); free(store);
    return 1;
memory:
    catalog->result = maage_status_make(MAAGE_STATUS_ALLOCATION_FAILED, MAAGE_CONTEXT_WORLD_IO, MAAGE_OPERATION_LOAD, 0);
    return 0;
}
static int maage_store_catalog_order(const void *left, const void *right)
{
    const maage_world_summary *a = (const maage_world_summary *)left;
    const maage_world_summary *b = (const maage_world_summary *)right;
    int order = strcmp(b->created_utc, a->created_utc);
    return order ? order : strcmp(a->id, b->id);
}
maage_status maage_world_store_list(const char *instance_root, const maage_item_registry *items,
    maage_world_summary **entries, size_t *count, char error[256], maage_status_trace *trace)
{
    maage_store_catalog catalog;
    char path[MAAGE_PATH_CAPACITY];
    maage_status result;
    if (!entries || !count || !items || !maage_item_registry_is_finalized(items) ||
        !maage_folder_link_managed_path(instance_root, 0))
        return maage_store_fail(error, "Invalid world catalog arguments.", MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_OPERATION_LOAD, trace);
    *entries = NULL; *count = 0;
    result = maage_storage_join(path, sizeof(path), instance_root, "instance.json");
    if (maage_status_failed(result)) return result;
    if (maage_storage_status(path) != 1) return maage_store_fail(error, "A physical instance is required.", MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_OPERATION_LOAD, trace);
    result = maage_storage_join(path, sizeof(path), instance_root, "worlds");
    if (maage_status_failed(result)) return result;
    if (!maage_storage_status(path)) return maage_status_make(MAAGE_STATUS_SUCCESS, 0, MAAGE_OPERATION_LOAD, 0);
    memset(&catalog, 0, sizeof(catalog)); catalog.root = path; catalog.items = items;
    result = maage_folder_link_list(path, maage_store_catalog_entry, &catalog);
    if (maage_status_failed(catalog.result)) result = catalog.result;
    if (maage_status_failed(result)) { free(catalog.entries); return MAAGE_STATUS_PROPAGATE(trace, result, MAAGE_CONTEXT_WORLD_IO, MAAGE_OPERATION_LOAD); }
    if (catalog.count > 1) qsort(catalog.entries, catalog.count, sizeof(*catalog.entries), maage_store_catalog_order);
    *entries = catalog.entries; *count = catalog.count; return result;
}
maage_status maage_world_store_rename(const char *instance_root, const char *id,
    const char *name, const maage_item_registry *items, char error[256], maage_status_trace *trace)
{
    maage_world_store *store;
    char worlds[MAAGE_PATH_CAPACITY], path[MAAGE_PATH_CAPACITY];
    maage_storage_lock *selection = NULL;
    maage_status result, closed;
    if (!name || !maage_store_name(name) || !id || !maage_storage_uuid_valid(id) ||
        !items || !maage_item_registry_is_finalized(items) ||
        !maage_folder_link_managed_path(instance_root, 0))
        return maage_store_fail(error, "Invalid world rename arguments.", MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_OPERATION_SAVE, trace);
    store = (maage_world_store *)calloc(1, sizeof(*store));
    if (!store) return maage_store_fail(error, "Cannot allocate world metadata.", MAAGE_STATUS_ALLOCATION_FAILED, MAAGE_OPERATION_SAVE, trace);
    store->items = items;
    result = maage_storage_join(path, sizeof(path), instance_root, "instance.json");
    if (!maage_status_failed(result) && maage_storage_status(path) != 1)
        result = maage_status_make(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_CONTEXT_WORLD_IO, MAAGE_OPERATION_VALIDATE, 0);
    if (!maage_status_failed(result)) result = maage_storage_join(worlds, sizeof(worlds), instance_root, "worlds");
    if (!maage_status_failed(result)) result = maage_storage_join(store->directory, sizeof(store->directory), worlds, id);
    if (!maage_status_failed(result) && !maage_folder_link_managed_path(store->directory, 0))
        result = maage_status_make(MAAGE_STATUS_ACCESS_DENIED, MAAGE_CONTEXT_WORLD_IO, MAAGE_OPERATION_SAVE, 0);
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
    int chunks;
    maage_status result;
} maage_store_remove;
static int maage_store_remove_entry(const char *name, maage_folder_link_kind kind, void *context)
{
    maage_store_remove *remove = (maage_store_remove *)context, child;
    char path[MAAGE_PATH_CAPACITY];
    if (!strcmp(name, ".world.lock")) return 1;
    remove->result = maage_storage_join(path, sizeof(path), remove->path, name);
    if (maage_status_failed(remove->result)) return 0;
    if (kind == MAAGE_FOLDER_LINK_FILE) remove->result = maage_storage_remove(path);
    else if (!remove->chunks && kind == MAAGE_FOLDER_LINK_DIRECTORY && !strcmp(name, "chunks")) {
        child.path = path; child.chunks = 1; child.result = 0;
        remove->result = maage_folder_link_list(path, maage_store_remove_entry, &child);
        if (maage_status_failed(child.result)) remove->result = child.result;
        if (!maage_status_failed(remove->result)) remove->result = maage_storage_remove_directory(path);
    } else remove->result = maage_status_make(MAAGE_STATUS_ACCESS_DENIED, MAAGE_CONTEXT_WORLD_IO, MAAGE_OPERATION_REMOVE, 0);
    return !maage_status_failed(remove->result);
}
maage_status maage_world_store_delete(const char *instance_root, const char *id,
    char error[256], maage_status_trace *trace)
{
    char worlds[MAAGE_PATH_CAPACITY], directory[MAAGE_PATH_CAPACITY], path[MAAGE_PATH_CAPACITY];
    maage_storage_lock *selection = NULL, *lock = NULL;
    maage_store_remove remove;
    maage_status result, closed;
    if (!id || !maage_storage_uuid_valid(id) || !maage_folder_link_managed_path(instance_root, 0))
        return maage_store_fail(error, "Invalid world deletion arguments.", MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_OPERATION_REMOVE, trace);
    result = maage_storage_join(path, sizeof(path), instance_root, "instance.json");
    if (!maage_status_failed(result) && maage_storage_status(path) != 1)
        result = maage_status_make(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_CONTEXT_WORLD_IO, MAAGE_OPERATION_VALIDATE, 0);
    if (!maage_status_failed(result)) result = maage_storage_join(worlds, sizeof(worlds), instance_root, "worlds");
    if (!maage_status_failed(result)) result = maage_storage_join(directory, sizeof(directory), worlds, id);
    if (!maage_status_failed(result) && !maage_folder_link_managed_path(directory, 0))
        result = maage_status_make(MAAGE_STATUS_ACCESS_DENIED, MAAGE_CONTEXT_WORLD_IO, MAAGE_OPERATION_REMOVE, 0);
    if (!maage_status_failed(result)) result = maage_storage_join(path, sizeof(path), worlds, ".selection.lock");
    if (!maage_status_failed(result)) result = maage_storage_acquire(path, &selection, trace);
    if (!maage_status_failed(result)) result = maage_storage_join(path, sizeof(path), directory, ".world.lock");
    if (!maage_status_failed(result)) result = maage_storage_acquire(path, &lock, trace);
    if (!maage_status_failed(result)) {
        remove.path = directory; remove.chunks = 0; remove.result = 0;
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
            if (!maage_status_failed(result) && (length == 36 || (length == 37 && selected[36] == '\n')) &&
                !memcmp(selected, id, 36)) result = maage_storage_remove(path);
            free(selected);
        }
    }
    closed = maage_storage_release(selection);
    if (!maage_status_failed(result)) result = closed;
    return MAAGE_STATUS_PROPAGATE(trace, result, MAAGE_CONTEXT_WORLD_IO, MAAGE_OPERATION_REMOVE);
}
