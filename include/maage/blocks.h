#ifndef MAAGE_BLOCKS_H
#define MAAGE_BLOCKS_H

#include <stddef.h>
#include "maage/content_ids.h"
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

typedef struct maage_block_registry maage_block_registry;

enum {
    MAAGE_LEAF_SPECIES_MASK = 3U,
    MAAGE_LEAF_PERSISTENT = 4U,
    MAAGE_LEAF_CHECK_DECAY = 8U
};
/* Oak/spruce/birch, with all four flag combinations. Direct numeric mapping;
 * unsupported species/metadata fail explicitly. */
maage_status maage_block_leaves_state(const maage_block_registry *registry, unsigned int metadata, maage_block_state_id *state);
/* Species 0..2 plus stage bit 8; unsupported metadata fails explicitly. */
maage_status maage_block_sapling_state(const maage_block_registry *registry, unsigned int metadata, maage_block_state_id *state);
enum {
    MAAGE_BLOCK_MESH_CUBE = 0,
    MAAGE_BLOCK_MESH_CROSS = 1,
    MAAGE_BLOCK_MESH_TORCH = 2
};
enum {
    MAAGE_FLUID_NONE = 0,
    MAAGE_FLUID_WATER = 1,
    MAAGE_FLUID_LAVA = 2
};

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

struct maage_world;
struct maage_random_tick_context;
struct maage_block_state_definition;
typedef maage_status (*maage_block_random_tick_callback)(struct maage_world *world, int x, int y, int z, const struct maage_block_state_definition *state, struct maage_random_tick_context *context);

typedef maage_status (*maage_block_survival_callback)(const struct maage_world *world, int x, int y, int z, const struct maage_block_state_definition *state, int *valid);

typedef struct maage_block_definition {
    const maage_builtin_ids *builtins; /* Registry-owned immutable references. */
    maage_block_id id;
    int solid;
    int opaque;
    int falls;                   /* Scheduled unsupported-block motion, independent of plant survival. */
    int material_solid;          /* Plant adjacency rule; independent of collision. */
    unsigned int emitted_light;  /* 0..15; resolved during registration. */
    unsigned int light_opacity;  /* 0..15; independent of face occlusion. */
    int use_neighbor_brightness; /* Combined light uses up and four sides. */
    int selectable, replaceable;
    unsigned int mesh_shape; /* Numeric cube, crossed plant or narrow torch. */
    double hardness;         /* Negative: unbreakable in Survival. */
    unsigned int required_tool, required_tool_level;
    int harvest_requires_tool;  /* Effectiveness and harvest eligibility differ. */
    unsigned long drop_item_id; /* Independent item domain; bound before finalize. */
    unsigned int drop_count;
    maage_block_shape collision_shape, selection_shape;
    unsigned int contact_damage;
    maage_block_shape contact_shape; /* Damaging shell may differ from physical collision. */
    /* Called only on the World owner for eligible states. NULL retains sampling
     * eligibility without behavior when a required system is unimplemented. */
    maage_block_random_tick_callback random_tick;
    maage_block_survival_callback survives;
} maage_block_definition;

typedef struct maage_block_state_definition {
    const maage_builtin_ids *builtins;
    maage_block_state_id id;
    maage_block_id block_id;
    /* Numeric property payload; no text at runtime. */
    unsigned int metadata;
    int random_ticks; /* Section eligibility, independent of callback support. */
    /* Optional selection override; all-zero keeps the type's bounds. */
    maage_block_shape selection_shape;
    unsigned int fluid_kind, fluid_level;
    maage_block_state_id fluid_source_state, fluid_states[16];
    maage_block_face_definition faces[MAAGE_FACE_COUNT];
    /* Optional pre-resolved opaque leaf texture; zero uses ordinary UVs. */
    float leaf_fast_uv[4];
} maage_block_state_definition;

/* Capacities are initial expected-registration-count hints (zero selects a small
 * default). Sparse paged numeric indexing allocates only occupied ID paths;
 * a high numeric ID never forces allocations up to that ID.
 *
 * The caller owns the registry. Definitions are borrowed until its destruction;
 * after finalize they are immutable. Registration copies names and definitions,
 * ignores incoming definition.id and returns the allocated ID in that field.
 * Block and state sequences are independent; air is resolved during registration
 * and implicit empty sections retain its actual assigned state. */
maage_status maage_block_registry_create(size_t block_capacity, size_t state_capacity, maage_block_registry **output);
maage_status maage_block_registry_destroy(maage_block_registry *registry);
maage_status maage_block_registry_register_block(maage_block_registry *registry, const char *external_name, maage_block_definition *definition);
maage_status maage_block_registry_register_state(maage_block_registry *registry, maage_block_state_definition *definition);
maage_status maage_block_registry_finalize(maage_block_registry *registry);
int maage_block_registry_is_finalized(const maage_block_registry *registry);
size_t maage_block_registry_block_count(const maage_block_registry *registry);
size_t maage_block_registry_state_count(const maage_block_registry *registry);
const maage_builtin_ids *maage_block_registry_builtin_ids(const maage_block_registry *registry);

/* Registration / explicit import boundary only. Open-addressed hashing gives
 * expected O(1) lookup with exact collision checks, not worst-case O(1). */
maage_status maage_block_registry_resolve_name(const maage_block_registry *registry, const char *external_name, maage_block_id *block_id);

/* Deterministic O(1), allocation-free runtime access. Unregistered/out-of-range
 * IDs return NULL; they never alias air or another valid definition. */
const maage_block_definition *maage_block_registry_block(const maage_block_registry *registry, maage_block_id id);
const maage_block_state_definition *maage_block_registry_state(const maage_block_registry *registry, maage_block_state_id id);
/* Pure numeric fluid observations. Neighbors use -X,+X,-Z,+Z order; NULL
 * means unavailable, never air. Flow is a unit vector (or zero), including
 * downward wall currents for falling fluid. Borrowed definitions stay immutable. */
double maage_block_fluid_height(const maage_block_state_definition *state);
void maage_block_fluid_flow(const maage_block_registry *registry, const maage_block_state_definition *state, const maage_block_state_definition *sides[4], const maage_block_state_definition *below[4], const maage_block_state_definition *above[4], double flow[3]);
/* Stable names and (block name, metadata) are persistence/import identities.
 * These functions are for registration and file I/O, never gameplay loops.
 * Within one block, state metadata must be unique. */
const char *maage_block_registry_block_name(const maage_block_registry *registry, maage_block_id id);
maage_status maage_block_registry_resolve_state(const maage_block_registry *registry, maage_block_id block, unsigned int metadata, maage_block_state_id *id);

#ifdef __cplusplus
}
#endif
#endif
