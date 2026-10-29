#include "maage/biome.h"
#include "maage/internal/registry_pages.h"
#include <stdlib.h>
#include <string.h>

typedef struct maage_biome_extension {
    maage_biome_definition definition;
    char *name;
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

maage_status maage_biome_registry_init(maage_biome_registry *registry) {
    static const double parameters[MAAGE_BIOME_COUNT][4] = {
        {46.0, 8.0, 0.5, 0.5}, {65.0, 3.0, 0.8, 0.4}, {65.0, 5.0, 2.0, 0.0}, {87.0, 30.0, 0.2, 0.3}, {68.0, 8.0, 0.7, 0.8}, {69.0, 10.0, 0.05, 0.8}};
    static const char *const names[MAAGE_BIOME_COUNT] = {
        "minecraft:ocean", "minecraft:plains", "minecraft:desert",
        "minecraft:hills", "minecraft:forest", "minecraft:taiga"};
    maage_biome_definition *definition;
    unsigned int id, component;
    if (!registry) return maage_status_make(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_CONTEXT_REGISTRY, MAAGE_OPERATION_INITIALIZE, 0);
    memset(registry, 0, sizeof(*registry));
    for (id = 0; id < MAAGE_BIOME_COUNT; ++id) {
        definition = &registry->builtin_definitions[id];
        definition->id = id;
        definition->base_height = parameters[id][0];
        definition->variation = parameters[id][1];
        definition->temperature = parameters[id][2];
        definition->rainfall = parameters[id][3];
        definition->surface = MAAGE_STATE_BIOME_GRASS(id);
        definition->filler = MAAGE_STATE_DIRT;
        if (id == MAAGE_BIOME_DESERT) {
            definition->surface = MAAGE_STATE_SAND;
            definition->filler = MAAGE_STATE_SAND;
        } else if (id == MAAGE_BIOME_OCEAN) {
            definition->surface = MAAGE_STATE_GRAVEL;
            definition->filler = MAAGE_STATE_DIRT;
        }
        for (component = 0; component < 3; ++component) {
            definition->grass_color[component] = 1.0f;
            definition->foliage_color[component] = 1.0f;
        }
        registry->builtin_names[id] = names[id];
    }
    return maage_status_make(MAAGE_STATUS_SUCCESS, 0, MAAGE_OPERATION_INITIALIZE, 0);
}
maage_status maage_biome_registry_destroy(maage_biome_registry *registry) {
    maage_biome_extension *node, *next;
    if (!registry) return maage_status_make(MAAGE_STATUS_NOOP, 0, MAAGE_OPERATION_DESTROY, 0);
    node = (maage_biome_extension *) registry->extensions;
    while (node) {
        next = node->next;
        free(node->name);
        free(node);
        node = next;
    }
    if (registry->extension_pages) {
        maage_registry_pages_destroy((maage_registry_pages *) registry->extension_pages, NULL);
        free(registry->extension_pages);
    }
    memset(registry, 0, sizeof(*registry));
    return maage_status_make(MAAGE_STATUS_SUCCESS, 0, MAAGE_OPERATION_DESTROY, 0);
}
maage_status maage_biome_registry_register(maage_biome_registry *registry,
                                           const char *name, const maage_biome_definition *definition) {
    maage_biome_extension *node, *existing;
    maage_registry_pages *pages;
    void **entry;
    size_t length;
    unsigned int id;
    if (!registry || !definition || !maage_biome_name_valid(name) ||
        !registry->builtin_names[0] || definition->id < MAAGE_BIOME_COUNT ||
        (unsigned long) definition->id > 4294967295UL)
        return maage_status_make(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_CONTEXT_REGISTRY, MAAGE_OPERATION_REGISTER, 0);
    if (registry->finalized) return maage_status_make(MAAGE_STATUS_INVALID_STATE, MAAGE_CONTEXT_REGISTRY, MAAGE_OPERATION_REGISTER, 0);
    for (id = 0; id < MAAGE_BIOME_COUNT; ++id)
        if (!strcmp(registry->builtin_names[id], name))
            return maage_status_make(MAAGE_STATUS_INVALID_STATE, MAAGE_CONTEXT_REGISTRY, MAAGE_OPERATION_REGISTER, 0);
    for (existing = (maage_biome_extension *) registry->extensions; existing; existing = existing->next)
        if (existing->definition.id == definition->id || !strcmp(existing->name, name))
            return maage_status_make(MAAGE_STATUS_INVALID_STATE, MAAGE_CONTEXT_REGISTRY, MAAGE_OPERATION_REGISTER, 0);
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
    if (!registry->extension_pages) {
        registry->extension_pages = calloc(1, sizeof(maage_registry_pages));
        if (!registry->extension_pages) {
            free(node->name);
            free(node);
            return maage_status_make(MAAGE_STATUS_ALLOCATION_FAILED, MAAGE_CONTEXT_REGISTRY, MAAGE_OPERATION_REGISTER, 0);
        }
    }
    pages = (maage_registry_pages *) registry->extension_pages;
    entry = maage_registry_pages_ensure(pages, (unsigned long) definition->id);
    if (!entry) {
        free(node->name);
        free(node);
        return maage_status_make(MAAGE_STATUS_ALLOCATION_FAILED, MAAGE_CONTEXT_REGISTRY, MAAGE_OPERATION_REGISTER, 0);
    }
    *entry = node;
    node->next = (maage_biome_extension *) registry->extensions;
    registry->extensions = node;
    return maage_status_make(MAAGE_STATUS_SUCCESS, MAAGE_CONTEXT_REGISTRY, MAAGE_OPERATION_REGISTER, 0);
}
static int maage_biome_valid(const maage_biome_definition *definition) {
    unsigned int component;
    if (!(definition->base_height >= 1.0 && definition->base_height <= 240.0) ||
        !(definition->variation >= 0.0 && definition->variation <= 120.0) ||
        !(definition->temperature >= 0.0 && definition->temperature <= 2.0) ||
        !(definition->rainfall >= 0.0 && definition->rainfall <= 1.0)) return 0;
    for (component = 0; component < 3; ++component)
        if (!(definition->grass_color[component] >= 0.0f &&
              definition->grass_color[component] <= 1.0f &&
              definition->foliage_color[component] >= 0.0f &&
              definition->foliage_color[component] <= 1.0f)) return 0;
    return 1;
}
maage_status maage_biome_registry_finalize(maage_biome_registry *registry) {
    maage_biome_extension *entry;
    unsigned int id;
    if (!registry || !registry->builtin_names[0])
        return maage_status_make(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_CONTEXT_REGISTRY, MAAGE_OPERATION_FINALIZE, 0);
    if (registry->finalized) return maage_status_make(MAAGE_STATUS_NOOP, 0, MAAGE_OPERATION_FINALIZE, 0);
    for (id = 0; id < MAAGE_BIOME_COUNT; ++id)
        if (!registry->builtin_names[id] || registry->builtin_definitions[id].id != id ||
            !maage_biome_valid(&registry->builtin_definitions[id]))
            return maage_status_make(MAAGE_STATUS_INVALID_STATE, MAAGE_CONTEXT_REGISTRY, MAAGE_OPERATION_FINALIZE, 0);
    for (entry = (maage_biome_extension *) registry->extensions; entry; entry = entry->next)
        if (!maage_biome_valid(&entry->definition))
            return maage_status_make(MAAGE_STATUS_INVALID_STATE, MAAGE_CONTEXT_REGISTRY, MAAGE_OPERATION_FINALIZE, 0);
    registry->finalized = 1;
    return maage_status_make(MAAGE_STATUS_SUCCESS, 0, MAAGE_OPERATION_FINALIZE, 0);
}
static const maage_biome_extension *maage_biome_extension_get(
    const maage_biome_registry *registry, maage_biome_id id) {
    void **entry = registry && registry->extension_pages ? maage_registry_pages_find((const maage_registry_pages *) registry->extension_pages,
                                                                                     (unsigned long) id)
                                                         : NULL;
    return entry ? (const maage_biome_extension *) *entry : NULL;
}
const maage_biome_definition *maage_biome_registry_get(
    const maage_biome_registry *registry, maage_biome_id id) {
    const maage_biome_extension *entry;
    if (!registry || !registry->builtin_names[0]) return NULL;
    if (id < MAAGE_BIOME_COUNT) return &registry->builtin_definitions[id];
    entry = maage_biome_extension_get(registry, id);
    return entry ? &entry->definition : NULL;
}
const char *maage_biome_registry_name(
    const maage_biome_registry *registry, maage_biome_id id) {
    const maage_biome_extension *entry;
    if (!registry || !registry->builtin_names[0]) return NULL;
    if (id < MAAGE_BIOME_COUNT) return registry->builtin_names[id];
    entry = maage_biome_extension_get(registry, id);
    return entry ? entry->name : NULL;
}
maage_status maage_biome_registry_resolve_name(
    const maage_biome_registry *registry, const char *name, maage_biome_id *id) {
    maage_biome_extension *entry;
    unsigned int i;
    if (!registry || !name || !id || !registry->builtin_names[0])
        return maage_status_make(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_CONTEXT_REGISTRY, MAAGE_OPERATION_RESOLVE, 0);
    for (i = 0; i < MAAGE_BIOME_COUNT; ++i)
        if (!strcmp(registry->builtin_names[i], name)) {
            *id = i;
            return maage_status_make(MAAGE_STATUS_SUCCESS, MAAGE_CONTEXT_REGISTRY, MAAGE_OPERATION_RESOLVE, 0);
        }
    for (entry = (maage_biome_extension *) registry->extensions; entry; entry = entry->next)
        if (!strcmp(entry->name, name)) {
            *id = entry->definition.id;
            return maage_status_make(MAAGE_STATUS_SUCCESS, MAAGE_CONTEXT_REGISTRY, MAAGE_OPERATION_RESOLVE, 0);
        }
    return maage_status_make(MAAGE_STATUS_NOT_FOUND, MAAGE_CONTEXT_REGISTRY, MAAGE_OPERATION_RESOLVE, 0);
}
