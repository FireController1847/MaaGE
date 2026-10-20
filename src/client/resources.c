#include "resources.h"
#include "maage/minecraft_assets.h"
#include "maage/storage.h"
#include "maage/log.h"
#include <stdlib.h>
#include <string.h>

#define MAAGE_CLIENT_SOURCE_COUNT 14
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
    "minecraft/textures/blocks/sandstone_bottom.png"
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

int maage_client_resources_load(const char *application_data,
    maage_client_resources *resources, maage_biome_registry *biomes)
{
    static const int atlas_sources[11] = {0, 1, 2, 3, 6, 7, 8, 9, 10, 12, 13};
    maage_image sources[MAAGE_CLIENT_SOURCE_COUNT];
    maage_biome_definition *biome;
    char path[MAAGE_PATH_CAPACITY];
    const float *tint;
    unsigned char *pixel, *source, *overlay;
    unsigned int channel, alpha, value;
    int i, j, x, y, sx, sy, edge, tile, source_index, biome_id, ok = 0;
    if (!resources || !biomes || biomes->finalized) return 0;
    memset(sources, 0, sizeof(sources));
    memset(resources, 0, sizeof(*resources));
    /* External names are consumed here only; mesh faces retain numeric UVs. */
    for (i = 0; i < MAAGE_CLIENT_SOURCE_COUNT; ++i) {
        if (!maage_minecraft_asset_path(application_data,
                maage_client_texture_names[i], path, sizeof(path)) ||
            !maage_image_load_png(path, &sources[i])) {
            maage_log_error("maage.client.resources", "Missing or invalid Minecraft 1.7.10 PNG: %s. Prepare/repair resources with the Launcher.", maage_client_texture_names[i]);
            goto done;
        }
        maage_log_info("maage.client.resources", "Loaded %s (%dx%d).", maage_client_texture_names[i], sources[i].width, sources[i].height);
    }
    edge = sources[0].width;
    for (i = 0; i < MAAGE_CLIENT_SOURCE_COUNT; ++i) {
        if (i == 5 || i == 11) {
            if (sources[i].width == 256 && sources[i].height == 256) continue;
        } else if (sources[i].width == edge &&
            (sources[i].height == edge || (i == 7 && sources[i].height >= edge && sources[i].height % edge == 0))) continue;
        maage_log_error("maage.client.resources", "Invalid block texture or biome colormap dimensions: %s.", maage_client_texture_names[i]);
        goto done;
    }
    for (i = 0; i < MAAGE_BIOME_COUNT; ++i) {
        if (!biomes->occupied[i]) goto done;
        biome = &biomes->definitions[i];
        maage_client_colormap(&sources[5], biome->temperature, biome->rainfall, biome->grass_color);
        maage_client_colormap(&sources[11], biome->temperature, biome->rainfall, biome->foliage_color);
    }
    tile = edge + 2;
    resources->image.width = tile * MAAGE_CLIENT_TEXTURE_COUNT;
    resources->image.height = tile;
    resources->image.stride = (size_t)resources->image.width * 4;
    resources->image.pixels = (unsigned char *)malloc(resources->image.stride * (size_t)tile);
    if (!resources->image.pixels) goto done;
    for (i = 0; i < MAAGE_CLIENT_TEXTURE_COUNT; ++i) {
        biome_id = i >= 11 ? (i - 11) / 2 : MAAGE_BIOME_PLAINS;
        source_index = i >= 11 ? ((i - 11) % 2 ? 3 : 2) : atlas_sources[i];
        tint = biomes->definitions[biome_id].grass_color;
        for (y = 0; y < tile; ++y) {
            sy = y == 0 ? 0 : (y > edge ? edge - 1 : y - 1);
            for (x = 0; x < tile; ++x) {
                sx = x == 0 ? 0 : (x > edge ? edge - 1 : x - 1);
                pixel = resources->image.pixels + (size_t)y * resources->image.stride + (size_t)(i * tile + x) * 4;
                source = sources[source_index].pixels + (size_t)sy * sources[source_index].stride + (size_t)sx * 4;
                memcpy(pixel, source, 4);
                if (source_index == 2)
                    for (channel = 0; channel < 3; ++channel)
                        pixel[channel] = (unsigned char)((float)pixel[channel] * tint[channel]);
                if (source_index == 3) {
                    overlay = sources[4].pixels + (size_t)sy * sources[4].stride + (size_t)sx * 4;
                    alpha = overlay[3];
                    for (channel = 0; channel < 3; ++channel) {
                        value = (unsigned int)((float)overlay[channel] * tint[channel]);
                        pixel[channel] = (unsigned char)((value * alpha + (unsigned int)pixel[channel] * (255 - alpha) + 127) / 255);
                    }
                }
                if (source_index == 7) {
                    /* Static first animation frame; opaque water avoids ordering
                     * artifacts until transparent materials are implemented. */
                    pixel[0] = (unsigned char)((unsigned int)pixel[0] * 63 / 255);
                    pixel[1] = (unsigned char)((unsigned int)pixel[1] * 118 / 255);
                    pixel[2] = (unsigned char)((unsigned int)pixel[2] * 228 / 255);
                    pixel[3] = 255;
                } else if (pixel[3] != 255) {
                    maage_log_error("maage.client.resources", "Opaque block texture has transparent texels: %s.", maage_client_texture_names[source_index]);
                    goto done;
                }
            }
        }
        resources->uv[i][0] = (float)(i * tile + 1) / resources->image.width;
        resources->uv[i][1] = 1.0f / tile;
        resources->uv[i][2] = (float)(i * tile + 1 + edge) / resources->image.width;
        resources->uv[i][3] = (float)(1 + edge) / tile;
    }
    resources->atlas.pixels = resources->image.pixels;
    resources->atlas.width = resources->image.width;
    resources->atlas.height = resources->image.height;
    resources->atlas.stride = resources->image.stride;
    ok = 1;
done:
    for (j = 0; j < MAAGE_CLIENT_SOURCE_COUNT; ++j) maage_image_destroy(&sources[j]);
    if (!ok) maage_client_resources_destroy(resources);
    return ok;
}

