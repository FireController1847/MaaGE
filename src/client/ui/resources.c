#include "internal.h"
#include "maage/minecraft_assets.h"
#include "maage/storage.h"
#include "maage/log.h"
#include <stdlib.h>
#include <string.h>

int maage_client_ui_resources_load(maage_client_ui_resources *resources,
    maage_renderer *renderer, const char *application_data)
{
    static const char *const names[3] = {
        "minecraft/textures/gui/icons.png", "minecraft/textures/gui/widgets.png",
        "minecraft/textures/font/ascii.png"
    };
    static const int origins[3][2] = {{0, 0}, {256, 0}, {0, 256}};
    maage_image sources[3], atlas;
    maage_pixel_buffer pixels;
    maage_render_result result;
    char path[MAAGE_PATH_CAPACITY];
    int i, y, expected, ok = 0;
    memset(resources, 0, sizeof(*resources));
    memset(sources, 0, sizeof(sources)); memset(&atlas, 0, sizeof(atlas));
    for (i = 0; i < 3; ++i) {
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
    atlas.width = atlas.height = MAAGE_CLIENT_UI_ATLAS_EDGE;
    atlas.stride = (size_t)atlas.width * 4;
    atlas.pixels = (unsigned char *)calloc((size_t)atlas.height, atlas.stride);
    if (!atlas.pixels) goto done;
    for (i = 0; i < 3; ++i) for (y = 0; y < sources[i].height; ++y)
        memcpy(atlas.pixels + (size_t)(origins[i][1] + y) * atlas.stride + (size_t)origins[i][0] * 4,
            sources[i].pixels + (size_t)y * sources[i].stride, (size_t)sources[i].width * 4);
    memset(atlas.pixels + (size_t)511 * atlas.stride + 511 * 4, 255, 4);
    maage_client_ui_font_initialize(resources, &sources[2]);
    pixels.width = atlas.width; pixels.height = atlas.height;
    pixels.stride = atlas.stride; pixels.pixels = atlas.pixels;
    result = maage_renderer_create_texture(renderer, &pixels, &resources->texture);
    if (result != MAAGE_RENDER_OK) {
        maage_log_error("maage.client.ui", "GUI atlas upload failed: %s.", maage_render_result_string(result));
        goto done;
    }
    maage_log_info("maage.client.ui", "Resolved GUI icons, widgets and ASCII font into one shared atlas.");
    ok = 1;
done:
    for (i = 0; i < 3; ++i) maage_image_destroy(&sources[i]);
    maage_image_destroy(&atlas);
    return ok;
}
