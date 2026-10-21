#include "internal.h"
#include "maage/minecraft_assets.h"
#include "maage/storage.h"
#include "maage/log.h"
#include <limits.h>
#include <stdlib.h>
#include <string.h>

int maage_client_ui_resources_load(maage_client_ui_resources *resources,
    maage_renderer *renderer, const char *application_data, const maage_pixel_buffer *block_atlas)
{
    static const char *const names[4] = {
        "minecraft/textures/gui/icons.png", "minecraft/textures/gui/widgets.png",
        "minecraft/textures/font/ascii.png", "minecraft/textures/gui/container/inventory.png"
    };
    static const int origins[4][2] = {{0, 0}, {256, 0}, {0, 256}, {256, 256}};
    maage_image sources[4], atlas;
    maage_pixel_buffer pixels;
    maage_render_result result;
    char path[MAAGE_PATH_CAPACITY];
    maage_render_capabilities capabilities;
    int i, y, expected, ok = 0, edge, width_needed, height_needed;
    memset(resources, 0, sizeof(*resources));
    if (!block_atlas || !block_atlas->pixels || block_atlas->width <= 0 ||
        block_atlas->height <= 0 || block_atlas->width >= INT_MAX ||
        block_atlas->height >= INT_MAX - MAAGE_CLIENT_UI_BLOCK_ATLAS_Y) {
        maage_log_error("maage.client.ui", "Invalid GUI block atlas dimensions.");
        return 0;
    }
    /* Keep the 1.7.10 sprites at their original coordinates, enlarge the
     * atlas only as needed, and reserve the final pixel for solid panels. */
    width_needed = block_atlas->width > 512 ? block_atlas->width : 512;
    ++width_needed;
    height_needed = MAAGE_CLIENT_UI_BLOCK_ATLAS_Y + block_atlas->height + 1;
    edge = 1024;
    while (edge < width_needed || edge < height_needed) {
        if (edge > INT_MAX / 2) {
            maage_log_error("maage.client.ui", "GUI atlas dimensions overflow.");
            return 0;
        }
        edge *= 2;
    }
    if (maage_renderer_get_capabilities(renderer, &capabilities) == MAAGE_RENDER_OK &&
        capabilities.max_texture_dimension_2d &&
        (unsigned long)edge > capabilities.max_texture_dimension_2d) {
        maage_log_error("maage.client.ui", "GUI atlas %dx%d exceeds renderer texture limit %u.",
            edge, edge, capabilities.max_texture_dimension_2d);
        return 0;
    }
    if ((size_t)edge > ((size_t)-1) / 4 ||
        (size_t)edge > ((size_t)-1) / ((size_t)edge * 4)) {
        maage_log_error("maage.client.ui", "GUI atlas allocation size overflows.");
        return 0;
    }
    resources->atlas_edge = edge;
    resources->block_atlas_width = block_atlas->width;
    resources->block_atlas_height = block_atlas->height;
    memset(sources, 0, sizeof(sources)); memset(&atlas, 0, sizeof(atlas));
    for (i = 0; i < 4; ++i) {
        expected = i == 2 ? 128 : 256;
        if (!maage_minecraft_asset_path(application_data, names[i], path, sizeof(path)) ||
            !maage_image_load_png(path, &sources[i])) {
            maage_log_error("maage.client.ui", "Missing or invalid Minecraft 1.7.10 GUI resource: %s. Repair resources with the Launcher.", names[i]);
            goto done;
        }
        if (sources[i].width != expected || sources[i].height != expected) {
            maage_log_error("maage.client.ui", "Unsupported dimensions for %s: %dx%d; expected Minecraft 1.7.10 %dx%d.",
                names[i], sources[i].width, sources[i].height, expected, expected);
            goto done;
        }
    }
    atlas.width = atlas.height = edge;
    atlas.stride = (size_t)atlas.width * 4;
    atlas.pixels = (unsigned char *)calloc((size_t)atlas.height, atlas.stride);
    if (!atlas.pixels) goto done;
    for (i = 0; i < 4; ++i) for (y = 0; y < sources[i].height; ++y)
        memcpy(atlas.pixels + (size_t)(origins[i][1] + y) * atlas.stride + (size_t)origins[i][0] * 4,
            sources[i].pixels + (size_t)y * sources[i].stride, (size_t)sources[i].width * 4);
    for (y = 0; y < block_atlas->height; ++y)
        memcpy(atlas.pixels + (size_t)(MAAGE_CLIENT_UI_BLOCK_ATLAS_Y + y) * atlas.stride,
            block_atlas->pixels + (size_t)y * block_atlas->stride, (size_t)block_atlas->width * 4);
    memset(atlas.pixels + (size_t)(edge - 1) * atlas.stride +
        (size_t)(edge - 1) * 4, 255, 4);
    maage_client_ui_font_initialize(resources, &sources[2]);
    pixels.width = atlas.width; pixels.height = atlas.height;
    pixels.stride = atlas.stride; pixels.pixels = atlas.pixels;
    result = maage_renderer_create_texture(renderer, &pixels, &resources->texture);
    if (result != MAAGE_RENDER_OK) {
        maage_log_error("maage.client.ui", "GUI atlas upload failed: %s.", maage_render_result_string(result));
        goto done;
    }
    maage_log_info("maage.client.ui", "Resolved inventory, HUD, font and borrowed block images into one GUI atlas.");
    ok = 1;
done:
    for (i = 0; i < 4; ++i) maage_image_destroy(&sources[i]);
    maage_image_destroy(&atlas);
    return ok;
}
