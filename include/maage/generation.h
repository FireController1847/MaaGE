#ifndef MAAGE_GENERATION_H
#define MAAGE_GENERATION_H
#include "maage/world.h"
#include "maage/jobs.h"
typedef maage_status (*maage_generation_feature_callback)(void *, maage_chunk *, maage_world_seed, const maage_block_registry *, const maage_biome_registry *, const maage_generation_policy *, const maage_job_token *, maage_status_trace *);
typedef struct maage_generation_feature {
    unsigned long id;
    unsigned int stage;
    maage_generation_feature_callback callback;
    void *context; /* Borrowed immutable module data; callbacks must not mutate it. */
} maage_generation_feature;
typedef struct maage_ore_definition {
    maage_block_state_id state;
    unsigned int attempts, size, minimum_y, maximum_y;
    unsigned long salt; /* Coordinate RNG domain, independent of numeric registry order. */
    int triangular;
    const void *context; /* Borrowed immutable provider data; outlives generation jobs. */
    maage_status (*density)(const void *, maage_world_seed, const maage_biome_registry *, const maage_generation_policy *, int, int, void *, double *);
} maage_ore_definition;
maage_status maage_generation_feature_register(maage_block_registry *, const char *, const maage_generation_feature *, unsigned long *);
maage_status maage_generation_ore_register(maage_block_registry *, const char *, const maage_ore_definition *, unsigned long *);
size_t maage_generation_ore_count(const maage_block_registry *);
const maage_ore_definition *maage_generation_ore_at(const maage_block_registry *, size_t);
maage_status maage_generation_features_run(const maage_block_registry *, unsigned int, maage_chunk *, maage_world_seed, const maage_biome_registry *, const maage_generation_policy *, const maage_job_token *, maage_status_trace *);
/* Detached output metadata is borrowed for one generator invocation only.
 * The provider may mutate these arrays/counters; it must not retain pointers. */
typedef struct maage_generation_output {
    int x, z;
    maage_biome_id *biomes;    /* 256 cells, z*16+x. */
    unsigned short *heightmap; /* 256 first-air heights, 0..256. */
    unsigned int *stages;
    unsigned long *features; /* MAAGE_GENERATION_STAGE_COUNT counters. */
} maage_generation_output;
maage_status maage_chunk_generation_output(maage_chunk *, maage_generation_output *);
/* Detached worker outputs only. Never pass a published World column. */
maage_block_state_id maage_chunk_get(const maage_chunk *, int, int, int);
maage_status maage_chunk_set(maage_chunk *, int, int, int, maage_block_state_id);
int maage_chunk_x(const maage_chunk *);
int maage_chunk_z(const maage_chunk *);
maage_status maage_chunk_set_biome(maage_chunk *, int, int, maage_biome_id);
/* Bounded owner plan: INVALID cells are untouched; every registered state,
 * including numeric zero, is placeable. Block entities require ordinary owner
 * mutations and return NOT_IMPLEMENTED here, without changing authority. */
typedef struct maage_block_plan {
    int x, y, z, edge, height;
    maage_block_state_id *cells;
} maage_block_plan;
/* Owner preflight on detached columns, then allocation-free publication. */
maage_status maage_world_apply_plan(maage_world *world, const maage_block_plan *plan);
#endif
