#include "resources.h"
#include "maage/minecraft_assets.h"
#include "maage/storage.h"
#include "maage/log.h"
#include <limits.h>
#include <stdlib.h>
#include <string.h>

static const char *const maage_client_texture_names[MAAGE_CLIENT_SOURCE_COUNT] = {
    "minecraft/textures/blocks/stone.png",
    "minecraft/textures/blocks/dirt.png",
    "minecraft/textures/blocks/grass_top.png",
    "minecraft/textures/blocks/grass_side.png",
    "minecraft/textures/blocks/grass_side_overlay.png",
    "minecraft/textures/colormap/grass.png",
    "minecraft/textures/blocks/bedrock.png",
    "minecraft/textures/blocks/water_still.png",
    "minecraft/textures/blocks/sand.png",
    "minecraft/textures/blocks/gravel.png",
    "minecraft/textures/blocks/sandstone_normal.png",
    "minecraft/textures/colormap/foliage.png",
    "minecraft/textures/blocks/sandstone_top.png",
    "minecraft/textures/blocks/sandstone_bottom.png",
    "minecraft/textures/blocks/destroy_stage_0.png",
    "minecraft/textures/blocks/destroy_stage_1.png",
    "minecraft/textures/blocks/destroy_stage_2.png",
    "minecraft/textures/blocks/destroy_stage_3.png",
    "minecraft/textures/blocks/destroy_stage_4.png",
    "minecraft/textures/blocks/destroy_stage_5.png",
    "minecraft/textures/blocks/destroy_stage_6.png",
    "minecraft/textures/blocks/destroy_stage_7.png",
    "minecraft/textures/blocks/destroy_stage_8.png",
    "minecraft/textures/blocks/destroy_stage_9.png",
    "minecraft/textures/blocks/cobblestone.png",
    "minecraft/textures/blocks/planks_oak.png",
    "minecraft/textures/blocks/lava_still.png",
    "minecraft/textures/blocks/gold_ore.png",
    "minecraft/textures/blocks/iron_ore.png",
    "minecraft/textures/blocks/coal_ore.png",
    "minecraft/textures/blocks/log_oak.png",
    "minecraft/textures/blocks/log_oak_top.png",
    "minecraft/textures/blocks/leaves_oak.png",
    "minecraft/textures/blocks/lapis_ore.png",
    "minecraft/textures/blocks/tallgrass.png",
    "minecraft/textures/blocks/deadbush.png",
    "minecraft/textures/blocks/flower_dandelion.png",
    "minecraft/textures/blocks/flower_rose.png",
    "minecraft/textures/blocks/mushroom_brown.png",
    "minecraft/textures/blocks/mushroom_red.png",
    "minecraft/textures/blocks/cobblestone_mossy.png",
    "minecraft/textures/blocks/obsidian.png",
    "minecraft/textures/blocks/torch_on.png",
    "minecraft/textures/blocks/diamond_ore.png",
    "minecraft/textures/blocks/redstone_ore.png",
    "minecraft/textures/blocks/snow.png",
    "minecraft/textures/blocks/cactus_side.png",
    "minecraft/textures/blocks/cactus_top.png",
    "minecraft/textures/blocks/cactus_bottom.png",
    "minecraft/textures/blocks/emerald_ore.png",
    "minecraft/textures/blocks/log_spruce.png",
    "minecraft/textures/blocks/log_spruce_top.png",
    "minecraft/textures/blocks/leaves_spruce.png",
    "minecraft/textures/blocks/log_birch.png",
    "minecraft/textures/blocks/log_birch_top.png",
    "minecraft/textures/blocks/leaves_birch.png",
    "minecraft/textures/blocks/ice.png",
    "minecraft/textures/blocks/leaves_oak_opaque.png",
    "minecraft/textures/blocks/leaves_spruce_opaque.png",
    "minecraft/textures/blocks/leaves_birch_opaque.png"
};

