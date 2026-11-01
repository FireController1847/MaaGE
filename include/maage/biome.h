#ifndef MAAGE_BIOME_H
#define MAAGE_BIOME_H
#include "maage/blocks.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef unsigned long maage_biome_id;
typedef struct maage_biome_definition {
    maage_biome_id id;
    const char *display_name;
    double base_height, variation, temperature, rainfall;
    maage_block_state_id surface, filler;
    int aquatic; /* Discrete geographic trait used by dry-land spawn selection. */
    const void *generation_data;
    size_t generation_data_size; /* Registry copies this immutable payload. */
    float grass_color[3], foliage_color[3];
} maage_biome_definition;
typedef struct maage_biome_registry {
    size_t count, name_capacity;
    void **name_buckets;
    const void *generation_context; /* Borrowed generator data, never interpreted by registry. */
    void *extension_pages;
    void *extensions;
    int finalized;
} maage_biome_registry;
/* Initialize empty registries; register names before finalizing.
 * Every registered biome has a unique namespaced persistence identity. Registration
 * returns the allocated ID in definition.id. Content binds numeric surface references. */
maage_status maage_biome_registry_init(maage_biome_registry *registry);
maage_status maage_biome_registry_destroy(maage_biome_registry *registry);
maage_status maage_biome_registry_register(maage_biome_registry *registry, const char *external_name, maage_biome_definition *definition);
size_t maage_biome_registry_count(const maage_biome_registry *registry);
maage_status maage_biome_registry_update(maage_biome_registry *registry, const maage_biome_definition *definition);
maage_status maage_biome_registry_finalize(maage_biome_registry *registry);
const maage_biome_definition *maage_biome_registry_get(const maage_biome_registry *registry, maage_biome_id id);
const char *maage_biome_registry_name(const maage_biome_registry *registry, maage_biome_id id);
maage_status maage_biome_registry_resolve_name(const maage_biome_registry *registry, const char *name, maage_biome_id *id);
#ifdef __cplusplus
}
#endif
#endif
