#ifndef MAAGE_CLIENT_RESOURCES_H
#define MAAGE_CLIENT_RESOURCES_H
#include "maage/image.h"
#include "maage/pixels.h"
#include "maage/content.h"
/* Atlas indices are resolved once, during registration. */
#define MAAGE_CLIENT_TEXTURE_STONE MAAGE_CONTENT_TEXTURE_STONE
#define MAAGE_CLIENT_TEXTURE_DIRT MAAGE_CONTENT_TEXTURE_DIRT
#define MAAGE_CLIENT_TEXTURE_GRASS_TOP MAAGE_CONTENT_TEXTURE_GRASS_TOP
#define MAAGE_CLIENT_TEXTURE_GRASS_SIDE MAAGE_CONTENT_TEXTURE_GRASS_SIDE
#define MAAGE_CLIENT_TEXTURE_BEDROCK MAAGE_CONTENT_TEXTURE_BEDROCK
#define MAAGE_CLIENT_TEXTURE_WATER MAAGE_CONTENT_TEXTURE_WATER
#define MAAGE_CLIENT_TEXTURE_SAND MAAGE_CONTENT_TEXTURE_SAND
#define MAAGE_CLIENT_TEXTURE_GRAVEL MAAGE_CONTENT_TEXTURE_GRAVEL
#define MAAGE_CLIENT_TEXTURE_SANDSTONE MAAGE_CONTENT_TEXTURE_SANDSTONE
#define MAAGE_CLIENT_TEXTURE_SANDSTONE_TOP MAAGE_CONTENT_TEXTURE_SANDSTONE_TOP
#define MAAGE_CLIENT_TEXTURE_SANDSTONE_BOTTOM MAAGE_CONTENT_TEXTURE_SANDSTONE_BOTTOM
#define MAAGE_CLIENT_TEXTURE_BIOME_TOP(id) MAAGE_CONTENT_TEXTURE_BIOME_TOP(id)
#define MAAGE_CLIENT_TEXTURE_BIOME_SIDE(id) MAAGE_CONTENT_TEXTURE_BIOME_SIDE(id)
#define MAAGE_CLIENT_TEXTURE_EXTRA MAAGE_CONTENT_TEXTURE_EXTRA
#define MAAGE_CLIENT_TEXTURE_LEAVES_OPAQUE_OAK MAAGE_CONTENT_TEXTURE_LEAVES_OPAQUE_OAK
#define MAAGE_CLIENT_TEXTURE_LEAVES_OPAQUE_SPRUCE MAAGE_CONTENT_TEXTURE_LEAVES_OPAQUE_SPRUCE
#define MAAGE_CLIENT_TEXTURE_LEAVES_OPAQUE_BIRCH MAAGE_CONTENT_TEXTURE_LEAVES_OPAQUE_BIRCH
#define MAAGE_CLIENT_TEXTURE_MISSING MAAGE_CONTENT_TEXTURE_MISSING
#define MAAGE_CLIENT_TEXTURE_COUNT MAAGE_CONTENT_TEXTURE_COUNT
#define MAAGE_CLIENT_SOURCE_COUNT 60
#define MAAGE_CLIENT_BREAK_STAGES 10
typedef struct maage_client_resources {
    maage_image image;
    maage_pixel_buffer atlas;
    float uv[MAAGE_CLIENT_TEXTURE_COUNT][4];
    float breaking_uv[MAAGE_CLIENT_BREAK_STAGES][MAAGE_CLIENT_TEXTURE_COUNT][4];
    /* Initialization-only numeric failure cache. Gameplay never retries loads.
     * Destroy/reload explicitly retries all sources on the renderer owner.
     * load_status retains the first original error; recovery_status reports
     * atlas/fallback construction independently. */
    maage_status load_status, recovery_status;
    maage_status source_results[MAAGE_CLIENT_SOURCE_COUNT];
    unsigned int failed_resources;
} maage_client_resources;
/* Colormap samples populate mutable biome definitions before finalization. */
maage_status maage_client_resources_load(const char *application_data,
    maage_client_resources *resources, maage_biome_registry *biomes, maage_status_trace *trace);
/* Construct one owning finalized registry set, for integrated Client/Server use.
 * Call after loading the atlas/biome colormaps and finalizing those biomes. */
maage_status maage_client_content_create(const maage_client_resources *resources,
    const maage_biome_registry *biomes, maage_content **output,
    maage_status_trace *trace);
/* Historical blocks-only ownership API; new callers should own maage_content. */
maage_status maage_client_register_blocks(
    const maage_client_resources *resources, const maage_biome_registry *biomes,
    maage_block_registry **output, maage_status_trace *trace);
maage_status maage_client_resources_destroy(maage_client_resources *resources);
#endif
