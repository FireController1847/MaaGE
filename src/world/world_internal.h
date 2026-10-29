#ifndef MAAGE_WORLD_INTERNAL_H
#define MAAGE_WORLD_INTERNAL_H
#include "maage/world.h"
#include "maage/biome.h"
#include "maage/seed.h"
#include "maage/jobs.h"
#include "maage/environment.h"
#include "maage/random_ticks.h"
#define MAAGE_SECTION_VOLUME 4096

/* All block/light arrays are numeric and owned by their column. Empty sections
 * are NULL; implicit air above the heightmap has skylight 15. */
typedef struct maage_section {
    maage_block_state_id states[MAAGE_SECTION_VOLUME];
    unsigned char light[MAAGE_SECTION_VOLUME];
} maage_section;
typedef struct maage_update_index {
    unsigned int kind, cell;
    size_t heap_index; /* index+1, 0 empty, (size_t)-1 tombstone */
} maage_update_index;
struct maage_chunk {
    int x, z;
    maage_section *sections[16];
    maage_biome_id biomes[256];
    unsigned short heightmap[256]; /* first air Y, 0..256 */
    /* Derived once after load/generation, then maintained by owner mutations. */
    unsigned short random_tick_counts[16];
    maage_scheduled_update *updates;
    size_t update_count, update_capacity;
    size_t fluid_update_count;
    maage_update_index *update_index;
    size_t update_index_capacity, update_index_used;
    maage_block_entity *entities;
    size_t entity_count, entity_capacity;
    int environment_initialized;
    int updates_relative; /* file import only; rebase before owner publication */
    unsigned int generation_stages;
    unsigned long generation_features[8];
};
maage_status maage_chunk_create(int x, int z, maage_chunk **output);
maage_status maage_chunk_destroy(maage_chunk *chunk);
size_t maage_chunk_cell(int x, int y, int z);
maage_block_state_id maage_chunk_get(const maage_chunk *chunk, int x, int y, int z);
maage_status maage_chunk_set(maage_chunk *chunk, int x, int y, int z, maage_block_state_id state);
unsigned char maage_chunk_light(const maage_chunk *chunk, int x, int y, int z);
/* Private/owner-edit columns only. Queue entries are unique per (kind,cell),
 * ordered by due time; the finite voxel domain bounds fluid pending work. */
maage_status maage_chunk_schedule_update(maage_chunk *chunk, const maage_scheduled_update *update);
maage_status maage_chunk_take_update(maage_chunk *chunk, unsigned long now_low,
    unsigned long now_high, maage_scheduled_update *update);
maage_status maage_chunk_copy_environment(maage_chunk *destination, const maage_chunk *source);
maage_status maage_chunk_replace_environment(maage_chunk *destination, const maage_chunk *source);
maage_status maage_chunk_environment_initialize(maage_chunk *chunk,
    const maage_block_registry *blocks, unsigned long now_low, unsigned long now_high);
maage_status maage_chunk_random_ticks_initialize(maage_chunk *chunk,
    const maage_block_registry *blocks);
maage_status maage_random_tick_grass(maage_world *world, int x, int y, int z,
    const maage_block_state_definition *state, maage_random_tick_context *context);
maage_status maage_random_tick_cactus(maage_world *world, int x, int y, int z,
    const maage_block_state_definition *state, maage_random_tick_context *context);
maage_status maage_random_tick_mushroom(maage_world *world, int x, int y, int z,
    const maage_block_state_definition *state, maage_random_tick_context *context);
maage_status maage_random_tick_bush(maage_world *world, int x, int y, int z,
    const maage_block_state_definition *state, maage_random_tick_context *context);
maage_status maage_random_tick_snow(maage_world *world, int x, int y, int z,
    const maage_block_state_definition *state, maage_random_tick_context *context);
maage_status maage_random_tick_ice(maage_world *world, int x, int y, int z,
    const maage_block_state_definition *state, maage_random_tick_context *context);
