#include "resources.h"
#include "maage/minecraft_assets.h"
#include "maage/storage.h"
#include "maage/log.h"
#include <limits.h>
#include <stdlib.h>
#include <string.h>

maage_status maage_client_resources_set_window_icon(const char *application_data,
    maage_window *window, maage_status_trace *trace)
{
    static const char *const names[2] = {"icons/icon_32x32.png", "icons/icon_16x16.png"};
    maage_window_native native;
    maage_window_icon icon;
    maage_image images[2];
    maage_pixel_buffer pixels[2];
    char path[MAAGE_PATH_CAPACITY];
    maage_status result;
    int i;
    memset(&icon, 0, sizeof(icon));
    memset(images, 0, sizeof(images));
    result = maage_window_get_native(window, &native);
    if (maage_status_failed(result)) goto done;
    if (native.backend == MAAGE_WINDOW_COCOA) {
        result = maage_minecraft_asset_path(application_data, "icons/minecraft.icns", path, sizeof(path), trace);
        if (maage_status_failed(result)) goto done;
        icon.native_path = path;
    } else {
        for (i = 0; i < 2; ++i) {
            result = maage_minecraft_asset_path(application_data, names[i], path, sizeof(path), trace);
            if (maage_status_failed(result)) goto done;
            result = maage_image_load_png(path, &images[i], trace);
            if (maage_status_failed(result)) goto done;
            pixels[i].pixels = images[i].pixels;
            pixels[i].width = images[i].width; pixels[i].height = images[i].height;
            pixels[i].stride = images[i].stride;
        }
        icon.images = pixels; icon.image_count = 2;
    }
    result = maage_window_set_icon(window, &icon);
done:
    for (i = 0; i < 2; ++i) (void)maage_image_destroy(&images[i]);
    return MAAGE_STATUS_PROPAGATE(trace, result, MAAGE_CONTEXT_CLIENT, MAAGE_OPERATION_SET);
}

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
    "minecraft/textures/blocks/leaves_birch_opaque.png",
    "minecraft/textures/entity/steve.png",
    "minecraft/textures/blocks/grass_side_snowed.png",
    "minecraft/textures/blocks/water_flow.png",
    "minecraft/textures/blocks/lava_flow.png",
    "minecraft/textures/items/wood_pickaxe.png",
    "minecraft/textures/items/wood_shovel.png",
    "minecraft/textures/items/wood_axe.png",
    "minecraft/textures/items/stone_pickaxe.png",
    "minecraft/textures/items/stone_shovel.png",
    "minecraft/textures/items/stone_axe.png",
    "minecraft/textures/items/iron_pickaxe.png",
    "minecraft/textures/items/iron_shovel.png",
    "minecraft/textures/items/iron_axe.png",
    "minecraft/textures/items/diamond_pickaxe.png",
    "minecraft/textures/items/diamond_shovel.png",
    "minecraft/textures/items/diamond_axe.png",
    "minecraft/textures/items/gold_pickaxe.png",
    "minecraft/textures/items/gold_shovel.png",
    "minecraft/textures/items/gold_axe.png",
    "minecraft/textures/items/shears.png",
    "minecraft/textures/items/coal.png",
    "minecraft/textures/items/diamond.png",
    "minecraft/textures/items/redstone_dust.png",
    "minecraft/textures/items/dye_powder_blue.png",
    "minecraft/textures/items/emerald.png",
    "minecraft/textures/items/flint.png",
    "minecraft/textures/items/apple.png",
    "minecraft/textures/blocks/sapling_oak.png",
    "minecraft/textures/items/snowball.png",
    "minecraft/textures/items/seeds_wheat.png",
    "minecraft/textures/blocks/sapling_spruce.png",
    "minecraft/textures/blocks/sapling_birch.png",
    "minecraft/textures/items/stick.png",
    "minecraft/textures/items/iron_ingot.png",
    "minecraft/textures/items/gold_ingot.png",
    "minecraft/textures/items/gold_nugget.png",
    "minecraft/textures/items/bowl.png",
    "minecraft/textures/items/mushroom_stew.png",
    "minecraft/textures/items/charcoal.png",
    "minecraft/textures/blocks/crafting_table_top.png",
    "minecraft/textures/blocks/crafting_table_side.png",
    "minecraft/textures/blocks/crafting_table_front.png",
    "minecraft/textures/blocks/planks_spruce.png",
    "minecraft/textures/blocks/planks_birch.png",
    "minecraft/textures/blocks/gold_block.png",
    "minecraft/textures/blocks/iron_block.png",
    "minecraft/textures/blocks/diamond_block.png",
    "minecraft/textures/blocks/lapis_block.png",
    "minecraft/textures/blocks/emerald_block.png",
    "minecraft/textures/blocks/redstone_block.png",
    "minecraft/textures/blocks/coal_block.png",
    "minecraft/textures/blocks/snow.png"
};

