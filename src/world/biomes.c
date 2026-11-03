#include "registry_policy.h"
#include "maage/biome.h"
#include "maage/internal/registry_pages.h"
#include <stdlib.h>
#include <string.h>

typedef struct maage_biome_extension {
    maage_biome_definition definition;
    char *name, *display;
    void *payload;
    struct maage_biome_extension *next;
} maage_biome_extension;
static int maage_biome_name_valid(const char *name) {
    size_t i, length;
    int colon = 0;
    if (!name) return 0;
    length = strlen(name);
    if (length < 3 || length > 255) return 0;
    for (i = 0; i < length; ++i) {
        if (name[i] == ':') {
            if (colon || !i || i == length - 1) return 0;
            colon = 1;
        } else if (!((name[i] >= 'a' && name[i] <= 'z') ||
                     (name[i] >= '0' && name[i] <= '9') ||
                     name[i] == '_' || name[i] == '-' || name[i] == '.' ||
                     (colon && name[i] == '/')))
            return 0;
    }
    return colon;
}

static unsigned long biome_name_hash(const char *name) {
    unsigned long hash = 2166136261UL;
    while (*name) {
        hash ^= (unsigned char) *name++;
        hash = (hash * 16777619UL) & 4294967295UL;
    }
    return hash;
}
static size_t biome_name_bucket(const maage_biome_registry *registry, const char *name, int *found) {
    maage_biome_extension *node;
    size_t bucket = (size_t) (biome_name_hash(name) % registry->name_capacity);
    while ((node = (maage_biome_extension *) registry->name_buckets[bucket]) != NULL) {
        if (!strcmp(node->name, name)) {
            *found = 1;
            return bucket;
        }
        bucket = (bucket + 1) % registry->name_capacity;
    }
    *found = 0;
    return bucket;
}
static maage_status biome_name_grow(maage_biome_registry *registry) {
    void **buckets;
    maage_biome_extension *node;
    size_t capacity, bucket;
    if (registry->name_capacity && registry->count + 1 < registry->name_capacity / 2) return maage_status_make(MAAGE_STATUS_NOOP, 0, MAAGE_OPERATION_REGISTER, 0);
    if (registry->name_capacity > ((size_t) - 1 - 1) / 2) return maage_status_make(MAAGE_STATUS_BUFFER_OVERFLOW, MAAGE_CONTEXT_REGISTRY, MAAGE_OPERATION_REGISTER, 0);
    capacity = registry->name_capacity ? registry->name_capacity * 2 + 1 : 33;
    if (capacity > (size_t) - 1 / sizeof(*buckets)) return maage_status_make(MAAGE_STATUS_BUFFER_OVERFLOW, MAAGE_CONTEXT_REGISTRY, MAAGE_OPERATION_REGISTER, 0);
    buckets = (void **) calloc(capacity, sizeof(*buckets));
    if (!buckets) return maage_status_make(MAAGE_STATUS_ALLOCATION_FAILED, MAAGE_CONTEXT_REGISTRY, MAAGE_OPERATION_REGISTER, 0);
    for (node = (maage_biome_extension *) registry->extensions; node; node = node->next) {
        bucket = (size_t) (biome_name_hash(node->name) % capacity);
        while (buckets[bucket])
            bucket = (bucket + 1) % capacity;
        buckets[bucket] = node;
    }
    free(registry->name_buckets);
    registry->name_buckets = buckets;
    registry->name_capacity = capacity;
    return maage_status_make(MAAGE_STATUS_SUCCESS, 0, MAAGE_OPERATION_REGISTER, 0);
}
maage_status maage_biome_registry_init(maage_biome_registry *registry) {
    if (!registry) return maage_status_make(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_CONTEXT_REGISTRY, MAAGE_OPERATION_INITIALIZE, 0);
    memset(registry, 0, sizeof(*registry));
    return maage_status_make(MAAGE_STATUS_SUCCESS, 0, MAAGE_OPERATION_INITIALIZE, 0);
}
maage_status maage_biome_registry_destroy(maage_biome_registry *registry) {
    maage_biome_extension *node, *next;
    if (!registry) return maage_status_make(MAAGE_STATUS_NOOP, 0, MAAGE_OPERATION_DESTROY, 0);
    if (registry->view_source) {
        free(registry->view_definitions);
        memset(registry, 0, sizeof(*registry));
        return maage_status_make(MAAGE_STATUS_SUCCESS, 0, MAAGE_OPERATION_DESTROY, 0);
    }
    node = (maage_biome_extension *) registry->extensions;
    while (node) {
        next = node->next;
        free(node->name);
        free(node->payload);
        free(node->display);
        free(node);
        node = next;
    }
    if (registry->extension_pages) {
        maage_registry_pages_destroy((maage_registry_pages *) registry->extension_pages, NULL);
        free(registry->extension_pages);
    }
    free(registry->name_buckets);
    memset(registry, 0, sizeof(*registry));
    return maage_status_make(MAAGE_STATUS_SUCCESS, 0, MAAGE_OPERATION_DESTROY, 0);
}
maage_status maage_biome_registry_register(maage_biome_registry *registry, const char *name, maage_biome_definition *definition) {
    maage_status policy_result;
    maage_biome_extension *node;
    maage_status result;
    size_t bucket;
    int found;
    maage_registry_pages *pages;
    void **entry;
    size_t length;
    if (registry) {
        policy_result = content_policy_register((content_policy *) registry->ownership_policy, CONTENT_BIOME, name, (unsigned long) registry->count);
        if (maage_status_failed(policy_result)) return policy_result;
    }
    if (!registry || !definition || !maage_biome_name_valid(name) || (definition->display_name && strlen(definition->display_name) > 127))
        return maage_status_make(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_CONTEXT_REGISTRY, MAAGE_OPERATION_REGISTER, 0);
    if (registry->finalized) return maage_status_make(MAAGE_STATUS_INVALID_STATE, MAAGE_CONTEXT_REGISTRY, MAAGE_OPERATION_REGISTER, 0);
    if (registry->count >= 4294967295UL) return maage_status_make(MAAGE_STATUS_BUFFER_OVERFLOW, MAAGE_CONTEXT_REGISTRY, MAAGE_OPERATION_REGISTER, 0);
    definition->id = (unsigned long) registry->count;
    result = biome_name_grow(registry);
    if (maage_status_failed(result)) return result;
    bucket = biome_name_bucket(registry, name, &found);
    if (found) return maage_status_make(MAAGE_STATUS_INVALID_STATE, MAAGE_CONTEXT_REGISTRY, MAAGE_OPERATION_REGISTER, 0);
    node = (maage_biome_extension *) calloc(1, sizeof(*node));
    if (!node) return maage_status_make(MAAGE_STATUS_ALLOCATION_FAILED, MAAGE_CONTEXT_REGISTRY, MAAGE_OPERATION_REGISTER, 0);
    length = strlen(name) + 1;
    node->name = (char *) malloc(length);
    if (!node->name) {
        free(node);
        return maage_status_make(MAAGE_STATUS_ALLOCATION_FAILED, MAAGE_CONTEXT_REGISTRY, MAAGE_OPERATION_REGISTER, 0);
    }
    memcpy(node->name, name, length);
    node->definition = *definition;
    node->display = (char *) malloc(strlen(definition->display_name ? definition->display_name : name) + 1);
    if (!node->display) {
        free(node->name);
        free(node);
        return maage_status_make(MAAGE_STATUS_ALLOCATION_FAILED, MAAGE_CONTEXT_REGISTRY, MAAGE_OPERATION_REGISTER, 0);
    }
    strcpy(node->display, definition->display_name ? definition->display_name : name);
    node->definition.display_name = node->display;
    if (definition->generation_data_size) {
        if (!definition->generation_data) {
            free(node->display);
            free(node->name);
            free(node);
            return maage_status_make(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_CONTEXT_REGISTRY, MAAGE_OPERATION_REGISTER, 0);
        }
        node->payload = malloc(definition->generation_data_size);
        if (!node->payload) {
            free(node->display);
            free(node->name);
            free(node);
            return maage_status_make(MAAGE_STATUS_ALLOCATION_FAILED, MAAGE_CONTEXT_REGISTRY, MAAGE_OPERATION_REGISTER, 0);
        }
        memcpy(node->payload, definition->generation_data, definition->generation_data_size);
        node->definition.generation_data = node->payload;
    }
    if (!registry->extension_pages) {
        registry->extension_pages = calloc(1, sizeof(maage_registry_pages));
        if (!registry->extension_pages) {
            free(node->name);
            free(node->payload);
            free(node->display);
            free(node);
            return maage_status_make(MAAGE_STATUS_ALLOCATION_FAILED, MAAGE_CONTEXT_REGISTRY, MAAGE_OPERATION_REGISTER, 0);
        }
    }
    pages = (maage_registry_pages *) registry->extension_pages;
    entry = maage_registry_pages_ensure(pages, (unsigned long) definition->id);
    if (!entry) {
        free(node->name);
        free(node->payload);
        free(node->display);
        free(node);
        return maage_status_make(MAAGE_STATUS_ALLOCATION_FAILED, MAAGE_CONTEXT_REGISTRY, MAAGE_OPERATION_REGISTER, 0);
    }
    *entry = node;
    node->next = (maage_biome_extension *) registry->extensions;
    registry->extensions = node;
    registry->name_buckets[bucket] = node;
    ++registry->count;
    return maage_status_make(MAAGE_STATUS_SUCCESS, MAAGE_CONTEXT_REGISTRY, MAAGE_OPERATION_REGISTER, 0);
}
static int maage_biome_valid(maage_biome_definition *definition) {
    unsigned int component;
    if (!(definition->base_height >= 1.0 && definition->base_height <= 240.0) ||
        !(definition->variation >= 0.0 && definition->variation <= 120.0) ||
        !(definition->temperature >= 0.0 && definition->temperature <= 2.0) ||
        !(definition->rainfall >= 0.0 && definition->rainfall <= 1.0) ||
        (definition->aquatic != 0 && definition->aquatic != 1)) return 0;
    for (component = 0; component < 3; ++component)
        if (!(definition->grass_color[component] >= 0.0f &&
              definition->grass_color[component] <= 1.0f &&
              definition->foliage_color[component] >= 0.0f &&
              definition->foliage_color[component] <= 1.0f)) return 0;
    return 1;
}
maage_status maage_biome_registry_finalize(maage_biome_registry *registry) {
    maage_status policy_result;
    maage_biome_extension *entry;
    if (registry) {
        policy_result = content_policy_finalize((content_policy *) registry->ownership_policy);
        if (maage_status_failed(policy_result)) return policy_result;
    }
    if (!registry) return maage_status_make(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_CONTEXT_REGISTRY, MAAGE_OPERATION_FINALIZE, 0);
    if (registry->finalized) return maage_status_make(MAAGE_STATUS_NOOP, 0, MAAGE_OPERATION_FINALIZE, 0);
    for (entry = (maage_biome_extension *) registry->extensions; entry; entry = entry->next)
        if (!maage_biome_valid(&entry->definition))
            return maage_status_make(MAAGE_STATUS_INVALID_STATE, MAAGE_CONTEXT_REGISTRY, MAAGE_OPERATION_FINALIZE, 0);
    registry->finalized = 1;
    return maage_status_make(MAAGE_STATUS_SUCCESS, 0, MAAGE_OPERATION_FINALIZE, 0);
}
static const maage_biome_extension *maage_biome_extension_get(const maage_biome_registry *registry, maage_biome_id id) {
    void **entry = registry && registry->extension_pages ? maage_registry_pages_find((const maage_registry_pages *) registry->extension_pages, (unsigned long) id) : NULL;
    return entry ? (const maage_biome_extension *) *entry : NULL;
}
const maage_biome_definition *maage_biome_registry_get(const maage_biome_registry *registry, maage_biome_id id) {
    const maage_biome_extension *entry;
    if (!registry) return NULL;
    if (registry->view_source) return id < registry->count ? &registry->view_definitions[id] : NULL;
    entry = maage_biome_extension_get(registry, id);
    return entry ? &entry->definition : NULL;
}
const char *maage_biome_registry_name(const maage_biome_registry *registry, maage_biome_id id) {
    const maage_biome_extension *entry;
    if (!registry) return NULL;
    entry = maage_biome_extension_get(registry, id);
    return entry ? entry->name : NULL;
}
maage_status maage_biome_registry_resolve_name(const maage_biome_registry *registry, const char *name, maage_biome_id *id) {
    maage_status policy_result;
    size_t bucket;
    int found;
    if (registry) {
        policy_result = content_policy_read((content_policy *) registry->ownership_policy, CONTENT_BIOME, name);
        if (maage_status_failed(policy_result)) return policy_result;
    }
    if (!registry || !name || !id) return maage_status_make(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_CONTEXT_REGISTRY, MAAGE_OPERATION_RESOLVE, 0);
    if (!registry->name_capacity) return maage_status_make(MAAGE_STATUS_NOT_FOUND, MAAGE_CONTEXT_REGISTRY, MAAGE_OPERATION_RESOLVE, 0);
    bucket = biome_name_bucket(registry, name, &found);
    if (!found) return maage_status_make(MAAGE_STATUS_NOT_FOUND, MAAGE_CONTEXT_REGISTRY, MAAGE_OPERATION_RESOLVE, 0);
    *id = ((maage_biome_extension *) registry->name_buckets[bucket])->definition.id;
    return maage_status_make(MAAGE_STATUS_SUCCESS, 0, MAAGE_OPERATION_RESOLVE, 0);
}

