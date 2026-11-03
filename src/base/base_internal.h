#ifndef MAAGE_BASE_INTERNAL_H
#define MAAGE_BASE_INTERNAL_H
#include "maage/base.h"
#include "base_ids.h"
#include "maage/furnace.h"
#include "maage/generation.h"
#include "maage/environment.h"
#include "maage/random_ticks.h"
#define MAAGE_BASE_ID(ids, offset) (*(const unsigned long *) ((const unsigned char *) (ids) + (offset)))
maage_status maage_content_register_state(maage_block_registry *, const maage_base_ids *, const maage_content_config *, maage_block_state_id *, maage_block_id, unsigned int, int);
maage_status maage_furnace_content_blocks(maage_block_registry *, maage_base_ids *, const maage_content_config *);
maage_status maage_furnace_content_items(maage_content_context *, maage_base_ids *);
maage_status maage_base_blocks(maage_content_context *, maage_base_ids *);
maage_status maage_base_creative(maage_content_context *, const maage_base_ids *);
maage_status maage_base_items(maage_content_context *, maage_base_ids *);
maage_status maage_base_chest_models(maage_content_context *, const maage_base_ids *);
maage_status maage_base_chest_models_bind(maage_content_context *, const maage_base_ids *);
maage_status maage_base_entity_models(maage_content_context *, maage_base_ids *);
maage_status maage_base_entities(maage_content_context *, maage_base_ids *);
maage_status maage_base_entity_creative(maage_content_context *, const maage_base_ids *);
maage_status maage_base_bind(maage_content_context *, maage_base_ids *);
maage_status maage_base_recipes(maage_content_context *, const maage_base_ids *);
maage_status maage_base_smelting(maage_content_context *, const maage_base_ids *);
maage_status maage_base_generate(maage_chunk *, maage_world_seed, const maage_block_registry *, const maage_biome_registry *, const maage_generation_policy *, const maage_job_token *, maage_status_trace *);
maage_status maage_base_spawn(maage_world_seed, const maage_block_registry *, const maage_biome_registry *, const maage_generation_policy *, double[3]);
maage_status base_item_register(maage_content_context *, const char *, maage_item_definition *, unsigned long, maage_item_id *);
maage_status base_block_item(maage_content_context *, const char *, const char *, maage_block_id, maage_block_state_id, unsigned int, maage_item_placement, unsigned long, maage_item_id *);
maage_status base_tool(maage_content_context *, const char *, const char *, unsigned int, unsigned int, unsigned int, double, unsigned int, unsigned long, maage_item_id *);
maage_status base_smelt(maage_smelting_registry *, maage_item_id, unsigned int, maage_item_id, unsigned int, double);
maage_status maage_base_seed_placement(const maage_item_definition *, const maage_world *, const maage_world_target *, maage_block_state_id *, int *);
maage_status maage_base_hoe_placement(const maage_item_definition *, const maage_world *, const maage_world_target *, maage_block_state_id *, int *);

