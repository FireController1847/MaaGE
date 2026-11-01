#ifndef MAAGE_BIOME_H
#define MAAGE_BIOME_H
#include "maage/blocks.h"
#ifdef __cplusplus
extern "C" {
#endif
#define MAAGE_BIOME_COUNT 6
typedef unsigned long maage_biome_id;
/* Generation probabilities, independent of Client color blending. Trees are
 * oak/spruce/birch probabilities per jittered eight-block candidate; their sum
 * is at most one. Other densities are per eligible decoration/feature attempt.
 * Zero disables a feature. Finalization validates every probability. */
typedef struct maage_biome_decoration {
    double trees[3];
    double grass, flowers, dead_bushes, cacti, mushrooms;
    double emeralds, lakes;
} maage_biome_decoration;
typedef struct maage_biome_definition {
    maage_biome_id id;
    double base_height, variation, temperature, rainfall;
    maage_block_state_id surface, filler;
    int aquatic; /* Discrete geographic trait used by dry-land spawn selection. */
    maage_biome_decoration decoration;
    float grass_color[3], foliage_color[3];
} maage_biome_definition;
typedef struct maage_biome_registry {
    maage_builtin_ids ids;
    size_t count;
    /* Builtins are inline; extensions use a private sparse numeric index.
     * Names/definitions remain immutable after finalization. */
    maage_biome_definition builtin_definitions[MAAGE_BIOME_COUNT];
    const char *builtin_names[MAAGE_BIOME_COUNT];
    void *extension_pages;
    void *extensions;
    int finalized;
} maage_biome_registry;
/* Initialize classic biomes, register additional stable names before finalizing.
 * Every registered biome has a unique namespaced persistence identity. Registration
 * returns the allocated ID in definition.id. Shared content binds numeric surface
 * references before finalization; unbound initialization also serves color loading. */
maage_status maage_biome_registry_init(maage_biome_registry *registry);
maage_status maage_biome_registry_destroy(maage_biome_registry *registry);
maage_status maage_biome_registry_register(maage_biome_registry *registry, const char *external_name, maage_biome_definition *definition);
size_t maage_biome_registry_count(const maage_biome_registry *registry);
maage_status maage_biome_registry_finalize(maage_biome_registry *registry);
const maage_biome_definition *maage_biome_registry_get(const maage_biome_registry *registry, maage_biome_id id);
const char *maage_biome_registry_name(const maage_biome_registry *registry, maage_biome_id id);
maage_status maage_biome_registry_resolve_name(const maage_biome_registry *registry, const char *name, maage_biome_id *id);
#ifdef __cplusplus
}
#endif
#endif
