#include "registry_policy.h"
#include "maage/dimensions.h"
#include <stdlib.h>
#include <float.h>
#include <string.h>
struct maage_dimension_registry {
    maage_dimension_definition **entries;
    size_t count, capacity, bucket_count;
    maage_dimension_id *buckets, primary;
    content_policy *policy;
    int finalized;
};
static maage_status dimension_status(unsigned char code, unsigned char operation) {
    return maage_status_make(code, MAAGE_CONTEXT_REGISTRY, operation, 0);
}
static unsigned long dimension_hash(const char *name) {
    unsigned long hash = 2166136261UL;
    while (*name)
        hash = ((hash ^ (unsigned char) *name++) * 16777619UL) & 4294967295UL;
    return hash;
}
static size_t dimension_bucket(const maage_dimension_registry *registry, const char *name) {
    size_t bucket = dimension_hash(name) % registry->bucket_count;
    while (registry->buckets[bucket] && strcmp(registry->entries[registry->buckets[bucket] - 1]->name, name))
        bucket = (bucket + 1) % registry->bucket_count;
    return bucket;
}
void content_dimensions_policy(maage_dimension_registry *registry, content_policy *policy) {
    registry->policy = policy;
}
maage_status maage_dimension_registry_create(maage_dimension_registry **output) {
    if (!output) return dimension_status(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_OPERATION_CREATE);
    *output = (maage_dimension_registry *) calloc(1, sizeof(**output));
    return dimension_status(*output ? MAAGE_STATUS_SUCCESS : MAAGE_STATUS_ALLOCATION_FAILED, MAAGE_OPERATION_CREATE);
}
maage_status maage_dimension_registry_destroy(maage_dimension_registry *registry) {
    size_t i;
    if (!registry) return dimension_status(MAAGE_STATUS_NOOP, MAAGE_OPERATION_DESTROY);
    for (i = 0; i < registry->count; ++i) {
        free((void *) registry->entries[i]->name);
        free((void *) registry->entries[i]->display_name);
        free(registry->entries[i]);
    }
    free(registry->entries);
    free(registry->buckets);
    free(registry);
    return dimension_status(MAAGE_STATUS_SUCCESS, MAAGE_OPERATION_DESTROY);
}
maage_status maage_dimension_register(maage_dimension_registry *registry, const char *name, maage_dimension_definition *definition) {
    maage_dimension_definition *copy, **entries;
    maage_dimension_id *buckets;
    size_t i, capacity, bucket;
    const char *label, *cursor;
    int colon = 0;
    maage_status result;
    if (!registry || !definition || !name || !*name || strlen(name) > 255 || !strchr(name, ':') || !definition->generate || !definition->spawn || !(definition->coordinate_scale > 0 && definition->coordinate_scale <= DBL_MAX) || !(definition->ambient_light >= 0 && definition->ambient_light <= 1) || !(definition->fog_distance >= 0 && definition->fog_distance <= MAAGE_WORLD_LIMIT)) return dimension_status(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_OPERATION_REGISTER);
    if ((definition->display_name && strlen(definition->display_name) > 127) || (definition->primary != 0 && definition->primary != 1) || (definition->skylight != 0 && definition->skylight != 1) || (definition->weather != 0 && definition->weather != 1) || (definition->celestial != 0 && definition->celestial != 1) || (definition->water_evaporates != 0 && definition->water_evaporates != 1)) return dimension_status(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_OPERATION_REGISTER);
    for (cursor = name; *cursor; ++cursor) {
        if (*cursor == ':') {
            if (colon || cursor == name || !cursor[1]) return dimension_status(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_OPERATION_REGISTER);
            colon = 1;
        } else if (!((*cursor >= 'a' && *cursor <= 'z') || (*cursor >= '0' && *cursor <= '9') || *cursor == '_' || *cursor == '-' || *cursor == '.' || (colon && *cursor == '/'))) return dimension_status(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_OPERATION_REGISTER);
    }
    for (i = 0; i < 3; ++i)
        if (!(definition->fog[i] >= 0 && definition->fog[i] <= 1)) return dimension_status(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_OPERATION_REGISTER);
    if (registry->finalized || registry->count >= MAAGE_CONTENT_ID_INVALID - 1 || (definition->primary && registry->primary)) return dimension_status(MAAGE_STATUS_INVALID_STATE, MAAGE_OPERATION_REGISTER);
    result = content_policy_register(registry->policy, CONTENT_DIMENSION, name, (unsigned long) registry->count + 1);
    if (maage_status_failed(result)) return result;
    if (registry->bucket_count && registry->buckets[dimension_bucket(registry, name)]) return dimension_status(MAAGE_STATUS_INVALID_STATE, MAAGE_OPERATION_REGISTER);
    if (registry->count == registry->capacity) {
        if (registry->capacity > ((size_t) - 1 / sizeof(*entries) - 16) / 2) return dimension_status(MAAGE_STATUS_BUFFER_OVERFLOW, MAAGE_OPERATION_REGISTER);
        capacity = registry->capacity * 2 + 16;
        entries = (maage_dimension_definition **) realloc(registry->entries, capacity * sizeof(*entries));
        if (!entries) return dimension_status(MAAGE_STATUS_ALLOCATION_FAILED, MAAGE_OPERATION_REGISTER);
        registry->entries = entries;
        registry->capacity = capacity;
    }
    if (!registry->bucket_count || registry->count + 1 >= registry->bucket_count / 2) {
        if (registry->bucket_count > ((size_t) - 1 / sizeof(*buckets) - 33) / 2) return dimension_status(MAAGE_STATUS_BUFFER_OVERFLOW, MAAGE_OPERATION_REGISTER);
        capacity = registry->bucket_count * 2 + 33;
        buckets = (maage_dimension_id *) calloc(capacity, sizeof(*buckets));
        if (!buckets) return dimension_status(MAAGE_STATUS_ALLOCATION_FAILED, MAAGE_OPERATION_REGISTER);
        free(registry->buckets);
        registry->buckets = buckets;
        registry->bucket_count = capacity;
        for (i = 0; i < registry->count; ++i)
            registry->buckets[dimension_bucket(registry, registry->entries[i]->name)] = (maage_dimension_id) i + 1;
    }
    copy = (maage_dimension_definition *) malloc(sizeof(*copy));
    if (!copy) return dimension_status(MAAGE_STATUS_ALLOCATION_FAILED, MAAGE_OPERATION_REGISTER);
    *copy = *definition;
    label = definition->display_name ? definition->display_name : name;
    copy->name = (char *) malloc(strlen(name) + 1);
    copy->display_name = (char *) malloc(strlen(label) + 1);
    if (!copy->name || !copy->display_name) {
        free((void *) copy->name);
        free((void *) copy->display_name);
        free(copy);
        return dimension_status(MAAGE_STATUS_ALLOCATION_FAILED, MAAGE_OPERATION_REGISTER);
    }
    strcpy((char *) copy->name, name);
    strcpy((char *) copy->display_name, label);
    copy->id = (maage_dimension_id) registry->count + 1;
    bucket = dimension_bucket(registry, name);
    registry->entries[registry->count++] = copy;
    registry->buckets[bucket] = copy->id;
    if (copy->primary) registry->primary = copy->id;
    definition->id = copy->id;
    return dimension_status(MAAGE_STATUS_SUCCESS, MAAGE_OPERATION_REGISTER);
}
maage_status maage_dimension_finalize(maage_dimension_registry *registry) {
    if (!registry || !registry->primary) return dimension_status(MAAGE_STATUS_INVALID_STATE, MAAGE_OPERATION_FINALIZE);
    {
        size_t i;
        for (i = 0; i < registry->count; ++i)
            if (registry->entries[i]->respawn && !maage_dimension_get(registry, registry->entries[i]->respawn)) return dimension_status(MAAGE_STATUS_NOT_FOUND, MAAGE_OPERATION_FINALIZE);
    }
    registry->finalized = 1;
    return dimension_status(MAAGE_STATUS_SUCCESS, MAAGE_OPERATION_FINALIZE);
}
size_t maage_dimension_count(const maage_dimension_registry *registry) {
    return registry ? registry->count : 0;
}
const maage_dimension_definition *maage_dimension_get(const maage_dimension_registry *registry, maage_dimension_id id) {
    return registry && id && id <= registry->count ? registry->entries[id - 1] : NULL;
}
maage_dimension_id maage_dimension_primary(const maage_dimension_registry *registry) {
    return registry ? registry->primary : 0;
}
maage_status maage_dimension_find(const maage_dimension_registry *registry, const char *name, maage_dimension_id *id) {
    maage_status result;
    if (!registry || !name || !id) return dimension_status(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_OPERATION_RESOLVE);
    result = content_policy_read(registry->policy, CONTENT_DIMENSION, name);
    if (maage_status_failed(result)) return result;
    *id = registry->bucket_count ? registry->buckets[dimension_bucket(registry, name)] : 0;
    return dimension_status(*id ? MAAGE_STATUS_SUCCESS : MAAGE_STATUS_NOT_FOUND, MAAGE_OPERATION_RESOLVE);
}
