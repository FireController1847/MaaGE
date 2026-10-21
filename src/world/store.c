#include "world_internal.h"
#include "maage/world_store.h"
#include "maage/storage.h"
#include "maage/folder_link.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <time.h>
#include <limits.h>

/* V2 allows per-chunk palettes of up to 65536 namespaced state identities.
 * Reads allocate only the actual validated file size, not this maximum. */
#define MAAGE_STORE_CHUNK_MAX 18000000UL
/* Prevent save-data-driven allocations from growing without a file bound.
 * 16 MiB supports hundreds of thousands of overflow pickups. */
#define MAAGE_STORE_METADATA_MAX 16777216UL
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
static int maage_store_id(const char *id)
{
    size_t i, length = strlen(id);
    if (!length || length > 64) return 0;
    for (i = 0; i < length; ++i)
        if (!((id[i] >= 'a' && id[i] <= 'z') || (id[i] >= '0' && id[i] <= '9') ||
            (i && (id[i] == '-' || id[i] == '_')))) return 0;
    if (!strcmp(id, "con") || !strcmp(id, "prn") || !strcmp(id, "aux") || !strcmp(id, "nul") ||
        (length == 4 && (!memcmp(id, "com", 3) || !memcmp(id, "lpt", 3)) && id[3] >= '1' && id[3] <= '9')) return 0;
    return 1;
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
static unsigned char *maage_store_read(const char *path, size_t maximum, size_t *length, char error[256])
{
    unsigned char *data;
    FILE *file;
    long file_length;
    int failed;
    if (maage_storage_status(path) != 1) {
        maage_store_error(error, "World file is missing, unsafe, or inaccessible."); return NULL;
    }
    file = maage_storage_open(path, "rb");
    if (!file) { maage_store_error(error, "Could not open world file."); return NULL; }
    if (fseek(file, 0, SEEK_END) != 0 ||
        (file_length = ftell(file)) < 0 ||
        (unsigned long)file_length > maximum ||
        fseek(file, 0, SEEK_SET) != 0) {
        fclose(file);
        maage_store_error(error, "World file exceeds its format bound or could not be read.");
        return NULL;
    }
    *length = (size_t)file_length;
    data = (unsigned char *)malloc(*length ? *length : 1);
    if (!data) { fclose(file); maage_store_error(error, "Not enough memory to read world file."); return NULL; }
    failed = fread(data, 1, *length, file) != *length || ferror(file);
    if (fclose(file)) failed = 1;
    if (failed) {
        free(data); maage_store_error(error, "World file could not be read."); return NULL;
    }
    return data;
}
static int maage_store_write(const char *path, const unsigned char *data, size_t length, char error[256])
{
    char temporary[MAAGE_PATH_CAPACITY];
    FILE *file;
    int ok;
    if (strlen(path) + 5 > sizeof(temporary)) return maage_store_error(error, "World file path is too long.");
    strcpy(temporary, path); strcat(temporary, ".tmp");
    /* A stale temporary file must not retain an accidental hard link to a live
     * save. The lifetime lock excludes other processes; the task owner permits
     * only one write to this particular file at a time. Other files are disjoint. */
    if (!maage_storage_remove(temporary)) return maage_store_error(error, "Could not remove an abandoned temporary world file.");
    file = maage_storage_open(temporary, "wb");
    if (!file) return maage_store_error(error, "Could not create a temporary world file.");
    ok = fwrite(data, 1, length, file) == length && maage_storage_sync(file);
    if (fclose(file)) ok = 0;
    if (ok) ok = maage_storage_private_file(temporary) && maage_storage_replace(temporary, path);
    if (!ok) { maage_storage_remove(temporary); return maage_store_error(error, "World save failed; the previous file was preserved."); }
    return 1;
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
/* Item mapping 1 is intentionally restricted to these seven names and IDs.
 * An expanded runtime registry must never reinterpret an old inventory. */
static const char *maage_store_legacy_item_name(maage_item_id id)
{
    switch (id) {
        case 1: return "minecraft:stone";
        case 2: return "minecraft:grass";
        case 3: return "minecraft:dirt";
        case 7: return "minecraft:bedrock";
        case 12: return "minecraft:sand";
        case 13: return "minecraft:gravel";
        case 24: return "minecraft:sandstone";
        default: return NULL;
    }
}
static int maage_store_stack_valid(const maage_item_stack *stack)
{
    if (!stack->count) return !stack->item_id && !stack->metadata && !stack->damage;
    if (stack->count > 64 || stack->metadata || stack->damage) return 0;
    switch (stack->item_id) {
        case 1: case 2: case 3: case 7: case 12: case 13: case 24: return 1;
        default: return 0;
    }
}
static void maage_store_stack_put(maage_store_bytes *bytes, const maage_item_stack *stack)
{
    maage_store_put(bytes, stack->item_id, 4); maage_store_put(bytes, stack->count, 1);
    maage_store_put(bytes, stack->metadata, 4); maage_store_put(bytes, stack->damage, 4);
}
static void maage_store_stack_get(maage_store_bytes *bytes, maage_item_stack *stack)
{
    unsigned long value;
    stack->item_id = maage_store_get(bytes, 4);
    stack->count = (unsigned int)maage_store_get(bytes, 1);
    value = maage_store_get(bytes, 4); stack->metadata = (unsigned int)value;
    if ((unsigned long)stack->metadata != value) bytes->failed = 1;
    value = maage_store_get(bytes, 4); stack->damage = (unsigned int)value;
    if ((unsigned long)stack->damage != value || !maage_store_stack_valid(stack)) bytes->failed = 1;
}
static int maage_store_gameplay_valid(const maage_gameplay_save *save,
    const maage_item_registry *items)
{
    unsigned int i, axis;
    size_t maximum = (MAAGE_STORE_METADATA_MAX - 4096UL) / (items ? 34UL : 37UL);
    if (save->mode > MAAGE_GAME_MODE_CREATIVE || save->health > 20 || save->hunger > 20 ||
        save->inventory.selected >= MAAGE_HOTBAR_SLOTS ||
        !(save->saturation >= 0 && save->saturation <= save->hunger) ||
        !(save->exhaustion >= 0 && save->exhaustion <= 4) ||
        !(save->fall_distance >= 0 && save->fall_distance < MAAGE_WORLD_LIMIT) ||
        (save->flying != 0 && save->flying != 1) ||
        (save->mode == MAAGE_GAME_MODE_SURVIVAL && save->flying)) return 0;
    for (i = 0; i < MAAGE_INVENTORY_SLOTS; ++i)
        if (items ? !maage_item_stack_valid(items, &save->inventory.slots[i]) :
            !maage_store_stack_valid(&save->inventory.slots[i])) return 0;
    if (items ? !maage_item_stack_valid(items, &save->inventory.cursor) :
        !maage_store_stack_valid(&save->inventory.cursor)) return 0;
    for (i = 0; i < MAAGE_GAMEPLAY_PICKUPS; ++i) {
        if (items ? !maage_item_stack_valid(items, &save->pickups[i].stack) :
            !maage_store_stack_valid(&save->pickups[i].stack)) return 0;
        for (axis = 0; axis < 3; ++axis)
            if (!(save->pickups[i].position[axis] > -MAAGE_WORLD_LIMIT &&
                save->pickups[i].position[axis] < MAAGE_WORLD_LIMIT)) return 0;
        if (save->pickups[i].stack.count && !(save->pickups[i].position[1] >= 0 &&
            save->pickups[i].position[1] <= MAAGE_WORLD_HEIGHT)) return 0;
    }
    if (save->overflow_count > maximum ||
        (save->overflow_count && !save->overflow_pickups)) return 0;
    {
        size_t j;
        for (j = 0; j < save->overflow_count; ++j) {
            const maage_gameplay_pickup *pickup = &save->overflow_pickups[j];
            if (items ? !maage_item_stack_valid(items, &pickup->stack) :
                !maage_store_stack_valid(&pickup->stack)) return 0;
            for (axis = 0; axis < 3; ++axis)
                if (!(pickup->position[axis] > -MAAGE_WORLD_LIMIT &&
                    pickup->position[axis] < MAAGE_WORLD_LIMIT)) return 0;
            if (pickup->stack.count && !(pickup->position[1] >= 0 &&
                pickup->position[1] <= MAAGE_WORLD_HEIGHT)) return 0;
        }
    }
    return 1;
}
/* Format 4 uses complete stable item names rather than transient numeric IDs.
 * Names are resolved only at import/save boundaries; no gameplay string work. */
static void maage_store_stack_put_named(maage_store_bytes *bytes,
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
static void maage_store_stack_get_named(maage_store_bytes *bytes,
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
        if (bytes->failed || maage_item_registry_resolve_name(items, name,
            &stack->item_id) != MAAGE_REGISTRY_OK) { bytes->failed = 1; return; }
    }
    stack->count = (unsigned int)maage_store_get(bytes, 1);
    value = maage_store_get(bytes, 4); stack->metadata = (unsigned int)value;
    if ((unsigned long)stack->metadata != value) bytes->failed = 1;
    value = maage_store_get(bytes, 4); stack->damage = (unsigned int)value;
    if ((unsigned long)stack->damage != value || !maage_item_stack_valid(items, stack))
        bytes->failed = 1;
}
static void maage_store_stack_put_version(maage_store_bytes *bytes,
    const maage_item_stack *stack, const maage_item_registry *items)
{
    if (items) maage_store_stack_put_named(bytes, stack, items);
    else maage_store_stack_put(bytes, stack);
}
static void maage_store_stack_get_version(maage_store_bytes *bytes,
    maage_item_stack *stack, const maage_item_registry *items, unsigned long schema)
{
    if (schema >= 4) maage_store_stack_get_named(bytes, stack, items);
    else {
        const char *name;
        maage_store_stack_get(bytes, stack);
        if (items && stack->count) {
            const char *legacy = maage_store_legacy_item_name(stack->item_id);
            name = maage_item_registry_name(items, stack->item_id);
            if (!name || !legacy || strcmp(name, legacy)) bytes->failed = 1;
        }
    }
}
static size_t maage_store_named_length(const maage_gameplay_save *save,
    const maage_item_registry *items)
{
    size_t total = 0, j, length;
    unsigned int i;
    const maage_item_stack *stack;
    if (!items) return 0;
    for (i = 0; i <= MAAGE_INVENTORY_SLOTS; ++i) {
        stack = i == MAAGE_INVENTORY_SLOTS ?
            &save->inventory.cursor : &save->inventory.slots[i];
        if (!stack->count) continue;
        {
            const char *name = maage_item_registry_name(items, stack->item_id);
            if (!name || !(length = strlen(name)) || length > 255) return (size_t)-1;
            total += length;
        }
    }
    for (i = 0; i < MAAGE_GAMEPLAY_PICKUPS; ++i) {
        stack = &save->pickups[i].stack;
        if (!stack->count) continue;
        {
            const char *name = maage_item_registry_name(items, stack->item_id);
            if (!name || !(length = strlen(name)) || length > 255) return (size_t)-1;
            total += length;
        }
    }
    for (j = 0; j < save->overflow_count; ++j) {
        stack = &save->overflow_pickups[j].stack;
        if (!stack->count) continue;
        {
            const char *name = maage_item_registry_name(items, stack->item_id);
            if (!name || !(length = strlen(name)) || length > 255 ||
                total > MAAGE_STORE_METADATA_MAX - length) return (size_t)-1;
            total += length;
        }
    }
    return total;
}
static void maage_store_pickup_put(maage_store_bytes *bytes,
    const maage_gameplay_pickup *pickup, const maage_item_registry *items)
{
    unsigned int axis;
    maage_store_stack_put_version(bytes, &pickup->stack, items);
    for (axis = 0; axis < 3; ++axis)
        maage_store_double_put(bytes, pickup->position[axis]);
}
static void maage_store_gameplay_put(maage_store_bytes *bytes,
    const maage_gameplay_save *save, const maage_item_registry *items)
{
    unsigned int i;
    size_t j, pickups = 0;
    maage_store_put(bytes, items ? 2 : 1, 4); /* Item identity mapping version. */
    maage_store_put(bytes, save->mode, 1); maage_store_put(bytes, save->health, 1);
    maage_store_put(bytes, save->hunger, 1); maage_store_put(bytes, save->flying ? 1 : 0, 1);
    maage_store_put(bytes, save->inventory.selected, 1);
    maage_store_double_put(bytes, save->saturation); maage_store_double_put(bytes, save->exhaustion);
    maage_store_double_put(bytes, save->fall_distance);
    for (i = 0; i < MAAGE_INVENTORY_SLOTS; ++i)
        maage_store_stack_put_version(bytes, &save->inventory.slots[i], items);
    maage_store_stack_put_version(bytes, &save->inventory.cursor, items);
    for (i = 0; i < MAAGE_GAMEPLAY_PICKUPS; ++i)
        if (save->pickups[i].stack.count) ++pickups;
    for (j = 0; j < save->overflow_count; ++j)
        if (save->overflow_pickups[j].stack.count) ++pickups;
    if (pickups > MAAGE_STORE_WORD) { bytes->failed = 1; return; }
    maage_store_put(bytes, (unsigned long)pickups, 4);
    for (i = 0; i < MAAGE_GAMEPLAY_PICKUPS; ++i)
        if (save->pickups[i].stack.count) maage_store_pickup_put(bytes, &save->pickups[i], items);
    for (j = 0; j < save->overflow_count; ++j)
        if (save->overflow_pickups[j].stack.count) maage_store_pickup_put(bytes, &save->overflow_pickups[j], items);
}
static void maage_store_gameplay_get(maage_store_bytes *bytes, maage_gameplay_save *save,
    unsigned long schema, const maage_item_registry *items)
{
    unsigned int i, axis;
    unsigned long pickups;
    size_t overflow, j, record_size = schema >= 4 ? 34UL : 37UL;
    maage_gameplay_pickup *pickup;
    memset(save, 0, sizeof(*save));
    if (maage_store_get(bytes, 4) != (schema >= 4 ? 2UL : 1UL) ||
        (schema >= 4 && !items)) { bytes->failed = 1; return; }
    save->mode = (unsigned int)maage_store_get(bytes, 1); save->health = (unsigned int)maage_store_get(bytes, 1);
    save->hunger = (unsigned int)maage_store_get(bytes, 1); save->flying = (int)maage_store_get(bytes, 1);
    save->inventory.selected = (unsigned int)maage_store_get(bytes, 1);
    save->saturation = maage_store_double_get(bytes); save->exhaustion = maage_store_double_get(bytes);
    save->fall_distance = maage_store_double_get(bytes);
    for (i = 0; i < MAAGE_INVENTORY_SLOTS; ++i)
        maage_store_stack_get_version(bytes, &save->inventory.slots[i], items, schema);
    maage_store_stack_get_version(bytes, &save->inventory.cursor, items, schema);
    pickups = maage_store_get(bytes, schema >= 3 ? 4 : 1);
    if (bytes->failed || (schema == 2 && pickups > MAAGE_GAMEPLAY_PICKUPS) ||
        pickups > (MAAGE_STORE_METADATA_MAX - 4096UL) / record_size + MAAGE_GAMEPLAY_PICKUPS ||
        pickups > (bytes->length - bytes->position) / record_size) {
        bytes->failed = 1; return;
    }
    overflow = pickups > MAAGE_GAMEPLAY_PICKUPS ? (size_t)(pickups - MAAGE_GAMEPLAY_PICKUPS) : 0;
    if (overflow) {
        if (overflow > (size_t)-1 / sizeof(*save->overflow_pickups)) {
            bytes->failed = 1; return;
        }
        save->overflow_pickups = (maage_gameplay_pickup *)calloc(overflow,
            sizeof(*save->overflow_pickups));
        if (!save->overflow_pickups) { bytes->failed = 1; return; }
        save->overflow_count = overflow;
    }
    for (j = 0; j < pickups; ++j) {
        pickup = j < MAAGE_GAMEPLAY_PICKUPS ? &save->pickups[j] :
            &save->overflow_pickups[j - MAAGE_GAMEPLAY_PICKUPS];
        maage_store_stack_get_version(bytes, &pickup->stack, items, schema);
        if (!pickup->stack.count) bytes->failed = 1;
        for (axis = 0; axis < 3; ++axis) pickup->position[axis] = maage_store_double_get(bytes);
    }
    if (!maage_store_gameplay_valid(save, schema >= 4 ? items : NULL)) bytes->failed = 1;
}
static int maage_store_metadata_write(const maage_world_store *store,
    const maage_world_record *record, char error[256])
{
    unsigned char *data;
    maage_store_bytes bytes;
    size_t capacity, named_length;
    char path[MAAGE_PATH_CAPACITY];
    unsigned int i;
    if (!maage_generation_policy_valid(&record->generation_policy) ||
        (record->generation_policy.generator_version >= MAAGE_GENERATION_CONFIGURABLE_VERSION &&
         !store->items))
        return maage_store_error(error, "World generation policy is invalid or lacks item mapping.");
    if (!maage_store_gameplay_valid(&record->gameplay, store->items))
        return maage_store_error(error, "Player gameplay identity or pickup data is invalid.");
    if (!maage_environment_state_valid(&record->environment))
        return maage_store_error(error,"World environment snapshot is invalid.");
    named_length = maage_store_named_length(&record->gameplay, store->items);
    if (named_length == (size_t)-1 || named_length > MAAGE_STORE_METADATA_MAX)
        return maage_store_error(error, "Player item identity cannot be serialized.");
    if (record->gameplay.overflow_count >
        ((size_t)-1 - 4096UL) / (store->items ? 34UL : 37UL))
        return maage_store_error(error, "Player gameplay count exceeds addressable memory.");
    capacity = 4096UL + record->gameplay.overflow_count *
        (store->items ? 34UL : 37UL);
    if (named_length > (size_t)-1 - capacity)
        return maage_store_error(error, "Player item identity lengths overflow.");
    capacity += named_length;
    if (capacity > MAAGE_STORE_METADATA_MAX)
        return maage_store_error(error, "Player gameplay data exceeds the metadata file bound.");
    data = (unsigned char *)malloc(capacity);
    if (!data) return maage_store_error(error, "Not enough memory to encode player metadata.");
    bytes.data = data; bytes.length = capacity; bytes.position = 0; bytes.failed = 0;
    maage_store_text_put(&bytes, "MGWORLD1", 8);
    maage_store_put(&bytes,store->items ? MAAGE_WORLD_ENVIRONMENT_FORMAT_VERSION : 3UL,4);
    maage_store_put(&bytes, record->generation_policy.generator_version, 4);
    maage_store_put(&bytes, MAAGE_WORLD_BLOCKSET_VERSION, 4);
    maage_store_put(&bytes, record->seed.low, 4); maage_store_put(&bytes, record->seed.high, 4);
    maage_store_put(&bytes, strlen(record->id), 1); maage_store_put(&bytes, strlen(record->name), 1);
    maage_store_put(&bytes, strlen(record->created_utc), 1); maage_store_put(&bytes, record->new_world ? 1 : 0, 1);
    maage_store_text_put(&bytes, record->id, strlen(record->id));
    maage_store_text_put(&bytes, record->name, strlen(record->name));
    maage_store_text_put(&bytes, record->created_utc, strlen(record->created_utc));
    for (i = 0; i < 3; ++i) maage_store_double_put(&bytes, record->position[i]);
    maage_store_double_put(&bytes, record->yaw); maage_store_double_put(&bytes, record->pitch);
    maage_store_gameplay_put(&bytes, &record->gameplay, store->items);
    if (store->items) {
        maage_store_put(&bytes, record->generation_policy.schema_version, 4);
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
    }
    maage_store_checksum_put(&bytes);
    if (bytes.failed || !maage_storage_join(path, sizeof(path), store->directory, "world.bin")) {
        free(data); return maage_store_error(error, "Could not encode world metadata.");
    }
    {
        int ok = maage_store_write(path, data, bytes.position, error);
        free(data);
        return ok;
    }
}
static int maage_store_metadata_load(const maage_world_store *store,
    const char *expected_id, maage_world_record *record, char error[256])
{
    char path[MAAGE_PATH_CAPACITY];
    maage_store_bytes bytes;
    unsigned long lengths[3], format = 0, generator_version = 0, blockset_version = 0, value;
    size_t i, j;
    char *texts[3];
    int ok, gameplay_read = 0;
    if (!maage_storage_join(path, sizeof(path), store->directory, "world.bin")) return maage_store_error(error, "World metadata path is too long.");
    bytes.data = maage_store_read(path, MAAGE_STORE_METADATA_MAX, &bytes.length, error);
    if (!bytes.data) return 0;
    bytes.position = 0; bytes.failed = 0;
    ok = maage_store_checksum(&bytes) && maage_store_magic(&bytes, "MGWORLD1");
    if (ok) {
        format = maage_store_get(&bytes, 4);
        generator_version = maage_store_get(&bytes, 4);
        blockset_version = maage_store_get(&bytes, 4);
    }
    ok = ok && (format == 1 || format == 2 || format == 3 ||
        ((format == MAAGE_PLAYER_FORMAT_VERSION ||
          format == MAAGE_WORLD_CONFIGURED_FORMAT_VERSION ||
          format == MAAGE_WORLD_ENVIRONMENT_FORMAT_VERSION) && store->items != NULL)) &&
        ((generator_version == MAAGE_WORLD_GENERATOR_VERSION &&
          format != MAAGE_WORLD_CONFIGURED_FORMAT_VERSION) ||
         (generator_version == MAAGE_GENERATION_CONFIGURABLE_VERSION &&
          (format == MAAGE_WORLD_CONFIGURED_FORMAT_VERSION || format == MAAGE_WORLD_ENVIRONMENT_FORMAT_VERSION)) ||
         (generator_version == MAAGE_GENERATION_FEATURES_VERSION &&
          format == MAAGE_WORLD_ENVIRONMENT_FORMAT_VERSION)) &&
        blockset_version == MAAGE_WORLD_BLOCKSET_VERSION &&
        maage_generation_policy_default(generator_version, &record->generation_policy);
    if (ok) {
        record->seed.low = maage_store_get(&bytes, 4); record->seed.high = maage_store_get(&bytes, 4);
        maage_environment_default(record->seed,&record->environment);
        for (i = 0; i < 3; ++i) lengths[i] = maage_store_get(&bytes, 1);
        record->new_world = (int)maage_store_get(&bytes, 1);
        ok = record->new_world <= 1 && lengths[0] > 0 && lengths[0] < sizeof(record->id) &&
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
        if (format == 1) maage_store_gameplay_default(&record->gameplay);
        else maage_store_gameplay_get(&bytes, &record->gameplay, format, store->items);
        gameplay_read = 1;
        if (format == MAAGE_WORLD_CONFIGURED_FORMAT_VERSION || format == MAAGE_WORLD_ENVIRONMENT_FORMAT_VERSION) {
            value = maage_store_get(&bytes, 4);
            if (value > UINT_MAX) bytes.failed = 1;
            record->generation_policy.schema_version = (unsigned int)value;
            value = maage_store_get(&bytes, 4);
            if (value > INT_MAX) bytes.failed = 1;
            record->generation_policy.sea_level = (int)value;
            value = maage_store_get(&bytes, 4);
            if (value > INT_MAX) bytes.failed = 1;
            record->generation_policy.spawn_search_radius = (int)value;
            value = maage_store_get(&bytes, 4);
            if (value > INT_MAX) bytes.failed = 1;
            record->generation_policy.spawn_search_step = (int)value;
        }
        if (format == MAAGE_WORLD_ENVIRONMENT_FORMAT_VERSION) {
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
        }
        ok = ok && !bytes.failed && maage_generation_policy_valid(&record->generation_policy) &&
            maage_environment_state_valid(&record->environment) &&
            bytes.position == bytes.length && maage_store_id(record->id) &&
            !strcmp(record->id, expected_id) && maage_store_name(record->name) && maage_store_player(record);
        for (i = 0; i < 20; ++i) {
            char expected = (i == 4 || i == 7) ? '-' : (i == 10 ? 'T' : ((i == 13 || i == 16) ? ':' : (i == 19 ? 'Z' : 0)));
            if (expected ? record->created_utc[i] != expected : (record->created_utc[i] < '0' || record->created_utc[i] > '9')) ok = 0;
        }
    }
    free(bytes.data);
    if (!ok && gameplay_read) maage_gameplay_save_clear(&record->gameplay);
    return ok ? 1 : maage_store_error(error, "World metadata is corrupt or uses an unsupported format, generator, or block mapping.");
}
static int maage_store_random_id(char id[65])
{
    return maage_storage_uuid_generate(id);
}
int maage_world_store_open_configured(const char *instance_root, const char *id,
    int create, const char *name, const maage_world_seed *seed,
    const maage_generation_policy *generation_policy, const maage_item_registry *items,
    maage_world_store **output,
    maage_world_record *record, char error[256])
{
    char worlds[MAAGE_PATH_CAPACITY], config[MAAGE_PATH_CAPACITY], path[MAAGE_PATH_CAPACITY];
    char last[MAAGE_PATH_CAPACITY], chosen[65];
    maage_world_store *store = NULL;
    maage_storage_lock *selection = NULL;
    unsigned char *selected, random_seed[8];
    size_t length, i;
    time_t now;
    struct tm *utc;
    int status, ok = 0;
    *output = NULL;
    if (error) error[0] = 0;
    if (generation_policy && (!maage_generation_policy_valid(generation_policy) ||
        (generation_policy->generator_version >= MAAGE_GENERATION_CONFIGURABLE_VERSION &&
         !items))) return maage_store_error(error, "Invalid configured generator or missing item registry.");
    if (items && !maage_item_registry_is_finalized(items))
        return maage_store_error(error, "World storage requires finalized item definitions.");
    if ((id && (!maage_store_id(id) || (create && !maage_storage_uuid_valid(id)))) ||
        (name && !maage_store_name(name))) return maage_store_error(error, "New world IDs must be canonical UUIDs; world names are separate display metadata.");
    if (!maage_folder_link_managed_path(instance_root, 0) ||
        !maage_storage_join(path, sizeof(path), instance_root, "instance.json") || maage_storage_status(path) != 1)
        return maage_store_error(error, "World storage requires a physical instance root, never the global data directory.");
    if (!maage_storage_join(worlds, sizeof(worlds), instance_root, "worlds") || !maage_storage_directories(worlds) ||
        !maage_storage_join(config, sizeof(config), instance_root, "config") || !maage_storage_directories(config) ||
        !maage_storage_join(last, sizeof(last), config, "last-world") ||
        !maage_storage_join(path, sizeof(path), worlds, ".selection.lock")) return maage_store_error(error, "Could not prepare world storage directories.");
    selection = maage_storage_acquire(path);
    if (!selection) return maage_store_error(error, "Another process is selecting or creating a world.");
    if (id) strcpy(chosen, id);
    else if (create || seed) { create = 1; if (!maage_store_random_id(chosen)) goto random_failed; }
    else {
        status = maage_storage_status(last);
        if (!status) { create = 1; if (!maage_store_random_id(chosen)) goto random_failed; }
        else {
            selected = maage_store_read(last, 65, &length, error);
            if (!selected) goto done;
            if (length && selected[length - 1] == '\n') --length;
            if (!length || length > 64 || memchr(selected, 0, length)) { free(selected); maage_store_error(error, "The last-world selection is corrupt."); goto done; }
            memcpy(chosen, selected, length); chosen[length] = 0; free(selected);
            if (!maage_store_id(chosen)) { maage_store_error(error, "The last-world selection is invalid."); goto done; }
        }
    }
    store = (maage_world_store *)calloc(1, sizeof(*store));
    if (!store) { maage_store_error(error, "Not enough memory to open world storage."); goto done; }
    store->items = items;
    if (!maage_storage_join(store->directory, sizeof(store->directory), worlds, chosen)) { maage_store_error(error, "World path is too long."); goto done; }
    status = maage_storage_status(store->directory);
    if (create ? status != 0 : status != 2) {
        maage_store_error(error, create ? "The requested world ID already exists or cannot be safely created." : "The requested world does not exist or is inaccessible."); goto done;
    }
    if (!maage_storage_directories(store->directory) || !maage_storage_join(path, sizeof(path), store->directory, ".world.lock")) {
        maage_store_error(error, "Could not prepare the world directory."); goto done;
    }
    store->lock = maage_storage_acquire(path);
    if (!store->lock) { maage_store_error(error, "This world is already open in another process."); goto done; }
    if (!create && generation_policy) {
        maage_store_error(error, "Cannot override a saved world's generation policy."); goto done;
    }
    if (create) {
        if (generation_policy) store->record.generation_policy = *generation_policy;
        else if (!maage_generation_policy_default(MAAGE_WORLD_GENERATOR_VERSION,
            &store->record.generation_policy)) goto done;
        strcpy(store->record.id, chosen); strcpy(store->record.name, name ? name : "New World");
        store->record.new_world = 1; store->record.position[1] = 128;
        maage_store_gameplay_default(&store->record.gameplay);
        if (seed) store->record.seed = *seed;
        else {
            if (!maage_storage_random(random_seed, sizeof(random_seed))) goto random_failed;
            for (i = 0; i < 4; ++i) {
                store->record.seed.low |= (unsigned long)random_seed[i] << (i * 8);
                store->record.seed.high |= (unsigned long)random_seed[i + 4] << (i * 8);
            }
        }
        store->record.seed.high &= MAAGE_STORE_WORD; store->record.seed.low &= MAAGE_STORE_WORD;
        now = time(NULL); utc = gmtime(&now);
        if (now == (time_t)-1 || !utc || strftime(store->record.created_utc, sizeof(store->record.created_utc), "%Y-%m-%dT%H:%M:%SZ", utc) != 20) {
            maage_store_error(error, "Could not determine the world's creation time."); goto done;
        }
        maage_environment_default(store->record.seed,&store->record.environment);
        if (!maage_store_metadata_write(store, &store->record, error)) goto done;
    } else if (!maage_store_metadata_load(store, chosen, &store->record, error)) goto done;
    if (!maage_storage_join(store->chunks, sizeof(store->chunks), store->directory, "chunks") || !maage_storage_directories(store->chunks)) {
        maage_store_error(error, "Could not prepare chunk storage."); goto done;
    }
    length = strlen(chosen); chosen[length++] = '\n';
    if (!maage_store_write(last, (const unsigned char *)chosen, length, error)) goto done;
    *record = store->record; *output = store; store = NULL; ok = 1;
    goto done;
random_failed:
    maage_store_error(error, "Could not obtain secure randomness for world creation.");
done:
    maage_world_store_close(store); maage_storage_release(selection); return ok;
}

int maage_world_store_open_with_items(const char *instance_root, const char *id,
    int create, const char *name, const maage_world_seed *seed,
    const maage_item_registry *items, maage_world_store **output,
    maage_world_record *record, char error[256])
{
    return maage_world_store_open_configured(instance_root, id, create, name, seed,
        NULL, items, output, record, error);
}
int maage_world_store_open(const char *instance_root, const char *id, int create,
    const char *name, const maage_world_seed *seed, maage_world_store **output,
    maage_world_record *record, char error[256])
{
    return maage_world_store_open_configured(instance_root, id, create, name, seed,
        NULL, NULL, output, record, error);
}

typedef struct maage_store_migration {
    char source[MAAGE_PATH_CAPACITY];
    maage_storage_lock **locks;
    size_t count, capacity;
    char *error;
} maage_store_migration;

static int maage_store_migration_entry(const char *name, maage_folder_link_kind kind, void *context)
{
    maage_store_migration *migration = (maage_store_migration *)context;
    maage_world_store *world;
    maage_storage_lock *lock, **grown;
    char path[MAAGE_PATH_CAPACITY];
    size_t capacity;
    int ok;
    /* Preserve ordinary root files, including the selection lock, verbatim. */
    if (kind == MAAGE_FOLDER_LINK_FILE) return 1;
    if (kind != MAAGE_FOLDER_LINK_DIRECTORY || !maage_store_id(name))
        return maage_store_error(migration->error, "Legacy world storage contains an unsafe or unrecognized directory; migration is pending.");
    world = (maage_world_store *)calloc(1, sizeof(*world));
    if (!world) return maage_store_error(migration->error, "Not enough memory to inspect legacy worlds.");
    ok = maage_storage_join(world->directory, sizeof(world->directory), migration->source, name) &&
        maage_storage_join(path, sizeof(path), world->directory, ".world.lock");
    lock = ok ? maage_storage_acquire(path) : NULL;
    if (!lock) { free(world); return maage_store_error(migration->error, "A legacy world is active or inaccessible; close its Client/Server before migrating."); }
    ok = maage_store_metadata_load(world, name, &world->record, migration->error) &&
        maage_storage_join(path, sizeof(path), world->directory, "chunks") && maage_storage_directory_exists(path);
    free(world);
    if (!ok) { maage_storage_release(lock); return 0; }
    if (migration->count == migration->capacity) {
        capacity = migration->capacity ? migration->capacity * 2 : 16;
        if (capacity < migration->capacity || capacity > (size_t)-1 / sizeof(*grown)) {
            maage_storage_release(lock); return maage_store_error(migration->error, "Too many legacy worlds to acquire safely.");
        }
        grown = (maage_storage_lock **)realloc(migration->locks, capacity * sizeof(*grown));
        if (!grown) { maage_storage_release(lock); return maage_store_error(migration->error, "Not enough memory to lock legacy worlds."); }
        migration->locks = grown; migration->capacity = capacity;
    }
    migration->locks[migration->count++] = lock;
    return 1;
}

static int maage_store_migration_record(const char *path, const char *id,
    const char *last, int complete, char error[256])
{
    unsigned char data[128];
    maage_store_bytes bytes;
    bytes.data = data; bytes.length = sizeof(data); bytes.position = 0; bytes.failed = 0;
    maage_store_text_put(&bytes, "MGWMIGR1", 8); maage_store_put(&bytes, complete ? 1 : 0, 1);
    maage_store_text_put(&bytes, id, 36); maage_store_put(&bytes, strlen(last), 1);
    maage_store_text_put(&bytes, last, strlen(last)); maage_store_checksum_put(&bytes);
    return !bytes.failed && maage_store_write(path, data, bytes.position, error);
}

static int maage_store_migration_read(const char *path, char id[37], char last[65],
    int *complete, char error[256])
{
    maage_store_bytes bytes;
    unsigned int i, length;
    int ok;
    bytes.data = maage_store_read(path, 128, &bytes.length, error);
    if (!bytes.data) return 0;
    bytes.position = 0; bytes.failed = 0;
    ok = maage_store_checksum(&bytes) && maage_store_magic(&bytes, "MGWMIGR1");
    *complete = (int)maage_store_get(&bytes, 1);
    for (i = 0; i < 36; ++i) id[i] = (char)maage_store_get(&bytes, 1);
    id[36] = 0; length = (unsigned int)maage_store_get(&bytes, 1);
    if (length > 64) ok = 0;
    else {
        for (i = 0; i < length; ++i) last[i] = (char)maage_store_get(&bytes, 1);
        last[length] = 0;
        if (strlen(last) != length || (length && !maage_store_id(last))) ok = 0;
    }
    ok = ok && !bytes.failed && bytes.position == bytes.length && *complete <= 1 && maage_storage_uuid_valid(id);
    free(bytes.data);
    return ok ? 1 : maage_store_error(error, "Legacy migration journal is invalid; existing data is preserved.");
}

int maage_world_store_legacy_pending(const char *data_root)
{
    char path[MAAGE_PATH_CAPACITY], id[37], last[65], error[256];
    unsigned char *receipt;
    size_t length;
    int complete, matches;
    if (!maage_storage_join(path, sizeof(path), data_root, "config/worlds-migration")) return 1;
    if (maage_storage_status(path)) {
        if (!maage_store_migration_read(path, id, last, &complete, error)) return 1;
        if (complete) {
            if (!maage_storage_join(path, sizeof(path), data_root, "config/legacy-worlds-migrated")) return 1;
            receipt = maage_store_read(path, 36, &length, error);
            if (!receipt) return 1;
            matches = length == 36 && !memcmp(receipt, id, 36); free(receipt);
            return !matches; /* Verified-copy originals are retained as backup. */
        }
        return 1;
    }
    return !maage_storage_join(path, sizeof(path), data_root, "worlds") || maage_storage_status(path) != 0;
}

static int maage_store_migration_selection(const char *path, const char *last,
    int write, char error[256])
{
    unsigned char *data;
    size_t length;
    int status = maage_storage_status(path), matches;
    if (status == 0) return !write || !last[0] || maage_store_write(path, (const unsigned char *)last, strlen(last), error);
    if (status != 1) return maage_store_error(error, "Destination last-world selection is unsafe; migration is pending.");
    data = maage_store_read(path, 65, &length, error);
    if (!data) return 0;
    if (length && data[length - 1] == '\n') --length;
    matches = length == strlen(last) && !memcmp(data, last, length);
    free(data);
    return matches ? 1 : maage_store_error(error, "Destination last-world selection conflicts with migration; both collections are preserved.");
}

int maage_world_store_migrate_legacy(const char *data_root,
    const char *instance_root, char error[256])
{
    maage_store_migration migration;
    maage_storage_lock *guard = NULL, *selection = NULL;
    char destination[MAAGE_PATH_CAPACITY], config[MAAGE_PATH_CAPACITY], journal[MAAGE_PATH_CAPACITY];
    char path[MAAGE_PATH_CAPACITY], selected_path[MAAGE_PATH_CAPACITY], staged[MAAGE_PATH_CAPACITY];
    char destination_id[37], recorded_id[37], last[65] = "", recorded_last[65], attempt[37], stage_name[64];
    const char *id;
    unsigned char *data;
    size_t length, i;
    int status, recorded = 0, complete = 0, moved, ok = 0;
    memset(&migration, 0, sizeof(migration)); migration.error = error;
    if (error) error[0] = 0;
    id = strrchr(instance_root, '/'); id = id ? id + 1 : "";
    if (!maage_storage_uuid_valid(id) || !maage_folder_link_managed_path(data_root, 0) ||
        !maage_folder_link_managed_path(instance_root, 0)) goto failed;
    strcpy(destination_id, id);
    if (!maage_storage_join(path, sizeof(path), data_root, "instances") ||
        !maage_folder_link_within(path, instance_root) ||
        !maage_storage_join(config, sizeof(config), data_root, "config") || !maage_storage_directories(config) ||
        !maage_storage_join(journal, sizeof(journal), config, "worlds-migration") ||
        !maage_storage_join(path, sizeof(path), config, "worlds-migration.lock") ||
        !(guard = maage_storage_acquire(path)) ||
        !maage_storage_join(migration.source, sizeof(migration.source), data_root, "worlds") ||
        !maage_storage_join(destination, sizeof(destination), instance_root, "worlds") ||
        !maage_storage_join(path, sizeof(path), instance_root, "config") || !maage_storage_directories(path) ||
        !maage_storage_join(selected_path, sizeof(selected_path), path, "last-world")) goto failed;
    if (maage_storage_status(journal)) {
        if (!maage_store_migration_read(journal, recorded_id, recorded_last, &complete, error)) goto done;
        if (strcmp(recorded_id, destination_id)) { maage_store_error(error, "Legacy migration belongs to another instance; its journal and worlds are preserved."); goto done; }
        if (complete) goto receipt;
        recorded = 1; strcpy(last, recorded_last);
    }
    status = maage_storage_status(migration.source);
    if (status == 0) {
        if (!recorded) { ok = 1; goto done; }
        if (maage_storage_status(destination) != 2) goto failed;
        goto finalize;
    }
    if (status != 2 || !maage_folder_link_managed_path(migration.source, 0) ||
        !maage_storage_join(path, sizeof(path), migration.source, ".selection.lock") ||
        !(selection = maage_storage_acquire(path))) goto failed;
    if (!maage_folder_link_list(migration.source, maage_store_migration_entry, &migration)) goto failed;
    if (!recorded) {
        if (!maage_storage_join(path, sizeof(path), config, "last-world")) goto failed;
        status = maage_storage_status(path);
        if (status) {
            data = maage_store_read(path, 65, &length, error);
            if (!data) goto done;
            if (length && data[length - 1] == '\n') --length;
            if (!length || length > 64 || memchr(data, 0, length)) { free(data); goto failed; }
            memcpy(last, data, length); last[length] = 0; free(data);
            if (!maage_store_id(last) || !maage_storage_join(path, sizeof(path), migration.source, last) ||
                maage_storage_status(path) != 2) goto failed;
        }
    }
    if (!maage_store_migration_selection(selected_path, last, 0, error)) goto done;
    /* A completed copy may have been promoted immediately before interruption. */
    if (recorded && maage_storage_status(destination) == 2 &&
        maage_folder_link_equal_tree(migration.source, destination)) goto finalize;
    if (maage_storage_status(destination) == 2) maage_storage_remove_directory(destination);
    if (maage_storage_status(destination) != 0) {
        maage_store_error(error, "Destination already contains world data; legacy migration is pending without overwriting it."); goto done;
    }
    if (!maage_store_migration_record(journal, destination_id, last, 0, error)) goto done;
    moved = maage_folder_link_move(migration.source, destination);
    if (moved != 1) {
        /* Different volumes or open Windows directory handles use a verified
         * private staging copy. Originals remain available as legacy backup. */
        if (!maage_storage_uuid_generate(attempt)) goto failed;
        sprintf(stage_name, ".worlds-migration-%s", attempt);
        if (!maage_storage_join(staged, sizeof(staged), instance_root, stage_name) ||
            !maage_folder_link_copy_verified(migration.source, staged) ||
            maage_folder_link_move(staged, destination) != 1) goto failed;
    }
finalize:
    if (!maage_store_migration_selection(selected_path, last, 1, error)) goto done;
    if (!maage_store_migration_record(journal, destination_id, last, 1, error)) goto done;
receipt:
    /* A simple UI receipt complements the CRC journal; it never authorizes
     * migration or replaces the world's lifetime lock. */
    if (!maage_storage_join(path, sizeof(path), config, "legacy-worlds-migrated") ||
        !maage_store_write(path, (const unsigned char *)destination_id, 36, error)) goto failed;
    ok = 1; goto done;
failed:
    if (!error || !*error) maage_store_error(error, "Legacy migration is pending: active data, unsafe paths, conflicting files or unavailable atomic storage. Originals/staging are preserved.");
done:
    for (i = 0; i < migration.count; ++i) maage_storage_release(migration.locks[i]);
    free(migration.locks); maage_storage_release(selection); maage_storage_release(guard);
    return ok;
}

int maage_world_store_save_player(maage_world_store *store,
    const double position[3], double yaw, double pitch, char error[256])
{
    maage_world_record record = store->record, existing;
    /* Recheck the live metadata before replacement, just as chunk replacement
     * rechecks its old file. Corruption never becomes a successful overwrite. */
    if (!maage_store_metadata_load(store, record.id, &existing, error)) return 0;
    if (existing.seed.high != record.seed.high || existing.seed.low != record.seed.low ||
        existing.generation_policy.schema_version != record.generation_policy.schema_version ||
        existing.generation_policy.generator_version != record.generation_policy.generator_version ||
        existing.generation_policy.sea_level != record.generation_policy.sea_level ||
        existing.generation_policy.spawn_search_radius != record.generation_policy.spawn_search_radius ||
        existing.generation_policy.spawn_search_step != record.generation_policy.spawn_search_step) {
        maage_gameplay_save_clear(&existing.gameplay);
        return maage_store_error(error, "World metadata changed its seed while open; the save was preserved.");
    }
    memcpy(record.position, position, sizeof(record.position));
    record.gameplay = existing.gameplay;
    record.environment=existing.environment;
    record.yaw = yaw; record.pitch = pitch; record.new_world = 0;
    if (!maage_store_player(&record)) {
        maage_gameplay_save_clear(&existing.gameplay);
        return maage_store_error(error, "Player state is outside the world format's supported bounds.");
    }
    /* Store identity stays immutable while independent chunk jobs borrow it. */
    {
        int saved = maage_store_metadata_write(store, &record, error);
        maage_gameplay_save_clear(&existing.gameplay);
        return saved;
    }
}
int maage_world_store_save_environment_gameplay(maage_world_store *store,
    const double position[3], double yaw, double pitch,
    const maage_gameplay *game, const maage_item_registry *items,
    const maage_environment_state *environment, char error[256])
{
    maage_world_record record, existing;
    maage_gameplay restored;
    int valid, saved;
    if (!store || !position || !game || !items) return maage_store_error(error, "Missing player gameplay snapshot.");
    if (store->items && store->items != items)
        return maage_store_error(error, "Player item registry changed while the world was open.");
    record = store->record;
    if (!maage_store_metadata_load(store, record.id, &existing, error)) return 0;
    if (existing.seed.high != record.seed.high || existing.seed.low != record.seed.low ||
        existing.generation_policy.schema_version != record.generation_policy.schema_version ||
        existing.generation_policy.generator_version != record.generation_policy.generator_version ||
        existing.generation_policy.sea_level != record.generation_policy.sea_level ||
        existing.generation_policy.spawn_search_radius != record.generation_policy.spawn_search_radius ||
        existing.generation_policy.spawn_search_step != record.generation_policy.spawn_search_step) {
        maage_gameplay_save_clear(&existing.gameplay);
        return maage_store_error(error, "World metadata changed its seed while open; the save was preserved.");
    }
    memcpy(record.position, position, sizeof(record.position)); record.yaw = yaw; record.pitch = pitch;
    record.environment=environment ? *environment : existing.environment;
    record.new_world = 0;
    if (!maage_gameplay_snapshot(game, &record.gameplay)) {
        maage_gameplay_save_clear(&existing.gameplay);
        return maage_store_error(error, "Not enough memory to snapshot player gameplay.");
    }
    memset(&restored, 0, sizeof(restored));
    valid = maage_store_player(&record) &&
        maage_environment_state_valid(&record.environment) &&
        maage_store_gameplay_valid(&record.gameplay, store->items) &&
        maage_gameplay_restore(&restored, items, &record.gameplay);
    saved = valid && maage_store_metadata_write(store, &record, error);
    maage_gameplay_destroy(&restored);
    maage_gameplay_save_clear(&record.gameplay);
    maage_gameplay_save_clear(&existing.gameplay);
    if (!valid)
        return maage_store_error(error, "Player gameplay snapshot is invalid or uses an unmapped item identity.");
    return saved;
}
int maage_world_store_save_gameplay(maage_world_store *store,
    const double position[3], double yaw, double pitch,
    const maage_gameplay *game, const maage_item_registry *items, char error[256])
{
    return maage_world_store_save_environment_gameplay(store,position,yaw,pitch,game,items,NULL,error);
}
void maage_world_store_close(maage_world_store *store)
{
    if (!store) return;
    maage_gameplay_save_clear(&store->record.gameplay);
    maage_storage_release(store->lock); free(store);
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
    return maage_storage_join(path, MAAGE_PATH_CAPACITY, store->chunks, relative);
}
/* V1 numeric mapping must remain exact: reject any reinterpreted old ID. */
static int maage_store_legacy_state(const maage_block_registry *blocks, unsigned long id)
{
    const maage_block_state_definition *state = maage_block_registry_state(blocks, id);
    const char *name, *expected = NULL;
    if (!state) return 0;
    name = maage_block_registry_block_name(blocks, state->block_id);
    if (id >= 256 && id < 262)
        return state->block_id == 2 && state->metadata == id - 255 &&
            name && strcmp(name, "minecraft:grass") == 0;
    switch (id) {
        case 0: expected = "minecraft:air"; break;
        case 1: expected = "minecraft:stone"; break;
        case 2: expected = "minecraft:grass"; break;
        case 3: expected = "minecraft:dirt"; break;
        case 7: expected = "minecraft:bedrock"; break;
        case 9: expected = "minecraft:water"; break;
        case 12: expected = "minecraft:sand"; break;
        case 13: expected = "minecraft:gravel"; break;
        case 24: expected = "minecraft:sandstone"; break;
        default: return 0;
    }
    return state->block_id == id && state->metadata == 0 &&
        name && strcmp(name, expected) == 0;
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
    if (bytes->failed || chunk->environment_initialized>1 || count>65536UL || count>(bytes->length-bytes->position)/14UL) return 0;
    for (i=0;i<count;++i) {
        if (maage_store_get(bytes,4)!=MAAGE_BLOCK_UPDATE_FLUID) return 0;
        update.kind=MAAGE_BLOCK_UPDATE_FLUID; update.cell=(unsigned int)maage_store_get(bytes,2);
        update.due_low=maage_store_get(bytes,4); update.due_high=maage_store_get(bytes,4);
        before=chunk->update_count;
        if (bytes->failed || !maage_chunk_schedule_update(chunk,&update) || chunk->update_count!=before+1) return 0;
    }
    count=maage_store_get(bytes,4);
    if (bytes->failed || count>65536UL || count>(bytes->length-bytes->position)/14UL ||
        count>(size_t)-1/sizeof(*chunk->entities)) return 0;
    if (count) {
        chunk->entities=(maage_block_entity *)calloc((size_t)count,sizeof(*chunk->entities));
        if (!chunk->entities) return 0;
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
            if (!entity->data) return 0;
            memcpy(entity->data,bytes->data+bytes->position,(size_t)length); bytes->position+=(size_t)length;
        }
    }
    chunk->updates_relative=1;
    return !bytes->failed;
}
static int maage_store_chunk_simulation_size(const maage_chunk *chunk,
    const maage_block_registry *blocks, size_t *length)
{
    unsigned char occupied[8192];
    const maage_block_entity *entity;
    const maage_block_state_definition *state;
    size_t i, size=45UL;
    if (chunk->update_count>65536UL || chunk->entity_count>65536UL ||
        chunk->generation_stages>255U || (chunk->environment_initialized!=0 && chunk->environment_initialized!=1)) return 0;
    size+=chunk->update_count*14UL+chunk->entity_count*14UL;
    memset(occupied,0,sizeof(occupied));
    for (i=0;i<chunk->update_count;++i) {
        const maage_scheduled_update *update=&chunk->updates[i];
        if (update->kind!=MAAGE_BLOCK_UPDATE_FLUID || update->cell>65535U ||
            update->due_low>MAAGE_STORE_WORD || update->due_high>MAAGE_STORE_WORD ||
            (occupied[update->cell/8]&(1U<<(update->cell%8)))) return 0;
        occupied[update->cell/8]|=(unsigned char)(1U<<(update->cell%8));
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
static int maage_store_chunk_decode(unsigned char *data, size_t length, int x, int z,
    const maage_block_registry *blocks, const maage_biome_registry *biomes,
    unsigned long generator_version, maage_chunk **output)
{
    maage_store_bytes bytes;
    maage_chunk *chunk = NULL;
    maage_block_state_id *palette = NULL, state;
    maage_biome_id biome_palette[256], biome;
    maage_block_id block;
    unsigned long mask, stored_x, stored_z, expected_x, expected_z, value, format;
    unsigned long state_count = 0, biome_count = 0;
    size_t i, section, expected_length = 800, active_cells = 0;
    unsigned int metadata;
    char name[256];
    int modern, simulation;
    bytes.data = data; bytes.length = length; bytes.position = 0; bytes.failed = 0;
    if (length < 804 || !maage_store_checksum(&bytes)) return 0;
    simulation=memcmp(data,"MGCHNK3\0",8)==0;
    modern = simulation || memcmp(data, "MGCHNK2\0", 8) == 0;
    if (!maage_store_magic(&bytes,simulation ? "MGCHNK3\0" : (modern ? "MGCHNK2\0" : "MGCHNK1\0")))
        return 0;
    format = maage_store_get(&bytes, 4);
    if (format != (simulation ? 3UL : (modern ? 2UL : 1UL)) ||
        maage_store_get(&bytes, 4) != generator_version ||
        maage_store_get(&bytes, 4) != MAAGE_WORLD_BLOCKSET_VERSION) return 0;
    stored_x = maage_store_get(&bytes, 4); stored_z = maage_store_get(&bytes, 4);
    expected_x = (unsigned long)x & MAAGE_STORE_WORD;
    expected_z = (unsigned long)z & MAAGE_STORE_WORD;
    if (stored_x != expected_x || stored_z != expected_z) return 0;
    mask = maage_store_get(&bytes, 2);
    if (maage_store_get(&bytes, 2) != 0 || bytes.failed) return 0;
    for (section = 0; section < 16; ++section)
        if (mask & (1UL << section)) {
            active_cells += MAAGE_SECTION_VOLUME;
            expected_length += MAAGE_SECTION_VOLUME * 3;
        }
    if (!modern && bytes.length != expected_length) return 0;
    if (modern) {
        state_count = maage_store_get(&bytes, 4);
        biome_count = maage_store_get(&bytes, 2);
        if (maage_store_get(&bytes, 2) != 0 || bytes.failed ||
            state_count > active_cells || biome_count == 0 || biome_count > 256 ||
            bytes.length < expected_length + 8UL + state_count * 5UL + biome_count)
            return 0;
        if (state_count) {
            palette = (maage_block_state_id *)malloc((size_t)state_count * sizeof(*palette));
            if (!palette) return 0;
        }
        for (i = 0; i < state_count; ++i) {
            if (!maage_store_identity_get(&bytes, name)) goto failed;
            value = maage_store_get(&bytes, 4);
            metadata = (unsigned int)value;
            if ((unsigned long)metadata != value ||
                maage_block_registry_resolve_name(blocks, name, &block) != MAAGE_REGISTRY_OK ||
                maage_block_registry_resolve_state(blocks, block, metadata, &state) != MAAGE_REGISTRY_OK)
                goto failed;
            palette[i] = state;
        }
        for (i = 0; i < biome_count; ++i) {
            if (!maage_store_identity_get(&bytes, name) ||
                maage_biome_registry_resolve_name(biomes, name, &biome) != MAAGE_REGISTRY_OK)
                goto failed;
            biome_palette[i] = biome;
        }
        if (bytes.failed || (simulation ? bytes.length-bytes.position<expected_length-32+45UL :
            bytes.length-bytes.position!=expected_length-32)) goto failed;
    }
    chunk = maage_chunk_create(x, z);
    if (!chunk) goto failed;
    for (i = 0; i < 256; ++i) {
        value = maage_store_get(&bytes, 1);
        if (modern) {
            if (value >= biome_count) goto failed;
            chunk->biomes[i] = biome_palette[value];
        } else {
            if (value >= MAAGE_BIOME_COUNT ||
                !maage_biome_registry_get(biomes, (maage_biome_id)value)) goto failed;
            chunk->biomes[i] = (maage_biome_id)value;
        }
    }
    for (i = 0; i < 256; ++i) {
        value = maage_store_get(&bytes, 2);
        if (value > MAAGE_WORLD_HEIGHT) goto failed;
        chunk->heightmap[i] = (unsigned short)value;
    }
    for (section = 0; section < 16; ++section) {
        if (!(mask & (1UL << section))) continue;
        chunk->sections[section] = (maage_section *)malloc(sizeof(maage_section));
        if (!chunk->sections[section]) goto failed;
        for (i = 0; i < MAAGE_SECTION_VOLUME; ++i) {
            value = maage_store_get(&bytes, 2);
            if (modern) {
                if (value >= state_count) goto failed;
                state = palette[value];
            } else {
                if (!maage_store_legacy_state(blocks, value)) goto failed;
                state = (maage_block_state_id)value;
            }
            chunk->sections[section]->states[i] = state;
        }
        for (i = 0; i < MAAGE_SECTION_VOLUME; ++i)
            chunk->sections[section]->light[i] = (unsigned char)maage_store_get(&bytes, 1);
    }
    if (simulation && !maage_store_chunk_simulation_get(&bytes,chunk,blocks)) goto failed;
    if (bytes.failed || bytes.position != bytes.length) goto failed;
    free(palette);
    *output = chunk;
    return 1;
failed:
    free(palette);
    maage_chunk_destroy(chunk);
    return 0;
}
int maage_world_store_load_chunk(maage_world_store *store, int x, int z,
    const maage_block_registry *blocks, const maage_biome_registry *biomes,
    maage_chunk **chunk, char error[256])
{
    char path[MAAGE_PATH_CAPACITY];
    unsigned char *data;
    size_t length;
    int ok, status;
    if (!chunk || !store || !blocks || !biomes) {
        maage_store_error(error, "Missing chunk load arguments."); return -1;
    }
    *chunk = NULL;
    if (!maage_store_chunk_path(store, x, z, path)) {
        maage_store_error(error, "Chunk coordinates or path exceed the world format bounds."); return -1;
    }
    status = maage_storage_status(path);
    if (!status) return 0;
    data = maage_store_read(path, MAAGE_STORE_CHUNK_MAX, &length, error);
    if (!data) return -1;
    ok = maage_store_chunk_decode(data, length, x, z, blocks, biomes,
        store->record.generation_policy.generator_version, chunk);
    free(data);
    if (!ok) {
        maage_store_error(error, "Chunk data is corrupt, unmapped, or incompatible; the save was preserved."); return -1;
    }
    return 1;
}
int maage_world_store_save_chunk_at(maage_world_store *store, const maage_chunk *chunk,
    const maage_block_registry *blocks, const maage_biome_registry *biomes,
    unsigned long tick_low, unsigned long tick_high, char error[256])
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
    size_t capacity = 800UL + 8UL + 4UL, simulation_length;
    int result;
    if (!store || !chunk || !blocks || !biomes ||
        !maage_store_chunk_path(store, chunk->x, chunk->z, path))
        return maage_store_error(error, "Chunk coordinates or path exceed the world format bounds.");
    result = maage_world_store_load_chunk(store, chunk->x, chunk->z, blocks, biomes, &existing, error);
    if (result < 0) return 0;
    maage_chunk_destroy(existing);
    if (tick_low>MAAGE_STORE_WORD || tick_high>MAAGE_STORE_WORD ||
        !maage_store_chunk_simulation_size(chunk,blocks,&simulation_length)) goto incompatible;
    capacity+=simulation_length;
    for (section = 0; section < 16; ++section)
        if (chunk->sections[section]) { mask |= 1UL << section; cells += MAAGE_SECTION_VOLUME; }
    if (cells) {
        states = (maage_block_state_id *)malloc(cells * sizeof(*states));
        if (!states) return maage_store_error(error, "Not enough memory for the chunk identity palette.");
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
        free(states); return maage_store_error(error, "Not enough memory to encode a chunk.");
    }
    bytes.length = capacity; bytes.position = 0; bytes.failed = 0;
    maage_store_text_put(&bytes, "MGCHNK3\0", 8);
    maage_store_put(&bytes, MAAGE_WORLD_FORMAT_VERSION, 4);
    maage_store_put(&bytes, store->record.generation_policy.generator_version, 4);
    maage_store_put(&bytes, MAAGE_WORLD_BLOCKSET_VERSION, 4);
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
    result = !bytes.failed && bytes.position == capacity &&
        maage_store_write(path, bytes.data, bytes.position, error);
    free(encoded); free(states);
    return result ? 1 : maage_store_error(error, "Chunk save failed; the previous file was preserved.");
incompatible:
    free(encoded); free(states);
    return maage_store_error(error, "Chunk has invalid heights or unresolved registered content identities.");
}
int maage_world_store_save_chunk(maage_world_store *store, const maage_chunk *chunk,
    const maage_block_registry *blocks, const maage_biome_registry *biomes, char error[256])
{
    if (!store) return maage_store_error(error,"Missing world store.");
    return maage_world_store_save_chunk_at(store,chunk,blocks,biomes,
        store->record.environment.ticks_low,store->record.environment.ticks_high,error);
}