static void maage_client_colormap(const maage_image *map,
    double temperature, double rainfall, float color[3])
{
    const unsigned char *pixel;
    int x, y, channel;
    if (temperature < 0) temperature = 0;
    if (temperature > 1) temperature = 1;
    if (rainfall < 0) rainfall = 0;
    if (rainfall > 1) rainfall = 1;
    rainfall *= temperature;
    x = (int)((1.0 - temperature) * 255.0);
    y = (int)((1.0 - rainfall) * 255.0);
    pixel = map->pixels + (size_t)y * map->stride + (size_t)x * 4;
    for (channel = 0; channel < 3; ++channel)
        color[channel] = (float)pixel[channel] / 255.0f;
}

static void maage_client_resource_failure(maage_client_resources *resources,
    int index, maage_status result, maage_status_trace *trace)
{
    resources->source_results[index] = result;
    if (!resources->failed_resources) resources->load_status = result;
    ++resources->failed_resources;
    MAAGE_STATUS_TRACE(trace, result, MAAGE_CONTEXT_RESOURCE, MAAGE_OPERATION_LOAD_TEXTURE);
    maage_status_trace_recover(trace, maage_status_make(MAAGE_STATUS_SUCCESS, 0, MAAGE_OPERATION_ATLAS, 0), MAAGE_CONTEXT_RESOURCE, MAAGE_OPERATION_ATLAS, __FILE__, __LINE__);
    maage_log_trace_status(MAAGE_LOG_WARNING, "maage.client.resources", result, trace,
        "%s; using an initialized visual fallback. Repair resources with the Launcher.",
        maage_client_texture_names[index]);
}