size_t maage_biome_registry_count(const maage_biome_registry *registry) {
    return registry ? registry->count : 0;
}

maage_status maage_biome_registry_update(maage_biome_registry *registry, const maage_biome_definition *definition) {
    maage_status policy_result;
    maage_biome_extension *entry;
    void **slot;
    if (registry) {
        policy_result = content_policy_update((content_policy *) registry->ownership_policy, CONTENT_BIOME, definition ? maage_biome_registry_name(registry, definition->id) : NULL);
        if (maage_status_failed(policy_result)) return policy_result;
    }
    if (!registry || registry->finalized || !definition || !registry->extension_pages) return maage_status_make(MAAGE_STATUS_INVALID_STATE, MAAGE_CONTEXT_REGISTRY, MAAGE_OPERATION_REGISTER, 0);
    slot = maage_registry_pages_find((maage_registry_pages *) registry->extension_pages, definition->id);
    entry = slot ? (maage_biome_extension *) *slot : NULL;
    if (!entry) return maage_status_make(MAAGE_STATUS_NOT_FOUND, MAAGE_CONTEXT_REGISTRY, MAAGE_OPERATION_REGISTER, 0);
    if (definition->generation_data_size != entry->definition.generation_data_size) return maage_status_make(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_CONTEXT_REGISTRY, MAAGE_OPERATION_REGISTER, 0);
    entry->definition = *definition;
    entry->definition.generation_data = entry->payload;
    entry->definition.display_name = entry->display;
    return maage_status_make(MAAGE_STATUS_SUCCESS, 0, MAAGE_OPERATION_REGISTER, 0);
}
