#ifndef MAAGE_CONTENT_H
#define MAAGE_CONTENT_H

#include "maage/blocks.h"
#include "maage/biome.h"
#include "maage/items.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Stable numeric presentation tile slots, not runtime resource paths.
 * The Client resolves these into atlas UVs once, before publication. */
#define MAAGE_CONTENT_TEXTURE_STONE 0
#define MAAGE_CONTENT_TEXTURE_DIRT 1
#define MAAGE_CONTENT_TEXTURE_GRASS_TOP 2
#define MAAGE_CONTENT_TEXTURE_GRASS_SIDE 3
#define MAAGE_CONTENT_TEXTURE_BEDROCK 4
#define MAAGE_CONTENT_TEXTURE_WATER 5
#define MAAGE_CONTENT_TEXTURE_SAND 6
#define MAAGE_CONTENT_TEXTURE_GRAVEL 7
#define MAAGE_CONTENT_TEXTURE_SANDSTONE 8
#define MAAGE_CONTENT_TEXTURE_SANDSTONE_TOP 9
#define MAAGE_CONTENT_TEXTURE_SANDSTONE_BOTTOM 10
#define MAAGE_CONTENT_TEXTURE_EXTRA 11
#define MAAGE_CONTENT_TEXTURE_LEAVES_OPAQUE_OAK (MAAGE_CONTENT_TEXTURE_EXTRA + 33)
#define MAAGE_CONTENT_TEXTURE_LEAVES_OPAQUE_SPRUCE (MAAGE_CONTENT_TEXTURE_EXTRA + 34)
#define MAAGE_CONTENT_TEXTURE_LEAVES_OPAQUE_BIRCH (MAAGE_CONTENT_TEXTURE_EXTRA + 35)
#define MAAGE_CONTENT_TEXTURE_GRASS_SIDE_OVERLAY (MAAGE_CONTENT_TEXTURE_EXTRA + 36)
#define MAAGE_CONTENT_TEXTURE_GRASS_SIDE_SNOW (MAAGE_CONTENT_TEXTURE_EXTRA + 37)
#define MAAGE_CONTENT_TEXTURE_MISSING (MAAGE_CONTENT_TEXTURE_EXTRA + 38)
#define MAAGE_CONTENT_TEXTURE_ITEMS (MAAGE_CONTENT_TEXTURE_MISSING + 1)
#define MAAGE_CONTENT_TEXTURE_COUNT (MAAGE_CONTENT_TEXTURE_ITEMS + MAAGE_ITEM_TEXTURE_COUNT)

typedef struct maage_content maage_content;

/* Initialization only: the callback may populate face UVs, tint selections,
 * overlays, animation metadata and opaque leaf UVs. The numeric texture_id is
 * authoritative and must not change.
 * state_id, block_id, metadata and face are numeric; no graphics headers needed.
 * The callback and its context are never retained by finalized content. */
typedef maage_status (*maage_content_presentation_resolver)(
    void *context, maage_block_state_id state_id, maage_block_id block_id,
    unsigned int metadata, maage_block_face face,
    maage_block_face_definition *presentation, float leaf_fast_uv[4]);

typedef struct maage_content_config {
    maage_content_presentation_resolver resolver; /* NULL = headless */
    void *resolver_context;
    /* Optional preloaded colors only. Core biome geometry and identities still
     * come from the shared builtins. NULL = neutral (white) colors. */
    const maage_biome_registry *presentation_biomes;
} maage_content_config;

/* Creates, finalizes, and owns precisely one biome, block/state, and item
 * registry set. NULL config creates valid headless registries (full-face
 * neutral UVs/tints); registration requires no PNG/window/render/GPU service.
 * output is reset to NULL on failure. The returned pointer owns all borrowed
 * immutable definitions until maage_content_destroy, which must run after
 * World, all asynchronous World jobs, gameplay, and readers have stopped. */
maage_status maage_content_create(const maage_content_config *config,
    maage_content **output, maage_status_trace *trace);
const maage_block_registry *maage_content_blocks(const maage_content *content);
const maage_biome_registry *maage_content_biomes(const maage_content *content);
const maage_item_registry *maage_content_items(const maage_content *content);
maage_status maage_content_destroy(maage_content *content);

#ifdef __cplusplus
}
#endif
#endif