maage_status maage_random_tick_leaves(maage_world *world, int x, int y, int z, const maage_block_state_definition *state, maage_random_tick_context *context);
maage_status maage_random_tick_grass(maage_world *world, int x, int y, int z, const maage_block_state_definition *state, maage_random_tick_context *context);
maage_status maage_random_tick_farmland(maage_world *world, int x, int y, int z, const maage_block_state_definition *state, maage_random_tick_context *context);
maage_status maage_random_tick_wheat(maage_world *world, int x, int y, int z, const maage_block_state_definition *state, maage_random_tick_context *context);
maage_status maage_random_tick_cactus(maage_world *world, int x, int y, int z, const maage_block_state_definition *state, maage_random_tick_context *context);
maage_status maage_random_tick_mushroom(maage_world *world, int x, int y, int z, const maage_block_state_definition *state, maage_random_tick_context *context);
maage_status maage_random_tick_sapling(maage_world *world, int x, int y, int z, const maage_block_state_definition *state, maage_random_tick_context *context);
maage_status maage_random_tick_bush(maage_world *world, int x, int y, int z, const maage_block_state_definition *state, maage_random_tick_context *context);
maage_status maage_random_tick_snow(maage_world *world, int x, int y, int z, const maage_block_state_definition *state, maage_random_tick_context *context);
maage_status maage_random_tick_ice(maage_world *world, int x, int y, int z, const maage_block_state_definition *state, maage_random_tick_context *context);
maage_status maage_leaves_changed(maage_world *world, int x, int y, int z, const maage_block_state_definition *before, const maage_block_state_definition *after);
maage_status maage_support_plant(const maage_world *, int, int, int, const maage_block_state_definition *, int *);
maage_status maage_support_mushroom(const maage_world *, int, int, int, const maage_block_state_definition *, int *);
maage_status maage_support_dead_bush(const maage_world *, int, int, int, const maage_block_state_definition *, int *);
maage_status maage_support_torch(const maage_world *, int, int, int, const maage_block_state_definition *, int *);
maage_status maage_support_cactus(const maage_world *, int, int, int, const maage_block_state_definition *, int *);
maage_status maage_support_snow(const maage_world *, int, int, int, const maage_block_state_definition *, int *);
maage_status maage_support_sapling(const maage_world *, int, int, int, const maage_block_state_definition *, int *);
maage_status maage_support_farmland(const maage_world *, int, int, int, const maage_block_state_definition *, int *);
maage_status maage_support_wheat(const maage_world *, int, int, int, const maage_block_state_definition *, int *);
maage_block_state_id maage_base_item_icon(const maage_item_definition *, unsigned int);
maage_texture_id maage_base_item_texture(const maage_item_definition *, unsigned int);
const char *maage_base_item_name(const maage_item_definition *, unsigned int);
maage_status maage_base_resolve_blocks(maage_content_context *, const maage_base_ids *);
maage_status maage_base_table_use(maage_world *, const maage_world_target *, maage_crafting_container *);
maage_status maage_base_furnace_use(maage_world *, const maage_world_target *, maage_crafting_container *);
int maage_base_table_access(const maage_base_ids *, const maage_world *, const maage_crafting_container *);
int maage_base_furnace_access(const maage_world *, const maage_crafting_container *);
maage_status maage_base_farmland_land(maage_world *, int, int, int, const maage_block_state_definition *, double, unsigned long *);
maage_status maage_base_leaf_import(const maage_block_registry *, const maage_block_state_definition *, maage_block_state_id *);
maage_status maage_base_furnace_entity(maage_content_context *, const maage_base_ids *);
maage_status maage_base_containers(maage_content_context *, maage_base_ids *);
maage_status maage_base_sounds(maage_content_context *, maage_base_ids *);
maage_status maage_base_geometry(maage_content_context *, maage_base_ids *);
maage_status maage_base_fluids_register(maage_content_context *, maage_base_ids *);
maage_status maage_base_fluids_resolve(maage_content_context *, const maage_base_ids *);
maage_status maage_base_textures(maage_content_context *, maage_base_ids *);
maage_texture_id maage_base_logical_texture(const maage_base_ids *, maage_texture_id);
#define MAAGE_GENERATION_REGION_CACHE_CAPACITY 64
typedef struct maage_generation_region_seed {
    int x, z, valid, supported;
    maage_biome_id biome, climate, supported_biome, supported_climate;
    double site_x, site_z;
} maage_generation_region_seed;
/* Zero-initialize for one seed/job. Eviction changes cost only; each entry is
 * reproducible from coordinates, with no neighboring chunk dependency. */
typedef struct maage_generation_region_cache {
    maage_generation_region_seed entries[MAAGE_GENERATION_REGION_CACHE_CAPACITY];
} maage_generation_region_cache;
typedef struct maage_generation_sample {
    int height;
    maage_biome_id biome;
    maage_block_state_id surface, filler;
    double temperature, rainfall, lapse_offset;
    maage_base_biome_decoration decoration;
} maage_generation_sample;
typedef struct maage_generation_context {
    const maage_base_ids *ids;
    maage_chunk *chunk;
    maage_generation_output output;
    maage_world_seed seed;
    const maage_block_registry *blocks;
    const maage_biome_registry *biomes;
    const maage_generation_policy *policy;
    const maage_job_token *token;
    maage_status_trace *trace;
    maage_status failure;
    const maage_generation_sample *columns; /* Borrowed job-local cache of 256 profiles; NULL for spawn queries. */
    maage_generation_region_cache *regions; /* Borrowed job-local regional memoization. */
} maage_generation_context;
enum {
    MAAGE_STRUCTURE_DUNGEON = 1
};
typedef enum maage_generation_write_rule {
    MAAGE_GENERATION_WRITE_ANY = 0,
    MAAGE_GENERATION_WRITE_NATURAL,
    MAAGE_GENERATION_WRITE_STONE,
    MAAGE_GENERATION_WRITE_AIR,
    MAAGE_GENERATION_WRITE_FOLIAGE
} maage_generation_write_rule;
int maage_generation_surface(const maage_base_ids *ids, maage_world_seed seed, const maage_biome_registry *biomes, int x, int z, int *height, maage_biome_id *biome, maage_generation_region_cache *regions);
/* Same supported discrete region and unchanged height function as surface().
 * Ecology uses compact influences near those region boundaries. Optional cache
 * belongs to this seed/job; cache misses and halo/spawn queries are equivalent. */
