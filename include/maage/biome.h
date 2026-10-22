#ifndef MAAGE_BIOME_H
#define MAAGE_BIOME_H
#include "maage/blocks.h"
#ifdef __cplusplus
extern "C" {
#endif
#define MAAGE_BIOME_OCEAN 0
#define MAAGE_BIOME_PLAINS 1
#define MAAGE_BIOME_DESERT 2
#define MAAGE_BIOME_HILLS 3
#define MAAGE_BIOME_FOREST 4
#define MAAGE_BIOME_TAIGA 5
#define MAAGE_BIOME_COUNT 6
#define MAAGE_STATE_BIOME_GRASS_BASE 256UL
#define MAAGE_STATE_BIOME_GRASS(id) (MAAGE_STATE_BIOME_GRASS_BASE + (id))
typedef unsigned int maage_biome_id;
typedef struct maage_biome_definition {
    maage_biome_id id;
    double base_height, variation, temperature, rainfall;
    maage_block_state_id surface, filler;
    float grass_color[3], foliage_color[3];
} maage_biome_definition;
typedef struct maage_biome_registry {
    /* Builtins are inline; extensions use a private sparse numeric index.
     * Names/definitions remain immutable after finalization. */
    maage_biome_definition builtin_definitions[MAAGE_BIOME_COUNT];
    const char *builtin_names[MAAGE_BIOME_COUNT];
    void *extension_pages;
    void *extensions;
    int finalized;
} maage_biome_registry;
/* Initialize classic biomes, register additional stable names before finalizing.
 * Every registered biome has a unique namespaced persistence identity. */
maage_status maage_biome_registry_init(maage_biome_registry *registry);
maage_status maage_biome_registry_destroy(maage_biome_registry *registry);
maage_status maage_biome_registry_register(maage_biome_registry *registry,
    const char *external_name, const maage_biome_definition *definition);
maage_status maage_biome_registry_finalize(maage_biome_registry *registry);
const maage_biome_definition *maage_biome_registry_get(
    const maage_biome_registry *registry, maage_biome_id id);
const char *maage_biome_registry_name(const maage_biome_registry *registry,
    maage_biome_id id);
maage_status maage_biome_registry_resolve_name(
    const maage_biome_registry *registry, const char *name, maage_biome_id *id);
#ifdef __cplusplus
}
#endif
#endif