maage_status maage_client_resources_load(const char *application_data,
    maage_client_resources *resources, maage_biome_registry *biomes, maage_status_trace *trace)
{
    static const int atlas_sources[11] = {0, 1, 2, 3, 6, 7, 8, 9, 10, 12, 13};
    static const float default_grass[3] = {0.50f, 0.72f, 0.35f};
    static const float default_foliage[3] = {0.35f, 0.66f, 0.30f};
    maage_image sources[MAAGE_CLIENT_SOURCE_COUNT], missing;
    const maage_image *selected;
    maage_biome_definition *biome;
    maage_status result;
    maage_status_trace source_trace, recovered_trace;
    char path[MAAGE_PATH_CAPACITY];
    const float *tint;
    unsigned char *pixel, *source, *overlay;
    unsigned int channel, alpha, value;
    int i, j, x, y, sx, sy, edge = 16, tile, source_index, biome_id, stage, ok = 0;
    int columns = 32, rows, tile_x, tile_y, group_height;
    if (!resources || !biomes || biomes->finalized) return maage_status_make(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_CONTEXT_RESOURCE, MAAGE_OPERATION_INITIALIZE, !resources ? 1 : 2);
    memset(sources, 0, sizeof(sources)); memset(&missing, 0, sizeof(missing));
    memset(resources, 0, sizeof(*resources));
    resources->load_status = maage_status_make(MAAGE_STATUS_SUCCESS, 0, MAAGE_OPERATION_LOAD_TEXTURE, 0);
    resources->recovery_status = maage_status_make(MAAGE_STATUS_SUCCESS, 0, MAAGE_OPERATION_ATLAS, 0);
    /* The asset-independent tile exists before ordinary source registration. */
    result = maage_image_make_missing(&missing, edge, edge);
    if (maage_status_failed(result)) { resources->recovery_status = result; goto done; }
    /* External names are consumed here only; mesh faces retain numeric UVs.
     * Each result is cached once for this immutable resource generation. */
    for (i = 0; i < MAAGE_CLIENT_SOURCE_COUNT; ++i) {
        maage_status_trace_init(&source_trace);
        MAAGE_STATUS_TRACE(&source_trace, 0, MAAGE_CONTEXT_CLIENT, MAAGE_OPERATION_INITIALIZE);
        MAAGE_STATUS_TRACE(&source_trace, 0, MAAGE_CONTEXT_RESOURCE, MAAGE_OPERATION_INITIALIZE);
        MAAGE_STATUS_TRACE(&source_trace, 0, MAAGE_CONTEXT_RESOURCE, MAAGE_OPERATION_ATLAS);
        result = maage_minecraft_asset_path(application_data,
            maage_client_texture_names[i], path, sizeof(path), &source_trace);
        if (!maage_status_failed(result)) result = maage_image_load_png(path, &sources[i], &source_trace);
        if (!maage_status_failed(result)) {
            /* Normalize each valid source independently into the canonical
             * 16px atlas. One unusual texture cannot resize unrelated tiles. */
            if ((i == 5 || i == 11) ? (sources[i].width != 256 || sources[i].height != 256) :
                (sources[i].height != sources[i].width &&
                !((i == 7 || i == 26) && sources[i].height >= sources[i].width &&
                sources[i].height % sources[i].width == 0)))
                result = maage_status_make(MAAGE_STATUS_MALFORMED_INPUT, MAAGE_CONTEXT_RESOURCE, MAAGE_OPERATION_LOAD_TEXTURE, 0);
        }
        resources->source_results[i] = result;
        if (maage_status_failed(result)) {
            maage_image_destroy(&sources[i]);
            maage_client_resource_failure(resources, i, result, &source_trace);
            if (resources->failed_resources == 1) recovered_trace = source_trace;
        } else maage_log_verbose("maage.client.resources", "Loaded %s (%dx%d).",
            maage_client_texture_names[i], sources[i].width, sources[i].height);
    }
    for (i = 0; i < MAAGE_BIOME_COUNT; ++i) {
        if (!biomes->builtin_names[i]) {
            resources->recovery_status = maage_status_make(MAAGE_STATUS_INVARIANT_VIOLATION,
                MAAGE_CONTEXT_RESOURCE, MAAGE_OPERATION_INITIALIZE, 0); goto done;
        }
        biome = &biomes->builtin_definitions[i];
        /* Absent optional colormaps use fixed, bounded green presentation tints. */
        if (sources[5].pixels) maage_client_colormap(&sources[5], biome->temperature, biome->rainfall, biome->grass_color);
        else memcpy(biome->grass_color, default_grass, sizeof(default_grass));
        if (sources[11].pixels) maage_client_colormap(&sources[11], biome->temperature, biome->rainfall, biome->foliage_color);
        else memcpy(biome->foliage_color, default_foliage, sizeof(default_foliage));
    }
    resources->recovery_status = maage_status_make(MAAGE_STATUS_BUFFER_OVERFLOW, MAAGE_CONTEXT_RESOURCE, MAAGE_OPERATION_ATLAS, 0);
    rows = (MAAGE_CLIENT_TEXTURE_COUNT + columns - 1) / columns;
    if (edge > INT_MAX - 2 || edge + 2 > INT_MAX / columns ||
        edge + 2 > INT_MAX / rows / (1 + MAAGE_CLIENT_BREAK_STAGES)) goto done;
    tile = edge + 2; group_height = tile * rows;
    resources->image.width = tile * columns;
    resources->image.height = group_height * (1 + MAAGE_CLIENT_BREAK_STAGES);
    if ((size_t)resources->image.width > (size_t)-1 / 4) goto done;
    resources->image.stride = (size_t)resources->image.width * 4;
    if ((size_t)resources->image.height > (size_t)-1 / resources->image.stride) goto done;
    resources->image.pixels = (unsigned char *)calloc((size_t)resources->image.height, resources->image.stride);
    if (!resources->image.pixels) {
        resources->recovery_status = maage_status_make(MAAGE_STATUS_ALLOCATION_FAILED, MAAGE_CONTEXT_RESOURCE, MAAGE_OPERATION_ATLAS, 0); goto done;
    }
    for (i = 0; i < MAAGE_CLIENT_TEXTURE_COUNT; ++i) {
        tile_x = (i % columns) * tile; tile_y = (i / columns) * tile;
        biome_id = i >= 11 && i < MAAGE_CLIENT_TEXTURE_EXTRA ? (i - 11) / 2 : MAAGE_BIOME_PLAINS;
        source_index = i == MAAGE_CLIENT_TEXTURE_MISSING ? -1 :
            (i >= MAAGE_CLIENT_TEXTURE_EXTRA ? 24 + i - MAAGE_CLIENT_TEXTURE_EXTRA :
            (i >= 11 ? ((i - 11) % 2 ? 3 : 2) : atlas_sources[i]));
        if (i == MAAGE_CLIENT_TEXTURE_EXTRA + 28 || i == MAAGE_CLIENT_TEXTURE_LEAVES_OPAQUE_SPRUCE)
            biome_id = MAAGE_BIOME_TAIGA;
        else if (i == MAAGE_CLIENT_TEXTURE_EXTRA + 31 || i == MAAGE_CLIENT_TEXTURE_LEAVES_OPAQUE_BIRCH)
            biome_id = MAAGE_BIOME_FOREST;
        /* Missing opaque variants degrade to a solid version of the cutout
         * source, not the magenta/black missing-texture tile. */
        if (i == MAAGE_CLIENT_TEXTURE_LEAVES_OPAQUE_OAK && !sources[source_index].pixels) source_index = 32;
        else if (i == MAAGE_CLIENT_TEXTURE_LEAVES_OPAQUE_SPRUCE && !sources[source_index].pixels) source_index = 52;
        else if (i == MAAGE_CLIENT_TEXTURE_LEAVES_OPAQUE_BIRCH && !sources[source_index].pixels) source_index = 55;
        selected = source_index >= 0 && sources[source_index].pixels ? &sources[source_index] : &missing;
        tint = source_index == 32 || source_index == 52 || source_index == 55 ||
            source_index == 57 || source_index == 58 || source_index == 59 ?
            biomes->builtin_definitions[biome_id].foliage_color : biomes->builtin_definitions[biome_id].grass_color;
        for (y = 0; y < tile; ++y) {
            sy = y == 0 ? 0 : (y > edge ? edge - 1 : y - 1);
            for (x = 0; x < tile; ++x) {
                sx = x == 0 ? 0 : (x > edge ? edge - 1 : x - 1);
                pixel = resources->image.pixels + (size_t)(tile_y + y) * resources->image.stride + (size_t)(tile_x + x) * 4;
                source = selected->pixels + (size_t)(sy * selected->width / edge) * selected->stride +
                    (size_t)(sx * selected->width / edge) * 4;
                memcpy(pixel, source, 4);
                if (selected == &missing) continue;
                if (source_index == 2 || source_index == 32 || source_index == 34 ||
                    source_index == 52 || source_index == 55 ||
                    source_index == 57 || source_index == 58 || source_index == 59)
                    for (channel = 0; channel < 3; ++channel) pixel[channel] = (unsigned char)((float)pixel[channel] * tint[channel]);
                if (source_index == 3 && sources[4].pixels) {
                    overlay = sources[4].pixels + (size_t)(sy * sources[4].width / edge) * sources[4].stride +
                        (size_t)(sx * sources[4].width / edge) * 4;
                    alpha = overlay[3];
                    for (channel = 0; channel < 3; ++channel) {
                        value = (unsigned int)((float)overlay[channel] * tint[channel]);
                        pixel[channel] = (unsigned char)((value * alpha + (unsigned int)pixel[channel] * (255 - alpha) + 127) / 255);
                    }
                }
                if (source_index == 7) {
                    pixel[0] = (unsigned char)((unsigned int)pixel[0] * 63 / 255);
                    pixel[1] = (unsigned char)((unsigned int)pixel[1] * 118 / 255);
                    pixel[2] = (unsigned char)((unsigned int)pixel[2] * 228 / 255); pixel[3] = 255;
                } else if (source_index == 26 ||
                    (i >= MAAGE_CLIENT_TEXTURE_LEAVES_OPAQUE_OAK && i <= MAAGE_CLIENT_TEXTURE_LEAVES_OPAQUE_BIRCH))
                    pixel[3] = 255;
            }
        }
        resources->uv[i][0] = (float)(tile_x + 1) / resources->image.width;
        resources->uv[i][1] = (float)(tile_y + 1) / resources->image.height;
        resources->uv[i][2] = (float)(tile_x + 1 + edge) / resources->image.width;
        resources->uv[i][3] = (float)(tile_y + 1 + edge) / resources->image.height;
        for (stage = 0; stage < MAAGE_CLIENT_BREAK_STAGES; ++stage) {
            for (y = 0; y < tile; ++y) {
                sy = y == 0 ? 0 : (y > edge ? edge - 1 : y - 1);
                for (x = 0; x < tile; ++x) {
                    sx = x == 0 ? 0 : (x > edge ? edge - 1 : x - 1);
                    source = resources->image.pixels + (size_t)(tile_y + y) * resources->image.stride + (size_t)(tile_x + x) * 4;
                    pixel = source + (size_t)(stage + 1) * group_height * resources->image.stride;
                    memcpy(pixel, source, 4);
                    if (!sources[14 + stage].pixels) continue;
                    overlay = sources[14 + stage].pixels + (size_t)(sy * sources[14 + stage].width / edge) * sources[14 + stage].stride +
                        (size_t)(sx * sources[14 + stage].width / edge) * 4;
                    for (channel = 0; channel < 3; ++channel)
                        pixel[channel] = (unsigned char)((unsigned int)source[channel] *
                            (255U - overlay[3] + (unsigned int)overlay[channel] * overlay[3] / 255U) / 255U);
                }
            }
            memcpy(resources->breaking_uv[stage][i], resources->uv[i], sizeof(resources->uv[i]));
            resources->breaking_uv[stage][i][1] += (float)(stage + 1) / (1 + MAAGE_CLIENT_BREAK_STAGES);
            resources->breaking_uv[stage][i][3] += (float)(stage + 1) / (1 + MAAGE_CLIENT_BREAK_STAGES);
        }
    }
    /* Every failed visual resolves to the same canonical tile in the shared
     * renderer-owned atlas; registration never publishes a missing handle. */
    for (i = 0; i < MAAGE_CLIENT_TEXTURE_MISSING; ++i) {
        source_index = i >= MAAGE_CLIENT_TEXTURE_EXTRA ? 24 + i - MAAGE_CLIENT_TEXTURE_EXTRA :
            (i >= 11 ? ((i - 11) % 2 ? 3 : 2) : atlas_sources[i]);
        if (sources[source_index].pixels) continue;
        if (i >= MAAGE_CLIENT_TEXTURE_LEAVES_OPAQUE_OAK &&
            sources[i == MAAGE_CLIENT_TEXTURE_LEAVES_OPAQUE_OAK ? 32 :
                (i == MAAGE_CLIENT_TEXTURE_LEAVES_OPAQUE_SPRUCE ? 52 : 55)].pixels) continue;
        memcpy(resources->uv[i], resources->uv[MAAGE_CLIENT_TEXTURE_MISSING], sizeof(resources->uv[i]));
        for (stage = 0; stage < MAAGE_CLIENT_BREAK_STAGES; ++stage)
            memcpy(resources->breaking_uv[stage][i], resources->breaking_uv[stage][MAAGE_CLIENT_TEXTURE_MISSING], sizeof(resources->uv[i]));
    }
    resources->atlas.pixels = resources->image.pixels;
    resources->atlas.width = resources->image.width; resources->atlas.height = resources->image.height;
    resources->atlas.stride = resources->image.stride;
    resources->recovery_status = maage_status_make(MAAGE_STATUS_SUCCESS, 0, MAAGE_OPERATION_ATLAS, 0);
    ok = 1;
done:
    for (j = 0; j < MAAGE_CLIENT_SOURCE_COUNT; ++j) maage_image_destroy(&sources[j]);
    maage_image_destroy(&missing);
    if (!ok) {
        maage_image_destroy(&resources->image);
        memset(&resources->atlas, 0, sizeof(resources->atlas));
    }
    if (!ok) return MAAGE_STATUS_PROPAGATE(trace, resources->recovery_status, MAAGE_CONTEXT_RESOURCE, MAAGE_OPERATION_INITIALIZE);
    if (trace && resources->failed_resources) *trace = recovered_trace;
    return MAAGE_STATUS_PROPAGATE(trace, maage_status_make(resources->failed_resources ? MAAGE_STATUS_PARTIAL : MAAGE_STATUS_SUCCESS,
        MAAGE_CONTEXT_RESOURCE, MAAGE_OPERATION_INITIALIZE, 0), MAAGE_CONTEXT_CLIENT, MAAGE_OPERATION_INITIALIZE);
}

