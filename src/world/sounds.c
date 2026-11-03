#include "registry_policy.h"
#include "maage/sounds.h"
#include "maage/internal/registry_pages.h"
#include <stdlib.h>
#include <string.h>
#include <float.h>

typedef struct sound_record {
    char *name;
    void *definition, *owned;
} sound_record;
typedef struct sound_store {
    maage_registry_pages records;
    sound_record **names;
    size_t count, buckets;
} sound_store;
struct maage_sound_registry {
    content_policy *policy;
    sound_store resources, events, materials, music, ambient;
    maage_sound_bindings bindings;
    int finalized;
};
typedef struct ambient_record {
    unsigned long id;
    maage_sound_ambient_definition definition;
} ambient_record;
static maage_status sound_status(unsigned char code, unsigned char operation) {
    return maage_status_make(code, code < MAAGE_STATUS_WARNING ? 0 : MAAGE_CONTEXT_REGISTRY, operation, 0);
}
static unsigned long sound_hash(const char *name) {
    unsigned long hash = 2166136261UL;
    while (*name)
        hash = (hash ^ (unsigned char) *name++) * 16777619UL;
    return hash;
}
static int sound_name_valid(const char *name) {
    const char *begin = name;
    int colon = 0;
    if (!name || !*name) return 0;
    while (*name) {
        if (*name == ':') {
            if (colon || name == begin || !name[1]) return 0;
            colon = 1;
        } else if (!((*name >= 'a' && *name <= 'z') || (*name >= '0' && *name <= '9') || *name == '_' || *name == '-' || *name == '.' || (colon && *name == '/'))) return 0;
        ++name;
    }
    return colon;
}
static size_t sound_bucket(const sound_store *store, const char *name) {
    size_t index = sound_hash(name) & (store->buckets - 1);
    while (store->names[index] && strcmp(store->names[index]->name, name))
        index = (index + 1) & (store->buckets - 1);
    return index;
}
static const sound_record *sound_find(const sound_store *store, unsigned long id) {
    void **slot = store ? maage_registry_pages_find(&store->records, id) : NULL;
    return slot ? (const sound_record *) *slot : NULL;
}
static const void *sound_definition(const sound_store *store, unsigned long id) {
    const sound_record *record = sound_find(store, id);
    return record ? record->definition : NULL;
}
static void sound_record_free(void *value) {
    sound_record *record = (sound_record *) value;
    free(record->owned);
    free(record->definition);
    free(record->name);
    free(record);
}
static void sound_store_free(sound_store *store) {
    maage_registry_pages_destroy(&store->records, sound_record_free);
    free(store->names);
}
/* Transfer owned bytes only after the record has been fully published. */
static maage_status sound_store_add(sound_store *store, const char *name, const void *definition, size_t bytes, void *owned, unsigned long *id) {
    sound_record *record, **names;
    void **slot;
    size_t buckets, index, bucket, length;
    if (!sound_name_valid(name) || !definition || !id) return sound_status(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_OPERATION_REGISTER);
    if (store->buckets && store->names[sound_bucket(store, name)]) return sound_status(MAAGE_STATUS_INVALID_STATE, MAAGE_OPERATION_REGISTER);
    if (store->count >= MAAGE_CONTENT_ID_INVALID - 1UL) return sound_status(MAAGE_STATUS_BUFFER_OVERFLOW, MAAGE_OPERATION_REGISTER);
    if (!store->buckets || store->count >= store->buckets / 2) {
        if (store->buckets > (size_t) - 1 / (2 * sizeof(*names))) return sound_status(MAAGE_STATUS_BUFFER_OVERFLOW, MAAGE_OPERATION_REGISTER);
        buckets = store->buckets ? store->buckets * 2 : 16;
        names = (sound_record **) calloc(buckets, sizeof(*names));
        if (!names) return sound_status(MAAGE_STATUS_ALLOCATION_FAILED, MAAGE_OPERATION_REGISTER);
        for (index = 0; index < store->buckets; ++index) {
            if (!store->names[index]) continue;
            bucket = sound_hash(store->names[index]->name) & (buckets - 1);
            while (names[bucket])
                bucket = (bucket + 1) & (buckets - 1);
            names[bucket] = store->names[index];
        }
        free(store->names);
        store->names = names;
        store->buckets = buckets;
    }
    length = strlen(name);
    if (length == (size_t) - 1) return sound_status(MAAGE_STATUS_BUFFER_OVERFLOW, MAAGE_OPERATION_REGISTER);
    record = (sound_record *) calloc(1, sizeof(*record));
    if (!record) return sound_status(MAAGE_STATUS_ALLOCATION_FAILED, MAAGE_OPERATION_REGISTER);
    record->name = (char *) malloc(length + 1);
    record->definition = malloc(bytes);
    if (!record->name || !record->definition) {
        sound_record_free(record);
        return sound_status(MAAGE_STATUS_ALLOCATION_FAILED, MAAGE_OPERATION_REGISTER);
    }
    strcpy(record->name, name);
    memcpy(record->definition, definition, bytes);
    *id = (unsigned long) store->count + 1;
    slot = maage_registry_pages_ensure(&store->records, *id);
    if (!slot) {
        sound_record_free(record);
        return sound_status(MAAGE_STATUS_ALLOCATION_FAILED, MAAGE_OPERATION_REGISTER);
    }
    record->owned = owned;
    *slot = record;
    store->names[sound_bucket(store, name)] = record;
    ++store->count;
    return sound_status(MAAGE_STATUS_SUCCESS, MAAGE_OPERATION_REGISTER);
}
static maage_status sound_store_resolve(const sound_store *store, const char *name, unsigned long *id) {
    const sound_record *record;
    unsigned long index;
    if (id) *id = MAAGE_CONTENT_ID_INVALID;
    if (!store || !name || !id) return sound_status(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_OPERATION_RESOLVE);
    if (!store->buckets) return sound_status(MAAGE_STATUS_NOT_FOUND, MAAGE_OPERATION_RESOLVE);
    record = store->names[sound_bucket(store, name)];
    if (!record) return sound_status(MAAGE_STATUS_NOT_FOUND, MAAGE_OPERATION_RESOLVE);
    /* Every named descriptor begins with its allocated numeric ID. */
    memcpy(&index, record->definition, sizeof(index));
    *id = index;
    return sound_status(MAAGE_STATUS_SUCCESS, MAAGE_OPERATION_RESOLVE);
}
static maage_status sound_resources_copy(const maage_sound_resource_id *resources, size_t count, maage_sound_resource_id **copy) {
    *copy = NULL;
    if (!count) return sound_status(MAAGE_STATUS_SUCCESS, MAAGE_OPERATION_REGISTER);
    if (!resources || count > (size_t) - 1 / sizeof(**copy)) return sound_status(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_OPERATION_REGISTER);
    *copy = (maage_sound_resource_id *) malloc(count * sizeof(**copy));
    if (!*copy) return sound_status(MAAGE_STATUS_ALLOCATION_FAILED, MAAGE_OPERATION_REGISTER);
    memcpy(*copy, resources, count * sizeof(**copy));
    return sound_status(MAAGE_STATUS_SUCCESS, MAAGE_OPERATION_REGISTER);
}
maage_status maage_sound_registry_create(maage_sound_registry **output) {
    if (!output) return sound_status(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_OPERATION_CREATE);
    *output = (maage_sound_registry *) calloc(1, sizeof(**output));
    return sound_status(*output ? MAAGE_STATUS_SUCCESS : MAAGE_STATUS_ALLOCATION_FAILED, MAAGE_OPERATION_CREATE);
}
maage_status maage_sound_registry_destroy(maage_sound_registry *registry) {
    if (!registry) return sound_status(MAAGE_STATUS_NOOP, MAAGE_OPERATION_DESTROY);
    sound_store_free(&registry->resources);
    sound_store_free(&registry->events);
    sound_store_free(&registry->materials);
    sound_store_free(&registry->music);
    sound_store_free(&registry->ambient);
    free(registry);
    return sound_status(MAAGE_STATUS_SUCCESS, MAAGE_OPERATION_DESTROY);
}
maage_status maage_sound_resource_register(maage_sound_registry *registry, const char *name, maage_sound_resource_definition *definition) {
    maage_status policy_result;
    maage_sound_resource_definition registered;
    char *path;
    size_t length;
    maage_status result;
    if (registry) {
        policy_result = content_policy_register(registry->policy, CONTENT_SOUND_RESOURCE, name, (unsigned long) (registry->resources.count + 1));
        if (maage_status_failed(policy_result)) return policy_result;
    }
    if (!registry || registry->finalized || !definition || !definition->path || !*definition->path || *definition->path == '/' || strchr(definition->path, '\\') || strchr(definition->path, ':') || strstr(definition->path, "..") || (definition->streamed != 0 && definition->streamed != 1)) return sound_status(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_OPERATION_REGISTER);
    length = strlen(definition->path);
    if (length == (size_t) - 1) return sound_status(MAAGE_STATUS_BUFFER_OVERFLOW, MAAGE_OPERATION_REGISTER);
    path = (char *) malloc(length + 1);
    if (!path) return sound_status(MAAGE_STATUS_ALLOCATION_FAILED, MAAGE_OPERATION_REGISTER);
    strcpy(path, definition->path);
    registered = *definition;
    registered.id = (unsigned long) registry->resources.count + 1;
    registered.path = path;
    result = sound_store_add(&registry->resources, name, &registered, sizeof(registered), path, &definition->id);
    if (maage_status_failed(result)) free(path);
    return result;
}
maage_status maage_sound_register(maage_sound_registry *registry, const char *name, maage_sound_definition *definition) {
    maage_status policy_result;
    maage_sound_definition registered;
    maage_sound_resource_id *resources;
    maage_status result;
    if (registry) {
        policy_result = content_policy_register(registry->policy, CONTENT_SOUND, name, (unsigned long) (registry->events.count + 1));
        if (maage_status_failed(policy_result)) return policy_result;
    }
    if (!registry || registry->finalized || !definition || definition->bus >= MAAGE_SOUND_BUS_COUNT || (definition->positional != 0 && definition->positional != 1) || !(definition->gain >= 0 && definition->gain <= FLT_MAX / 16 && definition->pitch > 0 && definition->pitch <= FLT_MAX / 4 && definition->distance > 0 && definition->distance <= FLT_MAX / 16)) return sound_status(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_OPERATION_REGISTER);
    result = sound_resources_copy(definition->resources, definition->resource_count, &resources);
    if (maage_status_failed(result)) return result;
    registered = *definition;
    registered.id = (unsigned long) registry->events.count + 1;
    registered.resources = resources;
    result = sound_store_add(&registry->events, name, &registered, sizeof(registered), resources, &definition->id);
    if (maage_status_failed(result)) free(resources);
    return result;
}
maage_status maage_sound_material_register(maage_sound_registry *registry, const char *name, maage_sound_material_definition *definition) {
    maage_status policy_result;
    maage_sound_material_definition registered;
    if (registry) {
        policy_result = content_policy_register(registry->policy, CONTENT_MATERIAL, name, (unsigned long) (registry->materials.count + 1));
        if (maage_status_failed(policy_result)) return policy_result;
    }
    if (!registry || registry->finalized || !definition) return sound_status(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_OPERATION_REGISTER);
    registered = *definition;
    registered.id = (unsigned long) registry->materials.count + 1;
    return sound_store_add(&registry->materials, name, &registered, sizeof(registered), NULL, &definition->id);
}
maage_status maage_sound_music_register(maage_sound_registry *registry, const char *name, maage_sound_music_definition *definition) {
    maage_status policy_result;
    maage_sound_music_definition registered;
    maage_sound_resource_id *resources;
    maage_status result;
    if (registry) {
        policy_result = content_policy_register(registry->policy, CONTENT_MUSIC, name, (unsigned long) (registry->music.count + 1));
        if (maage_status_failed(policy_result)) return policy_result;
    }
    if (!registry || registry->finalized || !definition || !definition->resource_count || !(definition->minimum_delay >= 0 && definition->minimum_delay <= DBL_MAX && definition->maximum_delay >= definition->minimum_delay && definition->maximum_delay <= DBL_MAX / 2 && definition->initial_delay >= 0 && definition->initial_delay <= DBL_MAX / 2 && definition->retry_delay > 0 && definition->retry_delay <= DBL_MAX / 2 && definition->unavailable_delay > 0 && definition->unavailable_delay <= DBL_MAX / 2)) return sound_status(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_OPERATION_REGISTER);
    result = sound_resources_copy(definition->resources, definition->resource_count, &resources);
    if (maage_status_failed(result)) return result;
    registered = *definition;
    registered.id = (unsigned long) registry->music.count + 1;
    registered.resources = resources;
    result = sound_store_add(&registry->music, name, &registered, sizeof(registered), resources, &definition->id);
    if (maage_status_failed(result)) free(resources);
    return result;
}
maage_status maage_sound_ambient_register(maage_sound_registry *registry, const char *name, const maage_sound_ambient_definition *definition) {
    maage_status policy_result;
    /* Ambient callbacks have no runtime numeric identity requirement; retain
     * one private ID before their copied descriptor for boundary diagnostics. */
    ambient_record registered;
    unsigned long id;
    if (registry) {
        policy_result = content_policy_register(registry->policy, CONTENT_AMBIENT, name, (unsigned long) (registry->ambient.count + 1));
        if (maage_status_failed(policy_result)) return policy_result;
    }
    if (!registry || registry->finalized || !definition || !definition->update || !(definition->interval > 0 && definition->interval <= DBL_MAX / 2)) return sound_status(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_OPERATION_REGISTER);
    registered.id = (unsigned long) registry->ambient.count + 1;
    registered.definition = *definition;
    return sound_store_add(&registry->ambient, name, &registered, sizeof(registered), NULL, &id);
}
maage_status maage_sound_bind(maage_sound_registry *registry, const maage_sound_bindings *bindings) {
    maage_status policy_result;
    if (registry) {
        policy_result = content_policy_register(registry->policy, CONTENT_SOUND_BINDINGS, "bindings", 0);
        if (maage_status_failed(policy_result)) return policy_result;
    }
    if (!registry || registry->finalized || !bindings) return sound_status(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_OPERATION_REGISTER);
    registry->bindings = *bindings;
    return sound_status(MAAGE_STATUS_SUCCESS, MAAGE_OPERATION_REGISTER);
}
maage_status maage_sound_finalize(maage_sound_registry *registry) {
    maage_status policy_result;
    const maage_sound_definition *event;
    const maage_sound_music_definition *music;
    const maage_sound_material_definition *material;
    const maage_sound_resource_definition *resource;
    size_t i, j;
    if (registry) {
        policy_result = content_policy_finalize(registry->policy);
        if (maage_status_failed(policy_result)) return policy_result;
    }
    if (!registry || registry->finalized) return sound_status(MAAGE_STATUS_INVALID_STATE, MAAGE_OPERATION_FINALIZE);
    for (i = 1; i <= registry->events.count; ++i) {
        event = maage_sound_get(registry, (maage_sound_id) i);
        for (j = 0; j < event->resource_count; ++j) {
            resource = maage_sound_resource_get(registry, event->resources[j]);
            if (!resource || resource->streamed) return sound_status(MAAGE_STATUS_NOT_FOUND, MAAGE_OPERATION_FINALIZE);
        }
    }
    for (i = 1; i <= registry->music.count; ++i) {
        music = maage_sound_music_get(registry, (maage_sound_music_id) i);
        for (j = 0; j < music->resource_count; ++j) {
            resource = maage_sound_resource_get(registry, music->resources[j]);
            if (!resource || !resource->streamed) return sound_status(MAAGE_STATUS_NOT_FOUND, MAAGE_OPERATION_FINALIZE);
        }
    }
    for (i = 1; i <= registry->materials.count; ++i) {
        material = maage_sound_material_get(registry, (maage_sound_material_id) i);
        for (j = 1; j < MAAGE_SOUND_ACTION_COUNT; ++j)
            if (material->actions[j] && !maage_sound_get(registry, material->actions[j])) return sound_status(MAAGE_STATUS_NOT_FOUND, MAAGE_OPERATION_FINALIZE);
    }
    for (j = 1; j < MAAGE_SOUND_ACTION_COUNT; ++j)
        if (registry->bindings.actions[j] && !maage_sound_get(registry, registry->bindings.actions[j])) return sound_status(MAAGE_STATUS_NOT_FOUND, MAAGE_OPERATION_FINALIZE);
    registry->finalized = 1;
    return sound_status(MAAGE_STATUS_SUCCESS, MAAGE_OPERATION_FINALIZE);
}
int maage_sound_is_finalized(const maage_sound_registry *registry) {
    return registry && registry->finalized;
}
size_t maage_sound_count(const maage_sound_registry *registry) {
    return registry ? registry->events.count : 0;
}
size_t maage_sound_resource_count(const maage_sound_registry *registry) {
    return registry ? registry->resources.count : 0;
}
size_t maage_sound_material_count(const maage_sound_registry *registry) {
    return registry ? registry->materials.count : 0;
}
size_t maage_sound_music_count(const maage_sound_registry *registry) {
    return registry ? registry->music.count : 0;
}
size_t maage_sound_ambient_count(const maage_sound_registry *registry) {
    return registry ? registry->ambient.count : 0;
}
const maage_sound_definition *maage_sound_get(const maage_sound_registry *registry, maage_sound_id id) {
    return registry ? (const maage_sound_definition *) sound_definition(&registry->events, id) : NULL;
}
const maage_sound_resource_definition *maage_sound_resource_get(const maage_sound_registry *registry, maage_sound_resource_id id) {
    return registry ? (const maage_sound_resource_definition *) sound_definition(&registry->resources, id) : NULL;
}
const maage_sound_material_definition *maage_sound_material_get(const maage_sound_registry *registry, maage_sound_material_id id) {
    return registry ? (const maage_sound_material_definition *) sound_definition(&registry->materials, id) : NULL;
}
const maage_sound_music_definition *maage_sound_music_get(const maage_sound_registry *registry, maage_sound_music_id id) {
    return registry ? (const maage_sound_music_definition *) sound_definition(&registry->music, id) : NULL;
}
const maage_sound_ambient_definition *maage_sound_ambient_at(const maage_sound_registry *registry, size_t index) {
    const ambient_record *record = registry && index < registry->ambient.count ? (const ambient_record *) sound_definition(&registry->ambient, (unsigned long) index + 1) : NULL;
    return record ? &record->definition : NULL;
}
const maage_sound_bindings *maage_sound_get_bindings(const maage_sound_registry *registry) {
    return registry ? &registry->bindings : NULL;
}
const char *maage_sound_name(const maage_sound_registry *registry, maage_sound_id id) {
    const sound_record *record = registry ? sound_find(&registry->events, id) : NULL;
    return record ? record->name : NULL;
}
maage_status maage_sound_resolve(const maage_sound_registry *registry, const char *name, maage_sound_id *id) {
    maage_status result;
    if (id) *id = MAAGE_CONTENT_ID_INVALID;
    if (registry) {
        result = content_policy_read(registry->policy, CONTENT_SOUND, name);
        if (maage_status_failed(result)) return result;
    }
    return sound_store_resolve(registry ? &registry->events : NULL, name, id);
}
maage_status maage_sound_resource_resolve(const maage_sound_registry *registry, const char *name, maage_sound_resource_id *id) {
    maage_status result;
    if (id) *id = MAAGE_CONTENT_ID_INVALID;
    if (registry) {
        result = content_policy_read(registry->policy, CONTENT_SOUND_RESOURCE, name);
        if (maage_status_failed(result)) return result;
    }
    return sound_store_resolve(registry ? &registry->resources : NULL, name, id);
}
maage_status maage_sound_material_resolve(const maage_sound_registry *registry, const char *name, maage_sound_material_id *id) {
    maage_status result;
    if (id) *id = MAAGE_CONTENT_ID_INVALID;
    if (registry) {
        result = content_policy_read(registry->policy, CONTENT_MATERIAL, name);
        if (maage_status_failed(result)) return result;
    }
    return sound_store_resolve(registry ? &registry->materials : NULL, name, id);
}
maage_status maage_sound_music_resolve(const maage_sound_registry *registry, const char *name, maage_sound_music_id *id) {
    maage_status result;
    if (id) *id = MAAGE_CONTENT_ID_INVALID;
    if (registry) {
        result = content_policy_read(registry->policy, CONTENT_MUSIC, name);
        if (maage_status_failed(result)) return result;
    }
    return sound_store_resolve(registry ? &registry->music : NULL, name, id);
}
maage_status maage_sound_select(const maage_sound_registry *registry, unsigned int action, maage_sound_material_id material, maage_sound_id *id) {
    const maage_sound_material_definition *definition;
    if (id) *id = 0;
    if (!registry || !id || !action || action >= MAAGE_SOUND_ACTION_COUNT) return sound_status(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_OPERATION_RESOLVE);
    definition = material ? maage_sound_material_get(registry, material) : NULL;
    if (material && !definition) return sound_status(MAAGE_STATUS_NOT_FOUND, MAAGE_OPERATION_RESOLVE);
    *id = definition ? definition->actions[action] : registry->bindings.actions[action];
    return sound_status(*id ? MAAGE_STATUS_SUCCESS : MAAGE_STATUS_NOOP, MAAGE_OPERATION_RESOLVE);
}

void content_sounds_policy(maage_sound_registry *registry, content_policy *policy) {
    registry->policy = policy;
}
