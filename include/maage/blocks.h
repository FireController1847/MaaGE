#ifndef MAAGE_BLOCKS_H
#define MAAGE_BLOCKS_H

#include <stddef.h>
#include "maage/status.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Separate numeric domains: a state selects a type plus its resolved properties.
 * unsigned long provides at least 32 bits in ISO C90; registries accept
 * the portable 32-bit ID domain. Persistence defines a versioned stable mapping. */
typedef unsigned long maage_block_id;
typedef unsigned long maage_block_state_id;
typedef unsigned long maage_texture_id;

enum {
    MAAGE_BLOCK_AIR = 0,
    MAAGE_BLOCK_STONE = 1,
    MAAGE_BLOCK_GRASS = 2,
    MAAGE_BLOCK_DIRT = 3,
    MAAGE_BLOCK_COBBLESTONE = 4,
    MAAGE_BLOCK_PLANKS = 5,
    MAAGE_BLOCK_BEDROCK = 7,
    MAAGE_BLOCK_WATER = 9,
    MAAGE_BLOCK_LAVA = 11,
    MAAGE_BLOCK_SAND = 12,
    MAAGE_BLOCK_GRAVEL = 13,
    MAAGE_BLOCK_GOLD_ORE = 14,
    MAAGE_BLOCK_IRON_ORE = 15,
    MAAGE_BLOCK_COAL_ORE = 16,
    MAAGE_BLOCK_LOG = 17,
    MAAGE_BLOCK_LEAVES = 18,
    MAAGE_BLOCK_LAPIS_ORE = 21,
    MAAGE_BLOCK_SANDSTONE = 24,
    MAAGE_BLOCK_TALL_GRASS = 31,
    MAAGE_BLOCK_DEAD_BUSH = 32,
    MAAGE_BLOCK_DANDELION = 37,
    MAAGE_BLOCK_POPPY = 38,
    MAAGE_BLOCK_BROWN_MUSHROOM = 39,
    MAAGE_BLOCK_RED_MUSHROOM = 40,
    MAAGE_BLOCK_MOSSY_COBBLESTONE = 48,
    MAAGE_BLOCK_OBSIDIAN = 49,
    MAAGE_BLOCK_TORCH = 50,
    MAAGE_BLOCK_CRAFTING_TABLE = 58,
    MAAGE_BLOCK_GOLD_BLOCK = 41, MAAGE_BLOCK_IRON_BLOCK = 42,
    MAAGE_BLOCK_DIAMOND_BLOCK = 57, MAAGE_BLOCK_LAPIS_BLOCK = 22,
    MAAGE_BLOCK_EMERALD_BLOCK = 133, MAAGE_BLOCK_REDSTONE_BLOCK = 152,
    MAAGE_BLOCK_COAL_BLOCK = 173, MAAGE_BLOCK_SNOW_BLOCK = 80,
    MAAGE_BLOCK_DIAMOND_ORE = 56,
    MAAGE_BLOCK_REDSTONE_ORE = 73,
    MAAGE_BLOCK_SNOW = 78,
    MAAGE_BLOCK_ICE = 79,
    MAAGE_BLOCK_CACTUS = 81,
    MAAGE_BLOCK_EMERALD_ORE = 129
};
enum {
    MAAGE_STATE_AIR = 0,
    MAAGE_STATE_STONE = 1,
    MAAGE_STATE_GRASS = 2,
    MAAGE_STATE_DIRT = 3,
    MAAGE_STATE_COBBLESTONE = 4,
    MAAGE_STATE_PLANKS = 5,
    MAAGE_STATE_BEDROCK = 7,
    MAAGE_STATE_WATER = 9,
    MAAGE_STATE_LAVA = 11,
    MAAGE_STATE_SAND = 12,
    MAAGE_STATE_GRAVEL = 13,
    MAAGE_STATE_GOLD_ORE = 14,
    MAAGE_STATE_IRON_ORE = 15,
    MAAGE_STATE_COAL_ORE = 16,
    MAAGE_STATE_LOG = 17,
    MAAGE_STATE_LEAVES = 18,
    MAAGE_STATE_LAPIS_ORE = 21,
    MAAGE_STATE_SANDSTONE = 24,
    MAAGE_STATE_TALL_GRASS = 31,
    MAAGE_STATE_DEAD_BUSH = 32,
    MAAGE_STATE_DANDELION = 37,
    MAAGE_STATE_POPPY = 38,
    MAAGE_STATE_BROWN_MUSHROOM = 39,
    MAAGE_STATE_RED_MUSHROOM = 40,
    MAAGE_STATE_MOSSY_COBBLESTONE = 48,
    MAAGE_STATE_OBSIDIAN = 49,
    MAAGE_STATE_TORCH = 50,
    MAAGE_STATE_CRAFTING_TABLE = 58,
    MAAGE_STATE_DIAMOND_ORE = 56,
    MAAGE_STATE_REDSTONE_ORE = 73,
    MAAGE_STATE_SNOW = 78,
    MAAGE_STATE_ICE = 79,
    MAAGE_STATE_CACTUS = 81,
    MAAGE_STATE_EMERALD_ORE = 129,
    MAAGE_STATE_SPRUCE_LOG = 272, MAAGE_STATE_BIRCH_LOG = 273,
    MAAGE_STATE_SPRUCE_LEAVES = 274, MAAGE_STATE_BIRCH_LEAVES = 275,
    MAAGE_STATE_TORCH_NEGATIVE_X = 276, MAAGE_STATE_TORCH_POSITIVE_X = 277,
    MAAGE_STATE_TORCH_NEGATIVE_Z = 278, MAAGE_STATE_TORCH_POSITIVE_Z = 279,
    MAAGE_STATE_LOG_X_BASE = 280, MAAGE_STATE_LOG_Z_BASE = 283,
    MAAGE_STATE_SPRUCE_PLANKS = 286, MAAGE_STATE_BIRCH_PLANKS = 287,
    MAAGE_STATE_WATER_FLOW_BASE = 512, MAAGE_STATE_LAVA_FLOW_BASE = 528
};
enum { MAAGE_BLOCK_MESH_CUBE = 0, MAAGE_BLOCK_MESH_CROSS = 1,
    MAAGE_BLOCK_MESH_TORCH = 2 };
