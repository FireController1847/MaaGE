#include "internal.h"
#include "maage/minecraft_assets.h"
#include "maage/storage.h"
#include "maage/log.h"
#include <limits.h>
#include <stdlib.h>
#include <string.h>

static maage_status maage_client_ui_decoration_fallback(maage_image *image, int index)
{
    unsigned char *pixel;
    maage_status result;
    int x, y;
    if (index == 2) return maage_client_ui_font_fallback(image);
    result = maage_image_make_missing(image, 256, 256);
    if (maage_status_failed(result)) return result;
    if (!index) {
        /* Missing icons are optional; retain an original white crosshair. */
        memset(image->pixels, 0, image->stride * (size_t)image->height);
        for (y = 0; y < 15; ++y) for (x = 0; x < 15; ++x)
            if (x == 7 || y == 7) {
                pixel = image->pixels + (size_t)y * image->stride + (size_t)x * 4;
                memset(pixel, 255, 4);
            }
    } else if (index == 1) {
        /* Keep buttons visually usable while the shared checker marks the
         * remaining unavailable sprite regions. */
        for (y = 66; y < 106; ++y) for (x = 0; x < 200; ++x) {
            pixel = image->pixels + (size_t)y * image->stride + (size_t)x * 4;
            pixel[0] = pixel[1] = pixel[2] = (unsigned char)(y < 86 ? 90 : 130);
            pixel[3] = 255;
        }
    }
    return result;
}

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
    maage_status status;
    char path[MAAGE_PATH_CAPACITY];
    maage_render_capabilities capabilities;
    int i, y, expected, ok = 0, edge, width_needed, height_needed;
    memset(resources, 0, sizeof(*resources));
    resources->load_status = maage_status_make(MAAGE_STATUS_SUCCESS, 0, MAAGE_OPERATION_LOAD_TEXTURE, 0);
    resources->recovery_status = maage_status_make(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_CONTEXT_GUI, MAAGE_OPERATION_ATLAS, 0);
    if (!block_atlas || !block_atlas->pixels || block_atlas->width <= 0 ||
        block_atlas->height <= 0 || block_atlas->width >= INT_MAX ||
        block_atlas->height >= INT_MAX - MAAGE_CLIENT_UI_BLOCK_ATLAS_Y) {
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
            resources->recovery_status = maage_status_make(MAAGE_STATUS_BUFFER_OVERFLOW, MAAGE_CONTEXT_GUI, MAAGE_OPERATION_ATLAS, 0);
            return 0;
        }
        edge *= 2;
    }
    if (maage_renderer_get_capabilities(renderer, &capabilities) == MAAGE_RENDER_OK &&
        capabilities.max_texture_dimension_2d &&
        (unsigned long)edge > capabilities.max_texture_dimension_2d) {
        resources->recovery_status = maage_status_make(MAAGE_STATUS_SYSTEM_NOT_SUPPORTED, MAAGE_CONTEXT_GUI, MAAGE_OPERATION_ATLAS, 0);
        return 0;
    }
    if ((size_t)edge > ((size_t)-1) / 4 ||
        (size_t)edge > ((size_t)-1) / ((size_t)edge * 4)) {
        resources->recovery_status = maage_status_make(MAAGE_STATUS_BUFFER_OVERFLOW, MAAGE_CONTEXT_GUI, MAAGE_OPERATION_ATLAS, 0);
        return 0;
    }
    resources->atlas_edge = edge;
    resources->block_atlas_width = block_atlas->width;
    resources->block_atlas_height = block_atlas->height;
    memset(sources, 0, sizeof(sources)); memset(&atlas, 0, sizeof(atlas));
    for (i = 0; i < 4; ++i) {
        expected = i == 2 ? 128 : 256;
        status = maage_minecraft_asset_path_status(application_data, names[i], path, sizeof(path));
        if (!maage_status_failed(status)) status = maage_image_load_png_status(path, &sources[i]);
        if (!maage_status_failed(status) && (sources[i].width != expected || sources[i].height != expected))
            status = maage_status_make(MAAGE_STATUS_MALFORMED_INPUT, MAAGE_CONTEXT_GUI, MAAGE_OPERATION_LOAD_TEXTURE, 0);
        resources->source_results[i] = status;
        if (maage_status_failed(status)) {
            if (!resources->failed_resources) resources->load_status = status;
            ++resources->failed_resources;
            maage_log_status(MAAGE_LOG_WARNING, "maage.client.ui", status,
                "%s; using %s. Repair resources with the Launcher.", names[i],
                i == 2 ? "the built-in minimal font" : "a generated decoration fallback");
            maage_image_destroy(&sources[i]);
            resources->recovery_status = maage_client_ui_decoration_fallback(&sources[i], i);
            if (maage_status_failed(resources->recovery_status)) goto done;
        }
    }
    atlas.width = atlas.height = edge;
    atlas.stride = (size_t)atlas.width * 4;
    atlas.pixels = (unsigned char *)calloc((size_t)atlas.height, atlas.stride);
    if (!atlas.pixels) {
        resources->recovery_status = maage_status_make(MAAGE_STATUS_ALLOCATION_FAILED, MAAGE_CONTEXT_GUI, MAAGE_OPERATION_ATLAS, 0);
        goto done;
    }
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
    resources->recovery_status = maage_render_operation_status(result, MAAGE_OPERATION_UPLOAD);
    if (result != MAAGE_RENDER_OK) {
        goto done;
    }
    maage_log_info("maage.client.ui", "Resolved inventory, HUD, font and borrowed block images into one GUI atlas.");
    ok = 1;
done:
    for (i = 0; i < 4; ++i) maage_image_destroy(&sources[i]);
    maage_image_destroy(&atlas);
    return ok;
}
