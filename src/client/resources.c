#include "resources.h"
#include "maage/minecraft_assets.h"
#include "maage/storage.h"
#include "maage/log.h"
#include <stdlib.h>
#include <string.h>

static const char *const maage_client_texture_names[] = {
    "minecraft/textures/blocks/stone.png",
    "minecraft/textures/blocks/dirt.png",
    "minecraft/textures/blocks/grass_top.png",
    "minecraft/textures/blocks/grass_side.png",
    "minecraft/textures/blocks/grass_side_overlay.png",
    "minecraft/textures/colormap/grass.png"
};

int maage_client_resources_load(const char *application_data,
    maage_client_resources *resources)
{
    maage_image sources[6];
    char path[MAAGE_PATH_CAPACITY];
    const unsigned char *tint;
    unsigned char *pixel, *source, *overlay;
    unsigned int channel, alpha, value;
    int i, x, y, sx, sy, edge, tile, ok = 0;
    memset(sources, 0, sizeof(sources));
    memset(resources, 0, sizeof(*resources));
    /* Names are interpreted once at startup. Runtime faces keep only atlas UVs. */
    for (i = 0; i < 6; ++i) {
        if (!maage_minecraft_asset_path(application_data, maage_client_texture_names[i], path, sizeof(path)) ||
            !maage_image_load_png(path, &sources[i])) {
            maage_log_error("maage.client.resources", "Missing or invalid Minecraft 1.7.10 PNG: %s. Prepare/repair resources with the Launcher.", maage_client_texture_names[i]);
            goto done;
        }
        maage_log_info("maage.client.resources", "Loaded %s (%dx%d).", maage_client_texture_names[i], sources[i].width, sources[i].height);
    }
    edge = sources[0].width;
    for (i = 0; i < 5; ++i) {
        if (sources[i].width != edge || sources[i].height != edge) {
            maage_log_error("maage.client.resources", "Block PNGs must be equally sized square textures.");
            goto done;
        }
    }
    if (sources[5].width != 256 || sources[5].height != 256) {
        maage_log_error("maage.client.resources", "The grass colormap must be 256x256.");
        goto done;
    }
    /* Fixed temperate colormap sample until biomes exist; overlay is tinted at
     * load time. Neither this approximation nor face shading is biome lighting. */
    tint = sources[5].pixels + 173 * sources[5].stride + 51 * 4;
    tile = edge + 2;
    resources->image.width = tile * MAAGE_CLIENT_TEXTURE_COUNT;
    resources->image.height = tile;
    resources->image.stride = (size_t)resources->image.width * 4;
    resources->image.pixels = (unsigned char *)malloc(resources->image.stride * (size_t)tile);
    if (!resources->image.pixels) goto done;
    for (i = 0; i < MAAGE_CLIENT_TEXTURE_COUNT; ++i) {
        for (y = 0; y < tile; ++y) {
            sy = y == 0 ? 0 : (y > edge ? edge - 1 : y - 1);
            for (x = 0; x < tile; ++x) {
                sx = x == 0 ? 0 : (x > edge ? edge - 1 : x - 1);
                pixel = resources->image.pixels + (size_t)y * resources->image.stride + (size_t)(i * tile + x) * 4;
                source = sources[i].pixels + (size_t)sy * sources[i].stride + (size_t)sx * 4;
                if (source[3] != 255) {
                    maage_log_error("maage.client.resources", "Opaque block texture contains transparent texels: %s.", maage_client_texture_names[i]);
                    goto done;
                }
                memcpy(pixel, source, 4);
                if (i == MAAGE_CLIENT_TEXTURE_GRASS_TOP)
                    for (channel = 0; channel < 3; ++channel) pixel[channel] = (unsigned char)((unsigned int)pixel[channel] * tint[channel] / 255);
                if (i == MAAGE_CLIENT_TEXTURE_GRASS_SIDE) {
                    overlay = sources[4].pixels + (size_t)sy * sources[4].stride + (size_t)sx * 4;
                    alpha = overlay[3];
                    for (channel = 0; channel < 3; ++channel) {
                        value = (unsigned int)overlay[channel] * tint[channel] / 255;
                        pixel[channel] = (unsigned char)((value * alpha + (unsigned int)pixel[channel] * (255 - alpha) + 127) / 255);
                    }
                }
            }
        }
        /* One extruded texel gutter prevents adjacent-tile nearest samples. */
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
    for (i = 0; i < 6; ++i) maage_image_destroy(&sources[i]);
    if (!ok) maage_client_resources_destroy(resources);
    return ok;
}

void maage_client_resources_destroy(maage_client_resources *resources)
{
    maage_image_destroy(&resources->image);
    memset(resources, 0, sizeof(*resources));
}