maage_status maage_chunk_block_entity_set(maage_chunk *chunk, unsigned int cell,
    unsigned long type, unsigned long version, const void *data, size_t length);
maage_status maage_chunk_block_entity_remove(maage_chunk *chunk, unsigned int cell);
/* Workers mutate only detached columns. Published columns are immutable and
 * pinned by the owner until every snapshot job has returned its completion.
 * Generation is original and has no vanilla seed-parity requirement. */
maage_status maage_generate_chunk_job(maage_chunk *chunk, maage_world_seed seed,
    const maage_block_registry *blocks, const maage_biome_registry *biomes,
    const maage_generation_policy *policy, const maage_job_token *token,
    maage_status_trace *trace);
maage_status maage_generation_spawn(maage_world_seed seed,
    const maage_biome_registry *biomes, const maage_generation_policy *policy,
    double feet[3]);
/* Feature stages replay bounded regional owners in coordinate order and
 * clip writes to the private output column; no neighbor storage is borrowed. */
typedef struct maage_generation_context {
    maage_chunk *chunk;
    maage_world_seed seed;
    const maage_block_registry *blocks;
    const maage_biome_registry *biomes;
    const maage_generation_policy *policy;
    const maage_job_token *token;
    maage_status_trace *trace;
    maage_status failure;
    int surface[256];
} maage_generation_context;
enum { MAAGE_STRUCTURE_DUNGEON = 1 };
typedef enum maage_generation_write_rule {
    MAAGE_GENERATION_WRITE_ANY = 0,
    MAAGE_GENERATION_WRITE_NATURAL,
    MAAGE_GENERATION_WRITE_STONE,
    MAAGE_GENERATION_WRITE_AIR,
    MAAGE_GENERATION_WRITE_FOLIAGE
} maage_generation_write_rule;
int maage_generation_surface(maage_world_seed seed,
    const maage_biome_registry *biomes, int x, int z, int *height, maage_biome_id *biome);
unsigned long maage_generation_feature_hash(maage_world_seed seed, int x, int z,
    unsigned long salt);
unsigned long maage_generation_feature_next(unsigned long *random);
int maage_generation_region(int coordinate, int edge);
/* -1 allocation failure, zero clipped/rejected/unchanged, one changed cell. */
int maage_generation_write(maage_generation_context *context, int x, int y, int z,
    maage_block_state_id state, maage_generation_write_rule rule);
maage_status maage_generation_outcome(maage_generation_context *context, unsigned char code, unsigned char operation);
int maage_generation_features_valid(const maage_block_registry *blocks);
maage_status maage_generation_carve(maage_generation_context *context);
maage_status maage_generation_underground(maage_generation_context *context);
maage_status maage_generation_decorate(maage_generation_context *context);
maage_status maage_generation_vegetation(maage_generation_context *context);
maage_status maage_generation_structures(maage_generation_context *context);
int maage_generation_spawn_clear(maage_world_seed seed, const maage_biome_registry *biomes,
    const maage_generation_policy *policy, int x, int z);
int maage_generation_spawn_floor(maage_world_seed seed, const maage_biome_registry *biomes,
    const maage_generation_policy *policy, int x, int z, double *feet_y);
/* Neighbors are -X,+X,-Z,+Z, read-only. Newly needed air sections transfer via
 * added, including on failure; attach to the private result or destroy them. */
/* All inputs are pinned immutable snapshots. Client retains copied CPU/GPU
 * geometry, never borrowed columns. Cancellation leaves partial output private. */
maage_status maage_world_mesh_create_chunk_job(const maage_block_registry *blocks,
    const maage_biome_registry *biomes, const maage_chunk *chunk,
    const maage_chunk *neighbors[8], maage_leaves_mode mode,
    const maage_job_token *token, maage_world_mesh **output, maage_status_trace *trace);
maage_status maage_light_chunk_job(maage_chunk *chunk, const maage_block_registry *blocks,
    const maage_chunk *neighbors[4], maage_section *added[16],
    const maage_job_token *token, maage_status_trace *trace);
#endif
