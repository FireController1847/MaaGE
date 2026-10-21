#ifndef MAAGE_CLIENT_RESOURCES_H
#define MAAGE_CLIENT_RESOURCES_H
#include "maage/image.h"
#include "maage/pixels.h"
#include "maage/biome.h"
/* Atlas indices are resolved once, during registration. */
#define MAAGE_CLIENT_TEXTURE_STONE 0
#define MAAGE_CLIENT_TEXTURE_DIRT 1
#define MAAGE_CLIENT_TEXTURE_GRASS_TOP 2
#define MAAGE_CLIENT_TEXTURE_GRASS_SIDE 3
#define MAAGE_CLIENT_TEXTURE_BEDROCK 4
#define MAAGE_CLIENT_TEXTURE_WATER 5
#define MAAGE_CLIENT_TEXTURE_SAND 6
#define MAAGE_CLIENT_TEXTURE_GRAVEL 7
#define MAAGE_CLIENT_TEXTURE_SANDSTONE 8
#define MAAGE_CLIENT_TEXTURE_SANDSTONE_TOP 9
#define MAAGE_CLIENT_TEXTURE_SANDSTONE_BOTTOM 10
#define MAAGE_CLIENT_TEXTURE_BIOME_TOP(id) (11 + 2 * (id))
#define MAAGE_CLIENT_TEXTURE_BIOME_SIDE(id) (12 + 2 * (id))
#define MAAGE_CLIENT_TEXTURE_EXTRA (11 + 2 * MAAGE_BIOME_COUNT)
#define MAAGE_CLIENT_TEXTURE_LEAVES_OPAQUE_OAK (MAAGE_CLIENT_TEXTURE_EXTRA + 33)
#define MAAGE_CLIENT_TEXTURE_LEAVES_OPAQUE_SPRUCE (MAAGE_CLIENT_TEXTURE_EXTRA + 34)
#define MAAGE_CLIENT_TEXTURE_LEAVES_OPAQUE_BIRCH (MAAGE_CLIENT_TEXTURE_EXTRA + 35)
#define MAAGE_CLIENT_TEXTURE_MISSING (MAAGE_CLIENT_TEXTURE_EXTRA + 36)
#define MAAGE_CLIENT_TEXTURE_COUNT (MAAGE_CLIENT_TEXTURE_MISSING + 1)
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
maage_status maage_client_register_blocks(
    const maage_client_resources *resources, const maage_biome_registry *biomes,
    maage_block_registry **output, maage_status_trace *trace);
maage_status maage_client_resources_destroy(maage_client_resources *resources);
#endif
