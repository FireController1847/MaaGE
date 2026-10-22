#ifndef MAAGE_CLIENT_RESOURCES_H
#define MAAGE_CLIENT_RESOURCES_H
#include "maage/image.h"
#include "maage/pixels.h"
#include "maage/content.h"
#define MAAGE_CLIENT_SOURCE_COUNT 60
#define MAAGE_CLIENT_BREAK_STAGES 10
typedef struct maage_client_resources {
    maage_image image;
    maage_pixel_buffer atlas;
    float uv[MAAGE_CONTENT_TEXTURE_COUNT][4];
    float breaking_uv[MAAGE_CLIENT_BREAK_STAGES][MAAGE_CONTENT_TEXTURE_COUNT][4];
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
maage_status maage_client_resources_destroy(maage_client_resources *resources);
#endif
