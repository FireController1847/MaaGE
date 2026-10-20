#include "world_internal.h"
#include "maage/world_store.h"
#include "maage/storage.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <time.h>

#define MAAGE_STORE_CHUNK_MAX 197412UL
#define MAAGE_STORE_METADATA_MAX 4096
#define MAAGE_STORE_WORD 4294967295UL
struct maage_world_store {
    char directory[MAAGE_PATH_CAPACITY], chunks[MAAGE_PATH_CAPACITY];
    maage_storage_lock *lock;
    maage_world_record record;
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
    int failed;
    if (maage_storage_status(path) != 1) { maage_store_error(error, "World file is missing, unsafe, or inaccessible."); return NULL; }
    file = maage_storage_open(path, "rb");
    if (!file) { maage_store_error(error, "Could not open world file."); return NULL; }
    data = (unsigned char *)malloc(maximum + 1);
    if (!data) { fclose(file); maage_store_error(error, "Not enough memory to read world file."); return NULL; }
    *length = fread(data, 1, maximum + 1, file);
    failed = ferror(file) || *length > maximum;
    if (fclose(file)) failed = 1;
    if (failed) { free(data); maage_store_error(error, "World file exceeds its format bound or could not be read."); return NULL; }
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
static int maage_store_gameplay_valid(const maage_gameplay_save *save)
{
    unsigned int i, axis;
    if (save->mode > MAAGE_GAME_MODE_CREATIVE || save->health > 20 || save->hunger > 20 ||
        save->inventory.selected >= MAAGE_HOTBAR_SLOTS ||
        !(save->saturation >= 0 && save->saturation <= save->hunger) ||
        !(save->exhaustion >= 0 && save->exhaustion <= 4) ||
        !(save->fall_distance >= 0 && save->fall_distance < MAAGE_WORLD_LIMIT) ||
        (save->flying != 0 && save->flying != 1) ||
        (save->mode == MAAGE_GAME_MODE_SURVIVAL && save->flying)) return 0;
    for (i = 0; i < MAAGE_INVENTORY_SLOTS; ++i)
        if (!maage_store_stack_valid(&save->inventory.slots[i])) return 0;
    if (!maage_store_stack_valid(&save->inventory.cursor)) return 0;
    for (i = 0; i < MAAGE_GAMEPLAY_PICKUPS; ++i) {
        if (!maage_store_stack_valid(&save->pickups[i].stack)) return 0;
        for (axis = 0; axis < 3; ++axis)
            if (!(save->pickups[i].position[axis] > -MAAGE_WORLD_LIMIT &&
                save->pickups[i].position[axis] < MAAGE_WORLD_LIMIT)) return 0;
        if (save->pickups[i].stack.count && !(save->pickups[i].position[1] >= 0 &&
            save->pickups[i].position[1] <= MAAGE_WORLD_HEIGHT)) return 0;
    }
    return 1;
}
static void maage_store_gameplay_put(maage_store_bytes *bytes, const maage_gameplay_save *save)
{
    unsigned int i, axis, pickups = 0;
    maage_store_put(bytes, 1, 4); /* Stable item mapping version. */
    maage_store_put(bytes, save->mode, 1); maage_store_put(bytes, save->health, 1);
    maage_store_put(bytes, save->hunger, 1); maage_store_put(bytes, save->flying ? 1 : 0, 1);
    maage_store_put(bytes, save->inventory.selected, 1);
    maage_store_double_put(bytes, save->saturation); maage_store_double_put(bytes, save->exhaustion);
    maage_store_double_put(bytes, save->fall_distance);
    for (i = 0; i < MAAGE_INVENTORY_SLOTS; ++i) maage_store_stack_put(bytes, &save->inventory.slots[i]);
    maage_store_stack_put(bytes, &save->inventory.cursor);
    for (i = 0; i < MAAGE_GAMEPLAY_PICKUPS; ++i) if (save->pickups[i].stack.count) ++pickups;
    maage_store_put(bytes, pickups, 1);
    for (i = 0; i < MAAGE_GAMEPLAY_PICKUPS; ++i) {
        if (!save->pickups[i].stack.count) continue;
        maage_store_stack_put(bytes, &save->pickups[i].stack);
        for (axis = 0; axis < 3; ++axis) maage_store_double_put(bytes, save->pickups[i].position[axis]);
    }
}
static void maage_store_gameplay_get(maage_store_bytes *bytes, maage_gameplay_save *save)
{
    unsigned int i, axis, pickups;
    memset(save, 0, sizeof(*save));
    if (maage_store_get(bytes, 4) != 1) { bytes->failed = 1; return; }
    save->mode = (unsigned int)maage_store_get(bytes, 1); save->health = (unsigned int)maage_store_get(bytes, 1);
    save->hunger = (unsigned int)maage_store_get(bytes, 1); save->flying = (int)maage_store_get(bytes, 1);
    save->inventory.selected = (unsigned int)maage_store_get(bytes, 1);
    save->saturation = maage_store_double_get(bytes); save->exhaustion = maage_store_double_get(bytes);
    save->fall_distance = maage_store_double_get(bytes);
    for (i = 0; i < MAAGE_INVENTORY_SLOTS; ++i) maage_store_stack_get(bytes, &save->inventory.slots[i]);
    maage_store_stack_get(bytes, &save->inventory.cursor);
    pickups = (unsigned int)maage_store_get(bytes, 1);
    if (pickups > MAAGE_GAMEPLAY_PICKUPS) { bytes->failed = 1; return; }
    for (i = 0; i < pickups; ++i) {
        maage_store_stack_get(bytes, &save->pickups[i].stack);
        if (!save->pickups[i].stack.count) bytes->failed = 1;
        for (axis = 0; axis < 3; ++axis) save->pickups[i].position[axis] = maage_store_double_get(bytes);
    }
    if (!maage_store_gameplay_valid(save)) bytes->failed = 1;
}
static int maage_store_metadata_write(const maage_world_store *store,
    const maage_world_record *record, char error[256])
{
    unsigned char data[MAAGE_STORE_METADATA_MAX];
    maage_store_bytes bytes;
    char path[MAAGE_PATH_CAPACITY];
    unsigned int i;
    bytes.data = data; bytes.length = sizeof(data); bytes.position = 0; bytes.failed = 0;
    maage_store_text_put(&bytes, "MGWORLD1", 8);
    maage_store_put(&bytes, MAAGE_PLAYER_FORMAT_VERSION, 4);
    maage_store_put(&bytes, MAAGE_WORLD_GENERATOR_VERSION, 4);
    maage_store_put(&bytes, MAAGE_WORLD_BLOCKSET_VERSION, 4);
    maage_store_put(&bytes, record->seed.low, 4); maage_store_put(&bytes, record->seed.high, 4);
    maage_store_put(&bytes, strlen(record->id), 1); maage_store_put(&bytes, strlen(record->name), 1);
    maage_store_put(&bytes, strlen(record->created_utc), 1); maage_store_put(&bytes, record->new_world ? 1 : 0, 1);
    maage_store_text_put(&bytes, record->id, strlen(record->id));
    maage_store_text_put(&bytes, record->name, strlen(record->name));
    maage_store_text_put(&bytes, record->created_utc, strlen(record->created_utc));
    for (i = 0; i < 3; ++i) maage_store_double_put(&bytes, record->position[i]);
    maage_store_double_put(&bytes, record->yaw); maage_store_double_put(&bytes, record->pitch);
    maage_store_gameplay_put(&bytes, &record->gameplay);
    maage_store_checksum_put(&bytes);
    if (bytes.failed || !maage_storage_join(path, sizeof(path), store->directory, "world.bin"))
        return maage_store_error(error, "Could not encode world metadata.");
    return maage_store_write(path, data, bytes.position, error);
}
static int maage_store_metadata_load(const maage_world_store *store,
    const char *expected_id, maage_world_record *record, char error[256])
{
    char path[MAAGE_PATH_CAPACITY];
    maage_store_bytes bytes;
    unsigned long lengths[3], format = 0;
    size_t i, j;
    char *texts[3];
    int ok;
    if (!maage_storage_join(path, sizeof(path), store->directory, "world.bin")) return maage_store_error(error, "World metadata path is too long.");
    bytes.data = maage_store_read(path, MAAGE_STORE_METADATA_MAX, &bytes.length, error);
    if (!bytes.data) return 0;
    bytes.position = 0; bytes.failed = 0;
    ok = maage_store_checksum(&bytes) && maage_store_magic(&bytes, "MGWORLD1");
    if (ok) format = maage_store_get(&bytes, 4);
    ok = ok && (format == 1 || format == MAAGE_PLAYER_FORMAT_VERSION) &&
        maage_store_get(&bytes, 4) == MAAGE_WORLD_GENERATOR_VERSION &&
        maage_store_get(&bytes, 4) == MAAGE_WORLD_BLOCKSET_VERSION;
    if (ok) {
        record->seed.low = maage_store_get(&bytes, 4); record->seed.high = maage_store_get(&bytes, 4);
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
        else maage_store_gameplay_get(&bytes, &record->gameplay);
        ok = ok && !bytes.failed && bytes.position == bytes.length && maage_store_id(record->id) &&
            !strcmp(record->id, expected_id) && maage_store_name(record->name) && maage_store_player(record);
        for (i = 0; i < 20; ++i) {
            char expected = (i == 4 || i == 7) ? '-' : (i == 10 ? 'T' : ((i == 13 || i == 16) ? ':' : (i == 19 ? 'Z' : 0)));
            if (expected ? record->created_utc[i] != expected : (record->created_utc[i] < '0' || record->created_utc[i] > '9')) ok = 0;
        }
    }
    free(bytes.data);
    return ok ? 1 : maage_store_error(error, "World metadata is corrupt or uses an unsupported format, generator, or block mapping.");
}
static int maage_store_random_id(char id[65])
{
    unsigned char bytes[16];
    const char *digits = "0123456789abcdef";
    size_t i;
    if (!maage_storage_random(bytes, sizeof(bytes))) return 0;
    for (i = 0; i < sizeof(bytes); ++i) { id[i * 2] = digits[bytes[i] >> 4]; id[i * 2 + 1] = digits[bytes[i] & 15]; }
    id[32] = 0; return 1;
}
int maage_world_store_open(const char *data_root, const char *id, int create,
    const char *name, const maage_world_seed *seed, maage_world_store **output,
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
    if ((id && !maage_store_id(id)) || (name && !maage_store_name(name))) return maage_store_error(error, "Invalid world ID or name.");
    if (!maage_storage_join(worlds, sizeof(worlds), data_root, "worlds") || !maage_storage_directories(worlds) ||
        !maage_storage_join(config, sizeof(config), data_root, "config") || !maage_storage_directories(config) ||
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
    if (create) {
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
int maage_world_store_save_player(maage_world_store *store,
    const double position[3], double yaw, double pitch, char error[256])
{
    maage_world_record record = store->record, existing;
    /* Recheck the live metadata before replacement, just as chunk replacement
     * rechecks its old file. Corruption never becomes a successful overwrite. */
    if (!maage_store_metadata_load(store, record.id, &existing, error)) return 0;
    if (existing.seed.high != record.seed.high || existing.seed.low != record.seed.low)
        return maage_store_error(error, "World metadata changed its seed while open; the save was preserved.");
    memcpy(record.position, position, sizeof(record.position));
    record.gameplay = existing.gameplay;
    record.yaw = yaw; record.pitch = pitch; record.new_world = 0;
    if (!maage_store_player(&record)) return maage_store_error(error, "Player state is outside the world format's supported bounds.");
    /* Store identity stays immutable while independent chunk jobs borrow it. */
    return maage_store_metadata_write(store, &record, error);
}
int maage_world_store_save_gameplay(maage_world_store *store,
    const double position[3], double yaw, double pitch,
    const maage_gameplay *game, const maage_item_registry *items, char error[256])
{
    maage_world_record record, existing;
    maage_gameplay restored;
    if (!store || !position || !game || !items) return maage_store_error(error, "Missing player gameplay snapshot.");
    record = store->record;
    if (!maage_store_metadata_load(store, record.id, &existing, error)) return 0;
    if (existing.seed.high != record.seed.high || existing.seed.low != record.seed.low)
        return maage_store_error(error, "World metadata changed its seed while open; the save was preserved.");
    memcpy(record.position, position, sizeof(record.position)); record.yaw = yaw; record.pitch = pitch;
    record.new_world = 0; maage_gameplay_snapshot(game, &record.gameplay);
    if (!maage_store_player(&record) || !maage_store_gameplay_valid(&record.gameplay) ||
        !maage_gameplay_restore(&restored, items, &record.gameplay))
        return maage_store_error(error, "Player gameplay snapshot is invalid or uses an unmapped item identity.");
    return maage_store_metadata_write(store, &record, error);
}
void maage_world_store_close(maage_world_store *store)
{
    if (!store) return;
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
static int maage_store_state(const maage_block_registry *blocks, unsigned long id)
{
    const maage_block_state_definition *state = maage_block_registry_state(blocks, id);
    if (!state) return 0;
    if (id >= 256 && id < 262) return state->block_id == 2 && state->metadata == id - 255;
    switch (id) {
        case 0: case 1: case 2: case 3: case 7: case 9: case 12: case 13: case 24:
            return state->block_id == id && state->metadata == 0;
        default: return 0;
    }
}
static int maage_store_chunk_decode(unsigned char *data, size_t length, int x, int z,
    const maage_block_registry *blocks, const maage_biome_registry *biomes, maage_chunk **output)
{
    maage_store_bytes bytes;
    maage_chunk *chunk;
    unsigned long mask, stored_x, stored_z, expected_x, expected_z, value;
    size_t i, section, expected_length = 800;
    bytes.data = data; bytes.length = length; bytes.position = 0; bytes.failed = 0;
    if (!maage_store_checksum(&bytes) || !maage_store_magic(&bytes, "MGCHNK1\0") ||
        maage_store_get(&bytes, 4) != MAAGE_WORLD_FORMAT_VERSION ||
        maage_store_get(&bytes, 4) != MAAGE_WORLD_GENERATOR_VERSION ||
        maage_store_get(&bytes, 4) != MAAGE_WORLD_BLOCKSET_VERSION) return 0;
    stored_x = maage_store_get(&bytes, 4); stored_z = maage_store_get(&bytes, 4);
    expected_x = (unsigned long)x & MAAGE_STORE_WORD; expected_z = (unsigned long)z & MAAGE_STORE_WORD;
    if (stored_x != expected_x || stored_z != expected_z) return 0;
    mask = maage_store_get(&bytes, 2);
    if (maage_store_get(&bytes, 2) != 0) return 0;
    for (section = 0; section < 16; ++section)
        if (mask & (1UL << section)) expected_length += MAAGE_SECTION_VOLUME * 3;
    if (bytes.failed || bytes.length != expected_length) return 0;
    chunk = maage_chunk_create(x, z);
    if (!chunk) return 0;
    for (i = 0; i < 256; ++i) {
        value = maage_store_get(&bytes, 1);
        if (value >= MAAGE_BIOME_COUNT || !maage_biome_registry_get(biomes, (maage_biome_id)value)) goto failed;
        chunk->biomes[i] = (maage_biome_id)value;
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
            if (!maage_store_state(blocks, value)) goto failed;
            chunk->sections[section]->states[i] = value;
        }
        for (i = 0; i < MAAGE_SECTION_VOLUME; ++i)
            chunk->sections[section]->light[i] = (unsigned char)maage_store_get(&bytes, 1);
    }
    if (bytes.failed || bytes.position != bytes.length) goto failed;
    *output = chunk; return 1;
failed:
    maage_chunk_destroy(chunk); return 0;
}
int maage_world_store_load_chunk(maage_world_store *store, int x, int z,
    const maage_block_registry *blocks, const maage_biome_registry *biomes,
    maage_chunk **chunk, char error[256])
{
    char path[MAAGE_PATH_CAPACITY];
    unsigned char *data;
    size_t length;
    int ok, status;
    *chunk = NULL;
    if (!maage_store_chunk_path(store, x, z, path)) { maage_store_error(error, "Chunk coordinates or path exceed the world format bounds."); return -1; }
    status = maage_storage_status(path);
    if (!status) return 0;
    data = maage_store_read(path, MAAGE_STORE_CHUNK_MAX, &length, error);
    if (!data) return -1;
    ok = maage_store_chunk_decode(data, length, x, z, blocks, biomes, chunk); free(data);
    if (!ok) { maage_store_error(error, "Chunk data is corrupt, unmapped, or incompatible; the save was preserved."); return -1; }
    return 1;
}
int maage_world_store_save_chunk(maage_world_store *store, const maage_chunk *chunk,
    const maage_block_registry *blocks, const maage_biome_registry *biomes, char error[256])
{
    char path[MAAGE_PATH_CAPACITY];
    maage_store_bytes bytes;
    maage_chunk *existing = NULL;
    unsigned long mask = 0;
    size_t i, section;
    int result;
    if (!maage_store_chunk_path(store, chunk->x, chunk->z, path)) return maage_store_error(error, "Chunk coordinates or path exceed the world format bounds.");
    result = maage_world_store_load_chunk(store, chunk->x, chunk->z, blocks, biomes, &existing, error);
    if (result < 0) return 0;
    maage_chunk_destroy(existing);
    bytes.data = (unsigned char *)malloc(MAAGE_STORE_CHUNK_MAX);
    if (!bytes.data) return maage_store_error(error, "Not enough memory to encode a chunk.");
    bytes.length = MAAGE_STORE_CHUNK_MAX; bytes.position = 0; bytes.failed = 0;
    maage_store_text_put(&bytes, "MGCHNK1\0", 8);
    maage_store_put(&bytes, MAAGE_WORLD_FORMAT_VERSION, 4);
    maage_store_put(&bytes, MAAGE_WORLD_GENERATOR_VERSION, 4);
    maage_store_put(&bytes, MAAGE_WORLD_BLOCKSET_VERSION, 4);
    maage_store_put(&bytes, (unsigned long)chunk->x, 4); maage_store_put(&bytes, (unsigned long)chunk->z, 4);
    for (section = 0; section < 16; ++section) if (chunk->sections[section]) mask |= 1UL << section;
    maage_store_put(&bytes, mask, 2); maage_store_put(&bytes, 0, 2);
    for (i = 0; i < 256; ++i) {
        if (chunk->biomes[i] >= MAAGE_BIOME_COUNT || !maage_biome_registry_get(biomes, chunk->biomes[i])) goto incompatible;
        maage_store_put(&bytes, chunk->biomes[i], 1);
    }
    for (i = 0; i < 256; ++i) {
        if (chunk->heightmap[i] > MAAGE_WORLD_HEIGHT) goto incompatible;
        maage_store_put(&bytes, chunk->heightmap[i], 2);
    }
    for (section = 0; section < 16; ++section) {
        if (!chunk->sections[section]) continue;
        for (i = 0; i < MAAGE_SECTION_VOLUME; ++i) {
            if (!maage_store_state(blocks, chunk->sections[section]->states[i])) goto incompatible;
            maage_store_put(&bytes, chunk->sections[section]->states[i], 2);
        }
        for (i = 0; i < MAAGE_SECTION_VOLUME; ++i) maage_store_put(&bytes, chunk->sections[section]->light[i], 1);
    }
    maage_store_checksum_put(&bytes);
    result = !bytes.failed && maage_store_write(path, bytes.data, bytes.position, error);
    free(bytes.data);
    return result ? 1 : maage_store_error(error, "Chunk save failed; the previous file was preserved.");
incompatible:
    free(bytes.data); return maage_store_error(error, "Chunk uses invalid heights or IDs outside the saved world's versioned mapping.");
}