/* Presentation is resolved once from the Client-owned atlas. All block and
 * state identity, geometry, gameplay and persistence fields are World-owned. */
static maage_status maage_client_content_resolve(void *context,
    maage_block_state_id state_id, maage_block_id block,
    unsigned int metadata, maage_block_face face,
    maage_block_face_definition *presentation, float leaf_fast_uv[4])
{
    const maage_client_resources *resources = (const maage_client_resources *)context;
    int fast_tile;
    (void)state_id;
    (void)face;
    if (!resources || !presentation || !leaf_fast_uv ||
        presentation->texture_id >= MAAGE_CLIENT_TEXTURE_COUNT)
        return maage_status_make(MAAGE_STATUS_INVALID_ARGUMENT,
            MAAGE_CONTEXT_RESOURCE, MAAGE_OPERATION_RESOLVE, 0);
    memcpy(presentation->uv, resources->uv[presentation->texture_id],
        sizeof(presentation->uv));
    if (block == MAAGE_BLOCK_LEAVES) {
        fast_tile = metadata == 1 ? MAAGE_CLIENT_TEXTURE_LEAVES_OPAQUE_SPRUCE :
            (metadata == 2 ? MAAGE_CLIENT_TEXTURE_LEAVES_OPAQUE_BIRCH :
                MAAGE_CLIENT_TEXTURE_LEAVES_OPAQUE_OAK);
        memcpy(leaf_fast_uv, resources->uv[fast_tile], sizeof(resources->uv[fast_tile]));
    }
    return maage_status_make(MAAGE_STATUS_SUCCESS, 0, MAAGE_OPERATION_RESOLVE, 0);
}

