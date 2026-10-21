#include "maage/biome.h"
#include <stdlib.h>
#include <string.h>

static int maage_biome_name_valid(const char *name)
{
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
            (colon && name[i] == '/'))) return 0;
    }
    return colon;
}

void maage_biome_registry_init(maage_biome_registry *registry)
{
    static const double parameters[MAAGE_BIOME_COUNT][4] = {
        { 46.0, 8.0, 0.5, 0.5 }, { 65.0, 3.0, 0.8, 0.4 },
        { 65.0, 5.0, 2.0, 0.0 }, { 87.0, 30.0, 0.2, 0.3 },
        { 68.0, 8.0, 0.7, 0.8 }, { 69.0, 10.0, 0.05, 0.8 }
    };
    static const char *const names[MAAGE_BIOME_COUNT] = {
        "minecraft:ocean", "minecraft:plains", "minecraft:desert",
        "minecraft:hills", "minecraft:forest", "minecraft:taiga"
    };
    maage_biome_definition *definition;
    unsigned int id, component;
    if (!registry) return;
    memset(registry, 0, sizeof(*registry));
    registry->definitions = registry->builtin_definitions;
    registry->occupied = registry->builtin_occupied;
    registry->names = registry->builtin_names;
    registry->capacity = MAAGE_BIOME_COUNT;
    for (id = 0; id < MAAGE_BIOME_COUNT; ++id) {
        definition = &registry->definitions[id];
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
        registry->names[id] = names[id];
        registry->occupied[id] = 1;
    }
}
void maage_biome_registry_destroy(maage_biome_registry *registry)
{
    size_t i;
    if (!registry) return;
    if (registry->names) for (i = MAAGE_BIOME_COUNT; i < registry->capacity; ++i)
        free((void *)registry->names[i]);
    if (registry->definitions != registry->builtin_definitions) free(registry->definitions);
    if (registry->occupied != registry->builtin_occupied) free(registry->occupied);
    if (registry->names != registry->builtin_names) free((void *)registry->names);
    memset(registry, 0, sizeof(*registry));
}
maage_registry_result maage_biome_registry_register(maage_biome_registry *registry,
    const char *name, const maage_biome_definition *definition)
{
    size_t i, count, old, length;
    maage_biome_definition *definitions;
    unsigned char *occupied;
    const char **names;
    char *copy;
    if (!registry || !definition || !maage_biome_name_valid(name) ||
        !registry->definitions || definition->id < MAAGE_BIOME_COUNT)
        return MAAGE_REGISTRY_INVALID;
    if (registry->finalized) return MAAGE_REGISTRY_FROZEN;
    for (i = 0; i < registry->capacity; ++i)
        if (registry->occupied[i] && !strcmp(registry->names[i], name))
            return MAAGE_REGISTRY_DUPLICATE;
    if (definition->id >= registry->capacity) {
        old = registry->capacity;
        if (definition->id >= (size_t)-1 / sizeof(*definitions) ||
            definition->id >= (size_t)-1 / sizeof(*names))
            return MAAGE_REGISTRY_OUT_OF_MEMORY;
        count = old;
        while (count <= definition->id) {
            if (count > ((size_t)-1 / sizeof(*definitions)) / 2 ||
                count > ((size_t)-1 / sizeof(*names)) / 2) {
                count = (size_t)definition->id + 1;
                break;
            }
            count *= 2;
        }
        definitions = (maage_biome_definition *)calloc(count, sizeof(*definitions));
        occupied = (unsigned char *)calloc(count, sizeof(*occupied));
        names = (const char **)calloc(count, sizeof(*names));
        if (!definitions || !occupied || !names) {
            free(definitions); free(occupied); free(names);
            return MAAGE_REGISTRY_OUT_OF_MEMORY;
        }
        memcpy(definitions, registry->definitions, old * sizeof(*definitions));
        memcpy(occupied, registry->occupied, old * sizeof(*occupied));
        memcpy(names, registry->names, old * sizeof(*names));
        if (registry->definitions != registry->builtin_definitions) free(registry->definitions);
        if (registry->occupied != registry->builtin_occupied) free(registry->occupied);
        if (registry->names != registry->builtin_names) free((void *)registry->names);
        registry->definitions = definitions;
        registry->occupied = occupied;
        registry->names = names;
        registry->capacity = count;
    }
    if (registry->occupied[definition->id]) return MAAGE_REGISTRY_DUPLICATE;
    length = strlen(name);
    copy = (char *)malloc(length + 1);
    if (!copy) return MAAGE_REGISTRY_OUT_OF_MEMORY;
    memcpy(copy, name, length + 1);
    registry->names[definition->id] = copy;
    registry->definitions[definition->id] = *definition;
    registry->occupied[definition->id] = 1;
    return MAAGE_REGISTRY_OK;
}
int maage_biome_registry_finalize(maage_biome_registry *registry)
{
    const maage_biome_definition *definition;
    size_t id;
    unsigned int component;
    if (!registry || !registry->definitions || registry->finalized) return 0;
    for (id = 0; id < MAAGE_BIOME_COUNT; ++id)
        if (!registry->occupied[id]) return 0;
    for (id = 0; id < registry->capacity; ++id) {
        if (!registry->occupied[id]) continue;
        definition = &registry->definitions[id];
        if (definition->id != id ||
            !(definition->base_height >= 1.0 && definition->base_height <= 240.0) ||
            !(definition->variation >= 0.0 && definition->variation <= 120.0) ||
            !(definition->temperature >= 0.0 && definition->temperature <= 2.0) ||
            !(definition->rainfall >= 0.0 && definition->rainfall <= 1.0)) return 0;
        for (component = 0; component < 3; ++component)
            if (!(definition->grass_color[component] >= 0.0f &&
                definition->grass_color[component] <= 1.0f &&
                definition->foliage_color[component] >= 0.0f &&
                definition->foliage_color[component] <= 1.0f)) return 0;
    }
    registry->finalized = 1;
    return 1;
}
const maage_biome_definition *maage_biome_registry_get(
    const maage_biome_registry *registry, maage_biome_id id)
{
    if (!registry || id >= registry->capacity || !registry->occupied[id]) return NULL;
    return &registry->definitions[id];
}
const char *maage_biome_registry_name(
    const maage_biome_registry *registry, maage_biome_id id)
{
    return maage_biome_registry_get(registry, id) ? registry->names[id] : NULL;
}
maage_registry_result maage_biome_registry_resolve_name(
    const maage_biome_registry *registry, const char *name, maage_biome_id *id)
{
    size_t i;
    if (!registry || !name || !id) return MAAGE_REGISTRY_INVALID;
    for (i = 0; i < registry->capacity; ++i) {
        if (registry->occupied[i] && !strcmp(registry->names[i], name)) {
            *id = (maage_biome_id)i;
            return MAAGE_REGISTRY_OK;
        }
    }
    return MAAGE_REGISTRY_INVALID;
}