static int maage_client_register_state(maage_block_registry *registry,
    const maage_client_resources *resources, maage_block_state_id id,
    maage_block_id block, unsigned int metadata, int texture, int grass_biome)
{
    maage_block_state_definition state;
    int face, channel, selected;
    memset(&state, 0, sizeof(state));
    state.id = id; state.block_id = block; state.metadata = metadata;
    for (face = 0; face < MAAGE_FACE_COUNT; ++face) {
        selected = texture;
        if (block == MAAGE_BLOCK_GRASS) {
            selected = grass_biome >= 0 ? MAAGE_CLIENT_TEXTURE_BIOME_SIDE(grass_biome) : MAAGE_CLIENT_TEXTURE_GRASS_SIDE;
            if (face == MAAGE_FACE_NEGATIVE_Y) selected = MAAGE_CLIENT_TEXTURE_DIRT;
            if (face == MAAGE_FACE_POSITIVE_Y)
                selected = grass_biome >= 0 ? MAAGE_CLIENT_TEXTURE_BIOME_TOP(grass_biome) : MAAGE_CLIENT_TEXTURE_GRASS_TOP;
        }
        if (block == MAAGE_BLOCK_SANDSTONE) {
            if (face == MAAGE_FACE_POSITIVE_Y) selected = MAAGE_CLIENT_TEXTURE_SANDSTONE_TOP;
            if (face == MAAGE_FACE_NEGATIVE_Y) selected = MAAGE_CLIENT_TEXTURE_SANDSTONE_BOTTOM;
        }
        state.faces[face].texture_id = (maage_texture_id)selected;
        memcpy(state.faces[face].uv, resources->uv[selected], sizeof(state.faces[face].uv));
        for (channel = 0; channel < 4; ++channel) state.faces[face].tint[channel] = 1.0f;
    }
    return maage_block_registry_register_state(registry, &state) == MAAGE_REGISTRY_OK;
}

maage_block_registry *maage_client_register_blocks(
    const maage_client_resources *resources, const maage_biome_registry *biomes)
{
    static const char *const names[] = {"minecraft:air", "minecraft:stone", "minecraft:grass", "minecraft:dirt", "minecraft:bedrock", "minecraft:water", "minecraft:sand", "minecraft:gravel", "minecraft:sandstone"};
    static const maage_block_id ids[] = {0, 1, 2, 3, 7, 9, 12, 13, 24};
    static const int textures[] = {0, 0, 2, 1, 4, 5, 6, 7, 8};
    maage_block_registry *registry;
    maage_block_definition block;
    int i;
    if (!resources || !biomes) return NULL;
    registry = maage_block_registry_create(32, 512);
    if (!registry) return NULL;
    for (i = 0; i < 9; ++i) {
        block.id = ids[i];
        block.solid = block.opaque = ids[i] != MAAGE_BLOCK_AIR && ids[i] != MAAGE_BLOCK_WATER;
        block.emitted_light = 0;
        if (maage_block_registry_register_block(registry, names[i], &block) != MAAGE_REGISTRY_OK ||
            !maage_client_register_state(registry, resources, ids[i], ids[i], 0, textures[i], -1)) goto failed;
    }
    for (i = 0; i < MAAGE_BIOME_COUNT; ++i) {
        if (!biomes->occupied[i] || !maage_client_register_state(registry, resources,
                MAAGE_STATE_BIOME_GRASS(i), MAAGE_BLOCK_GRASS,
                (unsigned int)i + 1, MAAGE_CLIENT_TEXTURE_GRASS_TOP, i)) goto failed;
    }
    if (maage_block_registry_finalize(registry) != MAAGE_REGISTRY_OK) goto failed;
    maage_log_info("maage.client.world", "Finalized %lu block types / %lu states; direct numeric runtime indexing.",
        (unsigned long)maage_block_registry_block_count(registry), (unsigned long)maage_block_registry_state_count(registry));
    return registry;
failed:
    maage_block_registry_destroy(registry);
    return NULL;
}

void maage_client_resources_destroy(maage_client_resources *resources)
{
    if (!resources) return;
    maage_image_destroy(&resources->image);
    memset(resources, 0, sizeof(*resources));
}