maage_status maage_client_content_create(const maage_client_resources *resources,
    const maage_biome_registry *biomes, maage_content **output,
    maage_status_trace *trace)
{
    maage_content_config config;
    maage_status result;
    if (output) *output = NULL;
    if (!resources || !biomes || !output) return maage_status_make(
        MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_CONTEXT_CLIENT, MAAGE_OPERATION_CREATE,
        !resources ? 0 : (!biomes ? 1 : 2));
    if (!biomes->finalized) return maage_status_make(MAAGE_STATUS_INVALID_STATE,
        MAAGE_CONTEXT_CLIENT, MAAGE_OPERATION_CREATE, 0);
    memset(&config, 0, sizeof(config));
    config.resolver = maage_client_content_resolve;
    config.resolver_context = (void *)resources;
    config.presentation_biomes = biomes;
    result = maage_content_create(&config, output, trace);
    if (maage_status_failed(result)) return MAAGE_STATUS_PROPAGATE(trace,
        result, MAAGE_CONTEXT_CLIENT, MAAGE_OPERATION_CREATE);
    maage_log_info("maage.client.world",
        "Finalized %lu block types / %lu states; direct numeric runtime indexing.",
        (unsigned long)maage_block_registry_block_count(maage_content_blocks(*output)),
        (unsigned long)maage_block_registry_state_count(maage_content_blocks(*output)));
    return result;
}

