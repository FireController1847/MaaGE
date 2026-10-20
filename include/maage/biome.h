#ifndef MAAGE_BIOME_H
#define MAAGE_BIOME_H
#include "maage/blocks.h"
#ifdef __cplusplus
extern "C" {
#endif
#define MAAGE_BIOME_CAPACITY 256
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
    /* Version-1 generator height parameters are in blocks above Y=0. */
    double base_height, variation, temperature, rainfall;
    maage_block_state_id surface, filler;
    float grass_color[3], foliage_color[3];
} maage_biome_definition;
typedef struct maage_biome_registry {
    maage_biome_definition definitions[MAAGE_BIOME_CAPACITY];
    unsigned char occupied[MAAGE_BIOME_CAPACITY];
    int finalized;
} maage_biome_registry;
/* Initialize classic-biome approximations, amend during resource registration,
 * then finalize. Numeric lookups are direct O(1); no names in runtime records. */
void maage_biome_registry_init(maage_biome_registry *registry);
int maage_biome_registry_finalize(maage_biome_registry *registry);
const maage_biome_definition *maage_biome_registry_get(const maage_biome_registry *registry, maage_biome_id id);
#ifdef __cplusplus
}
#endif
#endif
