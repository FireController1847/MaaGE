#ifndef MAAGE_CLIENT_RESOURCES_H
#define MAAGE_CLIENT_RESOURCES_H
#include "maage/image.h"
#include "maage/skin.h"
#include "maage/pixels.h"
#include "maage/content.h"
#include "maage/window.h"
#define MAAGE_CLIENT_SOURCE_COUNT (64 + MAAGE_ITEM_TEXTURE_COUNT + 13)
#define MAAGE_CLIENT_BREAK_STAGES 10
typedef struct maage_client_resources {
    maage_image image;
    maage_pixel_buffer atlas;
    float uv[MAAGE_CONTENT_TEXTURE_COUNT][4];
    float breaking_uv[MAAGE_CLIENT_BREAK_STAGES][4];
    /* One normalized local-player skin in the shared atlas. */
    float skin_uv[4];
    unsigned int skin_model;
    maage_status skin_status;
    float inventory_grass_color[3];
    float fluid_animation[2][3];
    float fluid_flow_uv[2][4], fluid_flow_animation[2][3];
    float white_uv[4];
    /* Initialization-only numeric failure cache. Gameplay never retries loads.
     * Destroy/reload explicitly retries all sources on the renderer owner.
     * load_status retains the first original error; recovery_status reports
     * atlas/fallback construction independently. */
    maage_status load_status, recovery_status;
    maage_status source_results[MAAGE_CLIENT_SOURCE_COUNT];
    unsigned int failed_resources;
} maage_client_resources;
/* Colormap samples populate mutable biome definitions before finalization. */
typedef maage_status (*maage_client_resource_checkpoint)(void *context, maage_status_trace *trace);
maage_status maage_client_resources_load(const char *application_data,
    maage_client_resources *resources, maage_biome_registry *biomes,
    const maage_skin_reference *player, maage_client_resource_checkpoint checkpoint,
    void *context, maage_status_trace *trace);
/* Startup-only native icon; external reconstructed artwork is never packaged. */
maage_status maage_client_resources_set_window_icon(const char *application_data,
    maage_window *window, maage_status_trace *trace);
/* Construct one owning finalized registry set, for integrated Client/Server use.
 * Call after loading the atlas/biome colormaps and finalizing those biomes. */
maage_status maage_client_content_create(const maage_client_resources *resources,
    const maage_biome_registry *biomes, maage_content **output,
    maage_status_trace *trace);
/* Pure fixed item/inventory color transform; world biome tinting stays in mesh. */
void maage_client_item_face_tint(const float grass_color[3],
    const maage_block_face_definition *face, unsigned int mode, float color[4]);
const maage_block_state_definition *maage_client_item_icon_state(
    const maage_block_registry *blocks, const maage_item_definition *item,
    unsigned int metadata);
const char *maage_client_item_name(const maage_item_registry *items, const maage_item_stack *stack);
maage_texture_id maage_client_item_texture(const maage_item_definition *item, unsigned int metadata);
maage_status maage_client_resources_destroy(maage_client_resources *resources);
#endif