int maage_generation_sample_at(const maage_base_ids *ids, maage_world_seed seed, const maage_biome_registry *biomes, const maage_generation_policy *policy, int x, int z, maage_generation_sample *sample, maage_generation_region_cache *regions);
int maage_generation_cold(const maage_generation_sample *sample, int y);
unsigned long maage_generation_feature_hash(maage_world_seed seed, int x, int z, unsigned long salt);
unsigned long maage_generation_feature_next(unsigned long *random);
int maage_generation_region(int coordinate, int edge);
/* -1 allocation failure, zero clipped/rejected/unchanged, one changed cell. */
int maage_generation_write(maage_generation_context *context, int x, int y, int z, maage_block_state_id state, maage_generation_write_rule rule);
maage_status maage_generation_outcome(maage_generation_context *context, unsigned char code, unsigned char operation);
int maage_generation_features_valid(const maage_base_ids *ids, const maage_block_registry *blocks);
maage_status maage_generation_carve(maage_generation_context *context);
maage_status maage_generation_underground(maage_generation_context *context);
maage_status maage_generation_decorate(maage_generation_context *context);
maage_status maage_generation_vegetation(maage_generation_context *context);
maage_status maage_generation_structures(maage_generation_context *context);
int maage_generation_spawn_clear(const maage_base_ids *ids, maage_world_seed seed, const maage_block_registry *blocks, const maage_biome_registry *biomes, const maage_generation_policy *policy, int x, int z, maage_generation_region_cache *regions);
int maage_generation_spawn_floor(const maage_base_ids *ids, maage_world_seed seed, const maage_block_registry *blocks, const maage_biome_registry *biomes, const maage_generation_policy *policy, int x, int z, double *feet_y, maage_generation_region_cache *regions);

maage_status maage_generation_grow_tree(const maage_base_ids *ids, maage_world *, int, int, int, unsigned int, int, unsigned long, int *);
maage_status maage_base_generation_register(maage_content_context *, const maage_base_ids *);
maage_status maage_base_drop_wheat(const maage_block_registry *, const maage_item_registry *, const maage_block_state_definition *, const maage_item_definition *, unsigned long *, maage_item_drops *);
maage_status maage_base_drop_redstone(const maage_block_registry *, const maage_item_registry *, const maage_block_state_definition *, const maage_item_definition *, unsigned long *, maage_item_drops *);
maage_status maage_base_drop_lapis(const maage_block_registry *, const maage_item_registry *, const maage_block_state_definition *, const maage_item_definition *, unsigned long *, maage_item_drops *);
maage_status maage_base_drop_gravel(const maage_block_registry *, const maage_item_registry *, const maage_block_state_definition *, const maage_item_definition *, unsigned long *, maage_item_drops *);
maage_status maage_base_drop_species(const maage_block_registry *, const maage_item_registry *, const maage_block_state_definition *, const maage_item_definition *, unsigned long *, maage_item_drops *);
maage_status maage_base_drop_planks(const maage_block_registry *, const maage_item_registry *, const maage_block_state_definition *, const maage_item_definition *, unsigned long *, maage_item_drops *);
maage_status maage_base_drop_leaves(const maage_block_registry *, const maage_item_registry *, const maage_block_state_definition *, const maage_item_definition *, unsigned long *, maage_item_drops *);
maage_status maage_base_drop_grass(const maage_block_registry *, const maage_item_registry *, const maage_block_state_definition *, const maage_item_definition *, unsigned long *, maage_item_drops *);
maage_status maage_base_drop_bush(const maage_block_registry *, const maage_item_registry *, const maage_block_state_definition *, const maage_item_definition *, unsigned long *, maage_item_drops *);
maage_status maage_base_place_sand(const maage_item_definition *, unsigned int, maage_block_face, double, maage_block_state_id *);
maage_status maage_base_place_planks(const maage_item_definition *, unsigned int, maage_block_face, double, maage_block_state_id *);
maage_status maage_base_place_log(const maage_item_definition *, unsigned int, maage_block_face, double, maage_block_state_id *);
maage_status maage_base_place_log2(const maage_item_definition *, unsigned int, maage_block_face, double, maage_block_state_id *);
maage_status maage_base_place_leaves(const maage_item_definition *, unsigned int, maage_block_face, double, maage_block_state_id *);
maage_status maage_base_place_sapling(const maage_item_definition *, unsigned int, maage_block_face, double, maage_block_state_id *);
maage_status maage_base_place_torch(const maage_item_definition *, unsigned int, maage_block_face, double, maage_block_state_id *);
maage_status maage_base_furnace_action(maage_world *, struct maage_gameplay *, unsigned long, unsigned int, unsigned int, unsigned int);
maage_status maage_base_furnace_sample(const maage_world *, maage_crafting_container *);
maage_status maage_base_biomes(maage_biome_registry *, maage_base_ids *);
#endif
