#ifndef MAAGE_BLOCK_GEOMETRY_H
#define MAAGE_BLOCK_GEOMETRY_H
#include "maage/blocks.h"
#include "maage/geometry.h"

/* Optimized primitives are engine mechanisms, not registered content IDs.
 * CUSTOM providers emit arbitrary many quads without a new renderer or shader. */
#define MAAGE_BLOCK_GEOMETRY_CUSTOM 3U
#define MAAGE_BLOCK_GEOMETRY_INHERIT_MATERIAL MAAGE_CONTENT_ID_INVALID

typedef struct maage_block_geometry_registry maage_block_geometry_registry;
struct maage_block_geometry_definition;
typedef struct maage_block_geometry_query {
    const maage_block_registry *blocks;
    const maage_block_definition *block;
    const maage_block_state_definition *state;
    const struct maage_block_geometry_definition *geometry; /* Includes registry-owned resolved appearances. */
    int x, y, z, item;
    /* Relative offsets -1..1 use the immutable job halo; NULL means unavailable.
     * Item generation has no neighborhood and supplies a NULL sample callback. */
    const maage_block_state_definition *(*sample)(const void *, int, int, int);
    const void *sample_context;
} maage_block_geometry_query;
typedef struct maage_block_geometry_quad {
    float positions[4][3];  /* CCW, local voxel coordinates 0..1. */
    float uv[4][2];         /* Local coordinates within appearance->uv. */
    unsigned int face;      /* Cardinal lighting/culling direction. */
    unsigned long material; /* INHERIT uses the block's numeric surface mode. */
    int cull, double_sided;
    /* NULL selects state->faces[face]. Other appearances borrow resolved faces
     * from query.geometry->appearances or query.blocks through this emission. */
    const maage_block_face_definition *appearance;
} maage_block_geometry_quad;
typedef struct maage_block_geometry_sink {
    maage_status (*quad)(void *, const maage_block_geometry_quad *);
    void *context;
} maage_block_geometry_sink;
typedef maage_status (*maage_block_geometry_provider)(const void *, const maage_block_geometry_query *, const maage_block_geometry_sink *);
typedef struct maage_block_geometry_definition {
    unsigned int primitive;
    maage_block_geometry_provider emit;
    const void *context;
    const maage_block_face_definition *appearances; /* Copied, then resolved before finalization. */
    size_t appearance_count;
} maage_block_geometry_definition;

/* Definitions/names/appearance arrays are copied. Callback/context data is borrowed and immutable;
 * it must outlive the finalized registry and all detached mesh jobs. A provider
 * is called once per block per count/write pass, never per vertex. It must be
 * deterministic for the query, allocate nothing per block and propagate sink
 * failures. Collision and selection retain the block/state's independent bounds. */
maage_status maage_block_geometry_create(maage_block_geometry_registry **output);
maage_status maage_block_geometry_destroy(maage_block_geometry_registry *registry);
maage_status maage_block_geometry_register(maage_block_geometry_registry *registry, const char *name, const maage_block_geometry_definition *definition, maage_block_geometry_id *id);
/* Registration/resolution only. Copies one face into the registry-owned array;
 * IDs and array length remain unchanged, and finalized registries reject updates. */
maage_status maage_block_geometry_update_appearance(maage_block_geometry_registry *registry, maage_block_geometry_id id, size_t index, const maage_block_face_definition *appearance);
maage_status maage_block_geometry_finalize(maage_block_geometry_registry *registry);
int maage_block_geometry_is_finalized(const maage_block_geometry_registry *registry);
size_t maage_block_geometry_count(const maage_block_geometry_registry *registry);
const maage_block_geometry_definition *maage_block_geometry_get(const maage_block_geometry_registry *registry, maage_block_geometry_id id);
const char *maage_block_geometry_name(const maage_block_geometry_registry *registry, maage_block_geometry_id id);
maage_status maage_block_geometry_resolve(const maage_block_geometry_registry *registry, const char *name, maage_block_geometry_id *id);
/* Generic allocation-free primitive service; appearance references are resolved
 * numeric state or registered geometry faces. face_mask selects any subset of the six cuboid faces. */
maage_status maage_block_geometry_cuboid(const maage_block_geometry_sink *sink, const maage_block_shape *bounds, const maage_block_face_definition faces[MAAGE_FACE_COUNT], unsigned int face_mask, unsigned long material, int cull);
maage_status maage_block_geometry_quad_validate(const maage_block_geometry_quad *quad);
#endif
