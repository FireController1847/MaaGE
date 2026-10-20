#include "maage/biome.h"

#include <string.h>

void maage_biome_registry_init(maage_biome_registry *registry)
{
    static const double parameters[MAAGE_BIOME_COUNT][4] = {
        { 46.0, 8.0, 0.5, 0.5 },
        { 65.0, 3.0, 0.8, 0.4 },
        { 65.0, 5.0, 2.0, 0.0 },
        { 87.0, 30.0, 0.2, 0.3 },
        { 68.0, 8.0, 0.7, 0.8 },
        { 69.0, 10.0, 0.05, 0.8 }
    };
    maage_biome_definition *definition;
    unsigned int id, component;
    if (registry == NULL) return;
    memset(registry, 0, sizeof(*registry));
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
        /* Resources replace these neutral defaults using actual classic
         * colormaps before finalization; runtime never resolves a texture. */
        for (component = 0; component < 3; ++component) {
            definition->grass_color[component] = 1.0f;
            definition->foliage_color[component] = 1.0f;
        }
        registry->occupied[id] = 1;
    }
}

int maage_biome_registry_finalize(maage_biome_registry *registry)
{
    const maage_biome_definition *definition;
    unsigned int id, component;
    if (registry == NULL || registry->finalized) return 0;
    for (id = 0; id < MAAGE_BIOME_COUNT; ++id)
        if (!registry->occupied[id]) return 0;
    for (id = 0; id < MAAGE_BIOME_CAPACITY; ++id) {
        if (!registry->occupied[id]) continue;
        definition = &registry->definitions[id];
        if (definition->id != id ||
            !(definition->base_height >= 1.0 && definition->base_height <= 240.0) ||
            !(definition->variation >= 0.0 && definition->variation <= 120.0) ||
            !(definition->temperature >= 0.0 && definition->temperature <= 2.0) ||
            !(definition->rainfall >= 0.0 && definition->rainfall <= 1.0)) return 0;
        for (component = 0; component < 3; ++component) {
            if (!(definition->grass_color[component] >= 0.0f &&
                definition->grass_color[component] <= 1.0f &&
                definition->foliage_color[component] >= 0.0f &&
                definition->foliage_color[component] <= 1.0f)) return 0;
        }
    }
    registry->finalized = 1;
    return 1;
}

const maage_biome_definition *maage_biome_registry_get(
    const maage_biome_registry *registry, maage_biome_id id)
{
    if (registry == NULL || id >= MAAGE_BIOME_CAPACITY ||
        !registry->occupied[id]) return NULL;
    return &registry->definitions[id];
}