enum { MAAGE_FLUID_NONE = 0, MAAGE_FLUID_WATER = 1, MAAGE_FLUID_LAVA = 2 };

typedef enum maage_block_face {
    MAAGE_FACE_NEGATIVE_X = 0,
    MAAGE_FACE_POSITIVE_X,
    MAAGE_FACE_NEGATIVE_Y,
    MAAGE_FACE_POSITIVE_Y,
    MAAGE_FACE_NEGATIVE_Z,
    MAAGE_FACE_POSITIVE_Z,
    MAAGE_FACE_COUNT
} maage_block_face;

enum {
    MAAGE_BLOCK_TINT_NONE = 0,
    MAAGE_BLOCK_TINT_GRASS = 1,
    MAAGE_BLOCK_TINT_FOLIAGE = 2,
    MAAGE_BLOCK_TINT_GRASS_LOCAL = 3
};

typedef struct maage_block_face_definition {
    maage_texture_id texture_id;
    /* Pre-resolved top-left-origin atlas rectangle: u0, v0, u1, v1. */
    float uv[4];
    float tint[4];
    /* Numeric biome color selection, applied once during immutable meshing.
     * Optional overlay uses its own atlas region; zero region means absent. */
    unsigned int tint_mode, overlay_tint_mode;
    float overlay_uv[4];
    float snow_uv[4];
    /* V stride, signed frame count (negative ping-pongs), ticks per frame. */
    float animation[3];
    float flow_uv[4], flow_animation[3];
} maage_block_face_definition;