static int maage_client_texture_source(int texture)
{
    static const int base[11] = {0, 1, 2, 3, 6, 7, 8, 9, 10, 12, 13};
    if (texture >= MAAGE_CONTENT_TEXTURE_CRAFTING_TOP && texture < MAAGE_CONTENT_TEXTURE_ITEMS)
        return 64 + MAAGE_ITEM_TEXTURE_COUNT + texture - MAAGE_CONTENT_TEXTURE_CRAFTING_TOP;
    if (texture >= MAAGE_CONTENT_TEXTURE_ITEMS) return 64 + texture - MAAGE_CONTENT_TEXTURE_ITEMS;
    if (texture == MAAGE_CONTENT_TEXTURE_MISSING) return -1;
    if (texture == MAAGE_CONTENT_TEXTURE_GRASS_SIDE_OVERLAY) return 4;
    if (texture == MAAGE_CONTENT_TEXTURE_GRASS_SIDE_SNOW) return 61;
    return texture >= MAAGE_CONTENT_TEXTURE_EXTRA ?
        24 + texture - MAAGE_CONTENT_TEXTURE_EXTRA : base[texture];
}

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
    maage_client_resources *resources, maage_biome_registry *biomes,
    const maage_skin_reference *player, maage_client_resource_checkpoint checkpoint,
    void *context, maage_status_trace *trace)
{
    static const float default_grass[3] = {0.50f, 0.72f, 0.35f};
    static const float default_foliage[3] = {0.35f, 0.66f, 0.30f};
    maage_image sources[MAAGE_CLIENT_SOURCE_COUNT], missing;
    const maage_image *selected;
    maage_biome_definition *biome;
    maage_status result;
    maage_status_trace source_trace, recovered_trace;
    char path[MAAGE_PATH_CAPACITY];
    unsigned char *pixel, *source;
    int i, j, x, y, sx, sy, edge = 16, tile, source_index, stage, ok = 0;
    int columns = 32, rows, tile_x, tile_y, skin_y;
    int fluid, frame, frames[4], animation_y, animation_height, frame_edge, frame_tile;
    float *fluid_uv, *fluid_animation;
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
        if (checkpoint) {
            result = checkpoint(context, trace);
            if (maage_status_failed(result)) { resources->recovery_status = result; goto done; }
        }
        maage_status_trace_init(&source_trace);
        MAAGE_STATUS_TRACE(&source_trace, 0, MAAGE_CONTEXT_CLIENT, MAAGE_OPERATION_INITIALIZE);
        MAAGE_STATUS_TRACE(&source_trace, 0, MAAGE_CONTEXT_RESOURCE, MAAGE_OPERATION_INITIALIZE);
        MAAGE_STATUS_TRACE(&source_trace, 0, MAAGE_CONTEXT_RESOURCE, MAAGE_OPERATION_ATLAS);
        if (i==60 && player && *player->texture) {
            result=maage_skin_path(application_data,player,path);
            if (!maage_status_failed(result)) result=maage_skin_load(path,player->model,&sources[i],&source_trace);
            resources->skin_status=result;
            if (!maage_status_failed(result)) { resources->skin_model=player->model; goto source_checked; }
            maage_log_trace_status(MAAGE_LOG_WARNING,"maage.client.skin",result,&source_trace,
                "Selected cached skin unavailable; using the original default appearance.");
            if (!resources->failed_resources) { resources->load_status=result; recovered_trace=source_trace; }
            ++resources->failed_resources;
            maage_image_destroy(&sources[i]); maage_status_trace_init(&source_trace);
        }
        result = maage_minecraft_asset_path(application_data,
            maage_client_texture_names[i], path, sizeof(path), &source_trace);
        if (!maage_status_failed(result)) result = i==60 ? maage_skin_load(path,MAAGE_SKIN_CLASSIC,&sources[i],&source_trace) :
            maage_image_load_png(path, &sources[i], &source_trace);
source_checked:
        if (!maage_status_failed(result)) {
            /* Normalize each valid source independently into the canonical
             * 16px atlas. One unusual texture cannot resize unrelated tiles. */
            if ((i == 5 || i == 11) ? (sources[i].width != 256 || sources[i].height != 256) :
                (i == 60 ? (sources[i].width != 64 || sources[i].height != 64) :
                (sources[i].height != sources[i].width &&
                !((i == 7 || i == 26 || i == 62 || i == 63) && sources[i].height >= sources[i].width &&
                sources[i].height % sources[i].width == 0))))
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
    if (sources[5].pixels)
        maage_client_colormap(&sources[5], 0.5, 1.0, resources->inventory_grass_color);
    else memcpy(resources->inventory_grass_color, default_grass, sizeof(default_grass));
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
    rows = (MAAGE_CONTENT_TEXTURE_COUNT + MAAGE_CLIENT_BREAK_STAGES + columns - 1) / columns;
    if (edge > INT_MAX - 2 || edge + 2 > INT_MAX / columns ||
        edge + 2 > INT_MAX / rows) goto done;
    tile = edge + 2;
    resources->image.width = tile * columns;
    animation_y = tile * rows;
    frames[0] = sources[7].pixels ? sources[7].height / sources[7].width : 1;
    frames[1] = sources[26].pixels ? sources[26].height / sources[26].width : 1;
    frames[2] = sources[62].pixels ? sources[62].height / sources[62].width : 1;
    frames[3] = sources[63].pixels ? sources[63].height / sources[63].width : 1;
    animation_height = 0;
    for (fluid = 0; fluid < 4; ++fluid) {
        frame_tile = fluid < 2 ? tile : 34;
        if (frames[fluid] > INT_MAX / frame_tile) goto done;
        if (frames[fluid] * frame_tile > animation_height)
            animation_height = frames[fluid] * frame_tile;
    }
    if (animation_y > INT_MAX - animation_height - 66) goto done;
    skin_y=animation_y+animation_height;
    resources->image.height=skin_y+66;
    if ((size_t)resources->image.width > (size_t)-1 / 4) goto done;
    resources->image.stride = (size_t)resources->image.width * 4;
    if ((size_t)resources->image.height > (size_t)-1 / resources->image.stride) goto done;
    resources->image.pixels = (unsigned char *)calloc((size_t)resources->image.height, resources->image.stride);
    if (!resources->image.pixels) {
        resources->recovery_status = maage_status_make(MAAGE_STATUS_ALLOCATION_FAILED, MAAGE_CONTEXT_RESOURCE, MAAGE_OPERATION_ATLAS, 0); goto done;
    }
    for (i = 0; i < MAAGE_CONTENT_TEXTURE_COUNT + MAAGE_CLIENT_BREAK_STAGES; ++i) {
        tile_x = (i % columns) * tile; tile_y = (i / columns) * tile;
        stage=i-MAAGE_CONTENT_TEXTURE_COUNT;
        source_index=stage>=0 ? 14+stage : maage_client_texture_source(i);
        /* Missing opaque variants degrade to a solid version of the cutout
         * source, not the magenta/black missing-texture tile. */
        if (i == MAAGE_CONTENT_TEXTURE_LEAVES_OPAQUE_OAK && !sources[source_index].pixels) source_index = 32;
        else if (i == MAAGE_CONTENT_TEXTURE_LEAVES_OPAQUE_SPRUCE && !sources[source_index].pixels) source_index = 52;
        else if (i == MAAGE_CONTENT_TEXTURE_LEAVES_OPAQUE_BIRCH && !sources[source_index].pixels) source_index = 55;
        selected = source_index >= 0 && sources[source_index].pixels ? &sources[source_index] : &missing;
        for (y = 0; y < tile; ++y) {
            sy = y == 0 ? 0 : (y > edge ? edge - 1 : y - 1);
            for (x = 0; x < tile; ++x) {
                sx = x == 0 ? 0 : (x > edge ? edge - 1 : x - 1);
                pixel = resources->image.pixels + (size_t)(tile_y + y) * resources->image.stride + (size_t)(tile_x + x) * 4;
                source = selected->pixels + (size_t)(sy * selected->width / edge) * selected->stride +
                    (size_t)(sx * selected->width / edge) * 4;
                memcpy(pixel, source, 4);
                /* An unavailable crack stage is an omitted decoration. */
                if (stage >= 0 && selected == &missing) memset(pixel, 0, 4);
                if (selected == &missing) continue;
                if (source_index == 26 ||
                    (i >= MAAGE_CONTENT_TEXTURE_LEAVES_OPAQUE_OAK && i <= MAAGE_CONTENT_TEXTURE_LEAVES_OPAQUE_BIRCH))
                    pixel[3] = 255;
            }
        }
        if (stage >= 0) {
            resources->breaking_uv[stage][0] = (float)(tile_x + 1) / resources->image.width;
            resources->breaking_uv[stage][1] = (float)(tile_y + 1) / resources->image.height;
            resources->breaking_uv[stage][2] = (float)(tile_x + 1 + edge) / resources->image.width;
            resources->breaking_uv[stage][3] = (float)(tile_y + 1 + edge) / resources->image.height;
            continue;
        }
        resources->uv[i][0] = (float)(tile_x + 1) / resources->image.width;
        resources->uv[i][1] = (float)(tile_y + 1) / resources->image.height;
        resources->uv[i][2] = (float)(tile_x + 1 + edge) / resources->image.width;
        resources->uv[i][3] = (float)(tile_y + 1 + edge) / resources->image.height;
    }
    selected=sources[60].pixels ? &sources[60] : &missing;
    for(y=0;y<66;++y) for(x=0;x<66;++x) {
        sx=x==0 ? 0 : (x>64 ? 63 : x-1); sy=y==0 ? 0 : (y>64 ? 63 : y-1);
        memcpy(resources->image.pixels+(size_t)(skin_y+y)*resources->image.stride+(size_t)x*4,
            selected->pixels+(size_t)(sy*selected->height/64)*selected->stride+(size_t)(sx*selected->width/64)*4,4);
    }
    resources->skin_uv[0]=1.0f/resources->image.width;
    resources->skin_uv[1]=(float)(skin_y+1)/resources->image.height;
    resources->skin_uv[2]=65.0f/resources->image.width;
    resources->skin_uv[3]=(float)(skin_y+65)/resources->image.height;
    /* Every failed visual resolves to the same canonical tile in the shared
     * renderer-owned atlas; registration never publishes a missing handle. */
    for (i = 0; i < MAAGE_CONTENT_TEXTURE_COUNT; ++i) {
        if (i == MAAGE_CONTENT_TEXTURE_MISSING) continue;
        source_index = maage_client_texture_source(i);
        if (sources[source_index].pixels) continue;
        if (i >= MAAGE_CONTENT_TEXTURE_LEAVES_OPAQUE_OAK && i <= MAAGE_CONTENT_TEXTURE_LEAVES_OPAQUE_BIRCH &&
            sources[i == MAAGE_CONTENT_TEXTURE_LEAVES_OPAQUE_OAK ? 32 :
                (i == MAAGE_CONTENT_TEXTURE_LEAVES_OPAQUE_SPRUCE ? 52 : 55)].pixels) continue;
        memcpy(resources->uv[i], resources->uv[MAAGE_CONTENT_TEXTURE_MISSING], sizeof(resources->uv[i]));
    }
    /* Immutable padded animation strips keep all mesh UVs stable. The original
     * 1.7.10 water metadata visits every frame; lava visits 0..19..1, both with
     * two game ticks per frame. Flow metadata uses one tick for water and three
     * for lava. No texture decode, upload or remesh per tick. */
    for (fluid = 0; fluid < 4; ++fluid) {
        source_index = fluid < 2 ? (fluid ? 26 : 7) : 60 + fluid;
        if (!sources[source_index].pixels) continue;
        selected = &sources[source_index];
        frame_edge = fluid < 2 ? edge : 32; frame_tile = frame_edge + 2;
        tile_x = fluid < 2 ? fluid * tile : 2 * tile + (fluid - 2) * frame_tile;
        for (frame = 0; frame < frames[fluid]; ++frame) {
            tile_y = animation_y + frame * frame_tile;
            for (y = 0; y < frame_tile; ++y) {
                sy = y == 0 ? 0 : (y > frame_edge ? frame_edge - 1 : y - 1);
                for (x = 0; x < frame_tile; ++x) {
                    sx = x == 0 ? 0 : (x > frame_edge ? frame_edge - 1 : x - 1);
                    pixel = resources->image.pixels + (size_t)(tile_y + y) * resources->image.stride +
                        (size_t)(tile_x + x) * 4;
                    source = selected->pixels + (size_t)(frame * selected->width +
                        sy * selected->width / frame_edge) * selected->stride +
                        (size_t)(sx * selected->width / frame_edge) * 4;
                    memcpy(pixel, source, 4);
                    if (fluid == 1 || fluid == 3) pixel[3] = 255;
                }
            }
        }
        i = fluid ? MAAGE_CONTENT_TEXTURE_EXTRA + 2 : MAAGE_CONTENT_TEXTURE_WATER;
        fluid_uv = fluid < 2 ? resources->uv[i] : resources->fluid_flow_uv[fluid - 2];
        fluid_animation = fluid < 2 ? resources->fluid_animation[fluid] : resources->fluid_flow_animation[fluid - 2];
        fluid_uv[0] = (float)(tile_x + 1) / resources->image.width;
        fluid_uv[1] = (float)(animation_y + 1) / resources->image.height;
        fluid_uv[2] = (float)(tile_x + 1 + frame_edge) / resources->image.width;
        fluid_uv[3] = (float)(animation_y + 1 + frame_edge) / resources->image.height;
        if (frames[fluid] > 1) {
            fluid_animation[0] = (float)frame_tile / resources->image.height;
            fluid_animation[1] = (float)(fluid == 1 ? -frames[fluid] : frames[fluid]);
            fluid_animation[2] = fluid < 2 ? 2.0f : (fluid == 2 ? 1.0f : 3.0f);
        }
    }
    pixel = resources->image.pixels + (size_t)(resources->image.height - 1) * resources->image.stride +
        (size_t)(resources->image.width - 1) * 4;
    memset(pixel, 255, 4);
    resources->white_uv[0] = resources->white_uv[2] =
        ((float)resources->image.width - 0.5f) / resources->image.width;
    resources->white_uv[1] = resources->white_uv[3] =
        ((float)resources->image.height - 0.5f) / resources->image.height;
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
    static const unsigned long leaf_colors[2] = {6396257UL, 8431445UL};
    unsigned long color;
    int fast_tile, channel;
    (void)state_id;
    if (!resources || !presentation || !leaf_fast_uv ||
        presentation->texture_id >= MAAGE_CONTENT_TEXTURE_COUNT)
        return maage_status_make(MAAGE_STATUS_INVALID_ARGUMENT,
            MAAGE_CONTEXT_RESOURCE, MAAGE_OPERATION_RESOLVE, 0);
    memcpy(presentation->uv, resources->uv[presentation->texture_id],
        sizeof(presentation->uv));
    /* Preserve the canonical fallback's magenta/black texels without biome
     * multiplication or decorative overlays. */
    if (presentation->texture_id == MAAGE_CONTENT_TEXTURE_MISSING ||
        maage_status_failed(resources->source_results[
            maage_client_texture_source((int)presentation->texture_id)]))
        return maage_status_make(MAAGE_STATUS_SUCCESS, 0, MAAGE_OPERATION_RESOLVE, 0);
    if (block == MAAGE_BLOCK_GRASS) {
        if (face == MAAGE_FACE_POSITIVE_Y) presentation->tint_mode = MAAGE_BLOCK_TINT_GRASS;
        else if (face != MAAGE_FACE_NEGATIVE_Y) {
            if (!maage_status_failed(resources->source_results[4])) {
                memcpy(presentation->overlay_uv, resources->uv[MAAGE_CONTENT_TEXTURE_GRASS_SIDE_OVERLAY],
                    sizeof(presentation->overlay_uv));
                presentation->overlay_tint_mode = MAAGE_BLOCK_TINT_GRASS;
            }
            if (!maage_status_failed(resources->source_results[61]))
                memcpy(presentation->snow_uv, resources->uv[MAAGE_CONTENT_TEXTURE_GRASS_SIDE_SNOW],
                    sizeof(presentation->snow_uv));
        }
    } else if (block == MAAGE_BLOCK_TALL_GRASS && metadata)
        presentation->tint_mode = MAAGE_BLOCK_TINT_GRASS_LOCAL;
    else if (block == MAAGE_BLOCK_WATER || block == MAAGE_BLOCK_LAVA) {
        memcpy(presentation->animation, resources->fluid_animation[block == MAAGE_BLOCK_LAVA],
            sizeof(presentation->animation));
        memcpy(presentation->flow_uv, resources->fluid_flow_uv[block == MAAGE_BLOCK_LAVA],
            sizeof(presentation->flow_uv));
        memcpy(presentation->flow_animation, resources->fluid_flow_animation[block == MAAGE_BLOCK_LAVA],
            sizeof(presentation->flow_animation));
    }
    if (block == MAAGE_BLOCK_LEAVES) {
        metadata &= MAAGE_LEAF_SPECIES_MASK;
        if (metadata == 1 || metadata == 2) {
            color = leaf_colors[metadata - 1];
            for (channel = 0; channel < 3; ++channel)
                presentation->tint[channel] = (float)((color >> ((2 - channel) * 8)) & 255UL) / 255;
        } else presentation->tint_mode = MAAGE_BLOCK_TINT_FOLIAGE;
        fast_tile = metadata == 1 ? MAAGE_CONTENT_TEXTURE_LEAVES_OPAQUE_SPRUCE :
            (metadata == 2 ? MAAGE_CONTENT_TEXTURE_LEAVES_OPAQUE_BIRCH :
                MAAGE_CONTENT_TEXTURE_LEAVES_OPAQUE_OAK);
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

void maage_client_item_face_tint(const float grass_color[3],
    const maage_block_face_definition *face, unsigned int mode, float color[4])
{
    static const float foliage_color[3] = {72.0f / 255, 181.0f / 255, 24.0f / 255};
    int channel;
    for (channel = 0; channel < 3; ++channel) {
        color[channel] = face->tint[channel];
        if (mode == MAAGE_BLOCK_TINT_GRASS || mode == MAAGE_BLOCK_TINT_GRASS_LOCAL)
            color[channel] *= grass_color[channel];
        else if (mode == MAAGE_BLOCK_TINT_FOLIAGE)
            color[channel] *= foliage_color[channel];
    }
    color[3] = face->tint[3];
}

const maage_block_state_definition *maage_client_item_icon_state(
    const maage_block_registry *blocks, const maage_item_definition *item,
    unsigned int metadata)
{
    const maage_block_state_definition *state;
    unsigned int species = metadata & 3;
    if (!item) return NULL;
    state = maage_block_registry_state(blocks, item->icon_state_id);
    if (state && (state->block_id == MAAGE_BLOCK_LOG || state->block_id == MAAGE_BLOCK_LEAVES) &&
        (species == 1 || species == 2))
        state = maage_block_registry_state(blocks, (state->block_id == MAAGE_BLOCK_LOG ?
            MAAGE_STATE_SPRUCE_LOG : MAAGE_STATE_SPRUCE_LEAVES) + species - 1);
    if (state && state->block_id == MAAGE_BLOCK_PLANKS && (species == 1 || species == 2))
        state = maage_block_registry_state(blocks, MAAGE_STATE_SPRUCE_PLANKS + species - 1);
    return state;
}

maage_status maage_client_resources_destroy(maage_client_resources *resources)
{
    if (!resources) return maage_status_make(MAAGE_STATUS_NOOP, 0, MAAGE_OPERATION_DESTROY, 0);
    maage_image_destroy(&resources->image);
    memset(resources, 0, sizeof(*resources));
    return maage_status_make(MAAGE_STATUS_SUCCESS, 0, MAAGE_OPERATION_DESTROY, 0);
}

maage_texture_id maage_client_item_texture(const maage_item_definition *item, unsigned int metadata)
{
    if (!item) return MAAGE_CONTENT_TEXTURE_MISSING;
    if (item->id == MAAGE_ITEM_SAPLING && (metadata == 1 || metadata == 2))
        return MAAGE_CONTENT_TEXTURE_ITEMS + 25 + metadata;
    if (item->id == MAAGE_ITEM_COAL && metadata == 1) return MAAGE_CONTENT_TEXTURE_ITEMS + 34;
    return item->texture_id;
}

const char *maage_client_item_name(const maage_item_registry *items, const maage_item_stack *stack)
{
    static const char *const logs[] = {"Oak Log", "Spruce Log", "Birch Log"};
    static const char *const leaves[] = {"Oak Leaves", "Spruce Leaves", "Birch Leaves"};
    static const char *const planks[] = {"Oak Planks", "Spruce Planks", "Birch Planks"};
    static const char *const saplings[] = {"Oak Sapling", "Spruce Sapling", "Birch Sapling"};
    const maage_item_definition *item = stack ? maage_item_registry_get(items, stack->item_id) : NULL;
    if (!item) return "";
    if (stack->metadata <= 2) {
        if (item->has_block && item->block_id == MAAGE_BLOCK_LOG) return logs[stack->metadata];
        if (item->has_block && item->block_id == MAAGE_BLOCK_LEAVES) return leaves[stack->metadata];
        if (item->has_block && item->block_id == MAAGE_BLOCK_PLANKS) return planks[stack->metadata];
        if (item->id == MAAGE_ITEM_COAL && stack->metadata == 1) return "Charcoal";
        if (item->id == MAAGE_ITEM_SAPLING) return saplings[stack->metadata];
    }
    return item->display_name;
}
