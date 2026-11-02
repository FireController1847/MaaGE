#include "base_internal.h"
#include <string.h>
static maage_status base_biome(maage_biome_registry *registry, const char *name, const char *label, double height, double variation, double temperature, double rainfall, int aquatic, double oak, double spruce, double birch, double grass, double flowers, double bushes, double cacti, double mushrooms, double emeralds, double lakes, unsigned long *id) {
    maage_biome_definition biome;
    maage_base_biome_decoration decoration;
    maage_status result;
    unsigned int component;
    memset(&biome, 0, sizeof(biome));
    memset(&decoration, 0, sizeof(decoration));
    biome.generation_data = &decoration;
    biome.generation_data_size = sizeof(decoration);
    biome.display_name = label;
    biome.base_height = height;
    biome.variation = variation;
    biome.temperature = temperature;
    biome.rainfall = rainfall;
    biome.aquatic = aquatic;
    decoration.trees[0] = oak;
    decoration.trees[1] = spruce;
    decoration.trees[2] = birch;
    decoration.grass = grass;
    decoration.flowers = flowers;
    decoration.dead_bushes = bushes;
    decoration.cacti = cacti;
    decoration.mushrooms = mushrooms;
    decoration.emeralds = emeralds;
    decoration.lakes = lakes;
    for (component = 0; component < 3; ++component)
        biome.grass_color[component] = biome.foliage_color[component] = 1;
    result = maage_biome_registry_register(registry, name, &biome);
    if (!maage_status_failed(result) && id) *id = biome.id;
    return result;
}
maage_status maage_base_biomes(maage_biome_registry *registry, maage_base_ids *ids) {
    maage_status result;
    result = base_biome(registry, "minecraft:ocean", "Ocean", 46, 8, 0.5, 0.5, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, &ids->biome_ocean);
    if (maage_status_failed(result)) return result;
    ids->biome_ids[0] = ids->biome_ocean;
    result = base_biome(registry, "minecraft:plains", "Plains", 65, 3, 0.8, 0.4, 0, 0.04, 0, 0, 0.25, 1.0 / 97.0, 0, 0, 0, 0, 1, &ids->biome_plains);
    if (maage_status_failed(result)) return result;
    ids->biome_ids[1] = ids->biome_plains;
    result = base_biome(registry, "minecraft:desert", "Desert", 65, 5, 2, 0, 0, 0, 0, 0, 0, 0, 1.0 / 43.0, 1.0 / 64.0, 0, 0, 1, &ids->biome_desert);
    if (maage_status_failed(result)) return result;
    ids->biome_ids[2] = ids->biome_desert;
    result = base_biome(registry, "minecraft:hills", "Extreme Hills", 87, 30, 0.2, 0.3, 0, 0.09, 0, 0, 0.25, 1.0 / 97.0, 0, 0, 0, 1, 1, &ids->biome_hills);
    if (maage_status_failed(result)) return result;
    ids->biome_ids[3] = ids->biome_hills;
    result = base_biome(registry, "minecraft:forest", "Forest", 68, 8, 0.7, 0.8, 0, 0.64, 0, 0.16, 0.25, 1.0 / 97.0, 0, 0, 1.0 / 73.0, 0, 1, &ids->biome_forest);
    if (maage_status_failed(result)) return result;
    ids->biome_ids[4] = ids->biome_forest;
    result = base_biome(registry, "minecraft:taiga", "Taiga", 69, 10, 0.05, 0.8, 0, 0, 0.70, 0, 0.25, 1.0 / 97.0, 0, 0, 0, 0, 1, &ids->biome_taiga);
    if (maage_status_failed(result)) return result;
    ids->biome_ids[5] = ids->biome_taiga;
    return result;
}