/* Legacy Client signature: retain the old ownership contract but delegate
 * entirely to the shared authoritative registration implementation. */
maage_status maage_client_register_blocks(
    const maage_client_resources *resources, const maage_biome_registry *biomes,
    maage_block_registry **output, maage_status_trace *trace)
{
    maage_content_config config;
    maage_status result;
    if (output) *output = NULL;
    if (!resources || !biomes || !output) return maage_status_make(
        MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_CONTEXT_REGISTRY, MAAGE_OPERATION_INITIALIZE,
        !resources ? 0 : (!biomes ? 1 : 2));
    memset(&config, 0, sizeof(config));
    config.resolver = maage_client_content_resolve;
    config.resolver_context = (void *)resources;
    result = maage_content_blocks_create(&config, biomes, output, trace);
    if (maage_status_failed(result)) return MAAGE_STATUS_PROPAGATE(trace,
        result, MAAGE_CONTEXT_CLIENT, MAAGE_OPERATION_REGISTER);
    maage_log_info("maage.client.world",
        "Finalized %lu block types / %lu states; direct numeric runtime indexing.",
        (unsigned long)maage_block_registry_block_count(*output),
        (unsigned long)maage_block_registry_state_count(*output));
    return result;
}

maage_status maage_client_resources_destroy(maage_client_resources *resources)
{
    if (!resources) return maage_status_make(MAAGE_STATUS_NOOP, 0, MAAGE_OPERATION_DESTROY, 0);
    maage_image_destroy(&resources->image);
    memset(resources, 0, sizeof(*resources));
    return maage_status_make(MAAGE_STATUS_SUCCESS, 0, MAAGE_OPERATION_DESTROY, 0);
}