/* Local voxel coordinates; selection and collision may differ. Empty shapes
 * are permitted only when the corresponding behavior is disabled. */
typedef struct maage_block_shape {
    double minimum[3], maximum[3];
} maage_block_shape;

typedef struct maage_block_definition {
    maage_block_id id;
    int solid;
    int opaque;
    unsigned int emitted_light; /* 0..15; resolved during registration. */
    unsigned int light_opacity; /* 0..15; independent of face occlusion. */
    int selectable, replaceable;
    unsigned int mesh_shape; /* Numeric cube, crossed plant or narrow torch. */
    double hardness; /* Negative: unbreakable in Survival. */
    unsigned int required_tool, required_tool_level;
    int harvest_requires_tool; /* Effectiveness and harvest eligibility differ. */
    unsigned long drop_item_id; /* Item domain, resolved at registration. */
    unsigned int drop_count;
    maage_block_shape collision_shape, selection_shape;
} maage_block_definition;

typedef struct maage_block_state_definition {
    maage_block_state_id id;
    maage_block_id block_id;
    /* Numeric property payload; no text at runtime. */
    unsigned int metadata;
    /* Optional selection override; all-zero keeps the type's bounds. */
    maage_block_shape selection_shape;
    unsigned int fluid_kind, fluid_level;
    maage_block_state_id fluid_source_state, fluid_states[16];
    maage_block_face_definition faces[MAAGE_FACE_COUNT];
    /* Optional pre-resolved opaque leaf texture; zero uses ordinary UVs. */
    float leaf_fast_uv[4];
} maage_block_state_definition;

typedef struct maage_block_registry maage_block_registry;

/* Capacities are initial expected-registration-count hints (zero selects a small
 * default). Sparse paged numeric indexing allocates only occupied ID paths;
 * a high numeric ID never forces allocations up to that ID.
 *
 * The caller owns the registry. Definitions are borrowed until its destruction;
 * after finalize they are immutable. Registration copies names and definitions. */
maage_status maage_block_registry_create(size_t block_capacity,
    size_t state_capacity, maage_block_registry **output);
maage_status maage_block_registry_destroy(maage_block_registry *registry);
maage_status maage_block_registry_register_block(
    maage_block_registry *registry, const char *external_name,
    const maage_block_definition *definition);
maage_status maage_block_registry_register_state(
    maage_block_registry *registry,
    const maage_block_state_definition *definition);
maage_status maage_block_registry_finalize(
    maage_block_registry *registry);
int maage_block_registry_is_finalized(const maage_block_registry *registry);
size_t maage_block_registry_block_count(const maage_block_registry *registry);
size_t maage_block_registry_state_count(const maage_block_registry *registry);

/* Registration / explicit import boundary only. Open-addressed hashing gives
 * expected O(1) lookup with exact collision checks, not worst-case O(1). */
maage_status maage_block_registry_resolve_name(
    const maage_block_registry *registry, const char *external_name,
    maage_block_id *block_id);

/* Deterministic O(1), allocation-free runtime access. Unregistered/out-of-range
 * IDs return NULL; they never alias air or another valid definition. */
const maage_block_definition *maage_block_registry_block(
    const maage_block_registry *registry, maage_block_id id);
const maage_block_state_definition *maage_block_registry_state(
    const maage_block_registry *registry, maage_block_state_id id);
/* Stable names and (block name, metadata) are persistence/import identities.
 * These functions are for registration and file I/O, never gameplay loops.
 * Within one block, state metadata must be unique. */
const char *maage_block_registry_block_name(
    const maage_block_registry *registry, maage_block_id id);
maage_status maage_block_registry_resolve_state(
    const maage_block_registry *registry, maage_block_id block,
    unsigned int metadata, maage_block_state_id *id);

#ifdef __cplusplus
}
#endif
#endif
