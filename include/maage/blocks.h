#ifndef MAAGE_BLOCKS_H
#define MAAGE_BLOCKS_H

#include <stddef.h>
#include "maage/content_ids.h"
#include "maage/fluids.h"
#include "maage/sounds.h"
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
typedef unsigned long maage_block_geometry_id;
struct maage_block_geometry_registry;
struct maage_container_registry;

typedef struct maage_block_registry maage_block_registry;

enum {
    MAAGE_BLOCK_MESH_CUBE = 0,
    MAAGE_BLOCK_MESH_CROSS = 1,
    MAAGE_BLOCK_MESH_TORCH = 2
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
    maage_texture_id overlay_texture, snow_texture;
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
    int has_flow_texture;
    maage_texture_id flow_texture;
} maage_block_face_definition;

/* Local voxel coordinates; selection and collision may differ. Empty shapes
 * are permitted only when the corresponding behavior is disabled. */
typedef struct maage_block_shape {
    double minimum[3], maximum[3];
} maage_block_shape;

struct maage_block_entity_handler;
struct maage_chunk;
struct maage_world_seed;
struct maage_biome_registry;
struct maage_generation_policy;
struct maage_job_token;
struct maage_item_registry;
struct maage_item_definition;
struct maage_item_drops;
struct maage_crafting_container;
struct maage_gameplay;
struct maage_world_target;
struct maage_world;
struct maage_random_tick_context;
struct maage_block_state_definition;
typedef maage_status (*maage_block_random_tick_callback)(struct maage_world *world, int x, int y, int z, const struct maage_block_state_definition *state, struct maage_random_tick_context *context);

typedef maage_status (*maage_block_survival_callback)(const struct maage_world *world, int x, int y, int z, const struct maage_block_state_definition *state, int *valid);

typedef struct maage_block_definition {
    const void *behavior_context; /* Borrowed immutable module data; outlives registry readers. */
    maage_block_id id;
    int solid;
    int opaque;
    int falls;                   /* Scheduled unsupported-block motion, independent of plant survival. */
    int material_solid;          /* Plant adjacency rule; independent of collision. */
    unsigned int emitted_light;  /* 0..15; resolved during registration. */
    unsigned int light_opacity;  /* 0..15; independent of face occlusion. */
    int use_neighbor_brightness; /* Combined light uses up and four sides. */
    int selectable, replaceable;
    maage_block_geometry_id geometry_id; /* Registered geometry; resolved before finalization. */
    unsigned int mesh_shape;             /* Cached optimized primitive tag from the geometry registry. */
    double hardness;                     /* Negative: unbreakable in Survival. */
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
    maage_status (*scheduled_tick)(struct maage_world *, int, int, int, const struct maage_block_state_definition *);
    /* Content callbacks run on the owner, except import_state (detached worker).
     * Read-only placement/presentation callbacks never mutate World. */
    maage_status (*drops)(const maage_block_registry *, const struct maage_item_registry *, const struct maage_block_state_definition *, const struct maage_item_definition *, unsigned long *, struct maage_item_drops *);
    maage_status (*use)(struct maage_world *, const struct maage_world_target *, struct maage_crafting_container *);
    maage_status (*land)(struct maage_world *, int, int, int, const struct maage_block_state_definition *, double, unsigned long *);
    maage_status (*changed)(struct maage_world *, int, int, int, const struct maage_block_state_definition *, const struct maage_block_state_definition *);
    maage_status (*import_state)(const maage_block_registry *, const struct maage_block_state_definition *, maage_block_state_id *);
    maage_sound_material_id material;
    unsigned int mesh_material, foliage, snow_cover, flow_permeable;
    unsigned int mesh_inset, cull_same;
    unsigned int tool_wear_class, special_tool_class;
    double special_tool_speed, item_drag;
    int unsupported_no_drops, unsupported_replaces;
    maage_block_state_id unsupported_state;
    const struct maage_block_entity_handler *entity;

} maage_block_definition;

