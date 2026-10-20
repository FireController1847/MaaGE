#ifndef MAAGE_BLOCKS_H
#define MAAGE_BLOCKS_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Separate numeric domains: a state selects a type plus its resolved properties.
 * unsigned long provides at least 32 bits in ISO C90; persistence defines a versioned stable mapping. */
typedef unsigned long maage_block_id;
typedef unsigned long maage_block_state_id;
typedef unsigned long maage_texture_id;

enum {
    MAAGE_BLOCK_AIR = 0,
    MAAGE_BLOCK_STONE = 1,
    MAAGE_BLOCK_GRASS = 2,
    MAAGE_BLOCK_DIRT = 3,
    MAAGE_BLOCK_BEDROCK = 7,
    MAAGE_BLOCK_WATER = 9,
    MAAGE_BLOCK_SAND = 12,
    MAAGE_BLOCK_GRAVEL = 13,
    MAAGE_BLOCK_SANDSTONE = 24
};
enum {
    MAAGE_STATE_AIR = 0,
    MAAGE_STATE_STONE = 1,
    MAAGE_STATE_GRASS = 2,
    MAAGE_STATE_DIRT = 3,
    MAAGE_STATE_BEDROCK = 7,
    MAAGE_STATE_WATER = 9,
    MAAGE_STATE_SAND = 12,
    MAAGE_STATE_GRAVEL = 13,
    MAAGE_STATE_SANDSTONE = 24
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

typedef struct maage_block_face_definition {
    maage_texture_id texture_id;
    /* Pre-resolved top-left-origin atlas rectangle: u0, v0, u1, v1. */
    float uv[4];
    float tint[4];
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
    int selectable, replaceable;
    double hardness; /* Negative: unbreakable in Survival. */
    unsigned int required_tool, required_tool_level;
    unsigned long drop_item_id; /* Item domain, resolved at registration. */
    unsigned int drop_count;
    maage_block_shape collision_shape, selection_shape;
} maage_block_definition;

typedef struct maage_block_state_definition {
    maage_block_state_id id;
    maage_block_id block_id;
    /* Legacy metadata-compatible numeric property payload; no text at runtime. */
    unsigned int metadata;
    maage_block_face_definition faces[MAAGE_FACE_COUNT];
} maage_block_state_definition;

typedef enum maage_registry_result {
    MAAGE_REGISTRY_OK = 0,
    MAAGE_REGISTRY_INVALID,
    MAAGE_REGISTRY_DUPLICATE,
    MAAGE_REGISTRY_FROZEN,
    MAAGE_REGISTRY_OUT_OF_MEMORY
} maage_registry_result;

typedef struct maage_block_registry maage_block_registry;

/* Capacities bound direct-indexed ID domains, not the number of occupied slots.
 * The caller owns the registry. Definitions are borrowed until its destruction;
 * after finalize they are immutable. Registration copies names and definitions. */
maage_block_registry *maage_block_registry_create(size_t block_capacity,
    size_t state_capacity);
void maage_block_registry_destroy(maage_block_registry *registry);
maage_registry_result maage_block_registry_register_block(
    maage_block_registry *registry, const char *external_name,
    const maage_block_definition *definition);
maage_registry_result maage_block_registry_register_state(
    maage_block_registry *registry,
    const maage_block_state_definition *definition);
maage_registry_result maage_block_registry_finalize(
    maage_block_registry *registry);
int maage_block_registry_is_finalized(const maage_block_registry *registry);
size_t maage_block_registry_block_count(const maage_block_registry *registry);
size_t maage_block_registry_state_count(const maage_block_registry *registry);

/* Registration / explicit import boundary only. Open-addressed hashing gives
 * expected O(1) lookup with exact collision checks, not worst-case O(1). */
maage_registry_result maage_block_registry_resolve_name(
    const maage_block_registry *registry, const char *external_name,
    maage_block_id *block_id);

/* Deterministic O(1), allocation-free runtime access. Unregistered/out-of-range
 * IDs return NULL; they never alias air or another valid definition. */
const maage_block_definition *maage_block_registry_block(
    const maage_block_registry *registry, maage_block_id id);
const maage_block_state_definition *maage_block_registry_state(
    const maage_block_registry *registry, maage_block_state_id id);

#ifdef __cplusplus
}
#endif
#endif