typedef struct maage_block_state_definition {
    maage_block_state_id id;
    maage_block_id block_id;
    /* Numeric property payload; no text at runtime. */
    unsigned int metadata;
    int geometry_grouped;      /* Explicit opt-in; ordinary states always invalidate geometry. */
    unsigned int geometry_key; /* Equal keys preserve geometry epochs only when physical bounds also agree. */
    unsigned int texture_axis; /* 0 = ordinary; 1..3 = X/Y/Z. */
    unsigned int presentation_effect;
    maage_block_shape mesh_bounds;

    int random_ticks; /* Section eligibility, independent of callback support. */
    /* Optional selection override; all-zero keeps the type's bounds. */
    maage_block_shape selection_shape;
    /* Explicit physical override; all-zero bounds disable collision for this
     * state. Without the flag, the type supplies its ordinary collision. */
    int collision_override;
    maage_block_shape collision_shape;
    maage_fluid_id fluid_kind;
    unsigned int fluid_level;
    const maage_fluid_definition *fluid_definition; /* Finalize-resolved immutable numeric dispatch. */
    maage_block_face_definition faces[MAAGE_FACE_COUNT];
    /* Optional pre-resolved opaque leaf texture; zero uses ordinary UVs. */
    maage_texture_id fast_texture;
    float leaf_fast_uv[4];
} maage_block_state_definition;

/* Pure validation/lookup services shared by registered appearances and World.
 * Collision returns zero for empty bounds; shape may be NULL for a predicate. */
int maage_block_face_definition_valid(const maage_block_face_definition *face);
int maage_block_collision_shape(const maage_block_definition *block, const maage_block_state_definition *state, maage_block_shape *shape);

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
/* Logical identity behind an immutable Client presentation view. */
const maage_block_registry *maage_block_registry_identity_source(const maage_block_registry *registry);
maage_status maage_block_registry_destroy(maage_block_registry *registry);
maage_status maage_block_registry_register_block(maage_block_registry *registry, const char *external_name, maage_block_definition *definition);
maage_status maage_block_registry_register_state(maage_block_registry *registry, maage_block_state_definition *definition);
/* Borrowed registries outlive finalized block definitions and their users. */
maage_status maage_block_registry_bind_sounds(maage_block_registry *registry, const maage_sound_registry *sounds);
const maage_sound_registry *maage_block_registry_sounds(const maage_block_registry *registry);
maage_status maage_block_registry_bind_containers(maage_block_registry *registry, const struct maage_container_registry *containers);
const struct maage_container_registry *maage_block_registry_containers(const maage_block_registry *registry);
maage_status maage_block_registry_bind_fluids(maage_block_registry *registry, const maage_fluid_registry *fluids);
const maage_fluid_registry *maage_block_registry_fluids(const maage_block_registry *registry);
maage_status maage_block_registry_bind_geometry(maage_block_registry *registry, const struct maage_block_geometry_registry *geometry);
const struct maage_block_geometry_registry *maage_block_registry_geometry(const maage_block_registry *registry);
maage_status maage_block_registry_finalize(maage_block_registry *registry);
int maage_block_registry_is_finalized(const maage_block_registry *registry);
size_t maage_block_registry_block_count(const maage_block_registry *registry);
size_t maage_block_registry_state_count(const maage_block_registry *registry);
/* Explicit engine-required bindings. No names or namespace receives special treatment. */
typedef struct maage_registry_bindings {
    maage_block_id block_air;
    maage_block_state_id state_air, state_boundary, state_ui_background;
    const void *generation_context;
    maage_status (*generate)(struct maage_chunk *chunk, struct maage_world_seed seed, const maage_block_registry *blocks, const struct maage_biome_registry *biomes, const struct maage_generation_policy *policy, const struct maage_job_token *token, maage_status_trace *trace);
    maage_status (*spawn)(struct maage_world_seed seed, const maage_block_registry *blocks, const struct maage_biome_registry *biomes, const struct maage_generation_policy *policy, double feet[3]);
} maage_registry_bindings;
maage_status maage_block_registry_bind(maage_block_registry *registry, const maage_registry_bindings *bindings);
const maage_registry_bindings *maage_block_registry_bindings(const maage_block_registry *registry);
/* Registration-only replacement copies a complete definition and retains its ID. */
maage_status maage_block_registry_update_block(maage_block_registry *registry, const maage_block_definition *definition);
maage_status maage_block_registry_update_state(maage_block_registry *registry, const maage_block_state_definition *definition);

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
