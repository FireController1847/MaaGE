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

maage_status maage_client_ui_resources_load(maage_client_ui_resources *resources,
    maage_renderer *renderer, const char *application_data, const maage_pixel_buffer *block_atlas, maage_status_trace *trace)
{
    static const char *const names[4] = {
        "minecraft/textures/gui/icons.png", "minecraft/textures/gui/widgets.png",
        "minecraft/textures/font/ascii.png", "minecraft/textures/gui/container/inventory.png"
    };
    static const int origins[4][2] = {{0, 0}, {256, 0}, {0, 256}, {256, 256}};
    maage_image sources[4], title_images[8], atlas;
    maage_pixel_buffer pixels;
    maage_status result;
    maage_status status;
    maage_status_trace source_trace, recovered_trace;
    char path[MAAGE_PATH_CAPACITY];
    maage_render_capabilities capabilities;
    int i, y, expected, ok = 0, edge, width_needed, height_needed;
    memset(resources, 0, sizeof(*resources));
    resources->load_status = maage_status_make(MAAGE_STATUS_SUCCESS, 0, MAAGE_OPERATION_LOAD_TEXTURE, 0);
    resources->recovery_status = maage_status_make(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_CONTEXT_GUI, MAAGE_OPERATION_ATLAS, 0);
    if (!block_atlas || !block_atlas->pixels || block_atlas->width <= 0 ||
        block_atlas->height <= 0 || block_atlas->width >= INT_MAX ||
        block_atlas->height >= INT_MAX - MAAGE_CLIENT_UI_BLOCK_ATLAS_Y) {
        return MAAGE_STATUS_PROPAGATE(trace, resources->recovery_status, MAAGE_CONTEXT_GUI, MAAGE_OPERATION_ATLAS);
    }
    /* Keep the 1.7.10 sprites at their original coordinates, enlarge the
     * atlas only as needed, and reserve the final pixel for solid panels. */
    width_needed = block_atlas->width > 512 ? block_atlas->width : 512;
    ++width_needed;
    height_needed = MAAGE_CLIENT_UI_BLOCK_ATLAS_Y + block_atlas->height + 1;
    edge = 2048;
    while (edge < width_needed || edge < height_needed) {
        if (edge > INT_MAX / 2) {
            resources->recovery_status = maage_status_make(MAAGE_STATUS_BUFFER_OVERFLOW, MAAGE_CONTEXT_GUI, MAAGE_OPERATION_ATLAS, 0);
            return MAAGE_STATUS_PROPAGATE(trace, resources->recovery_status, MAAGE_CONTEXT_GUI, MAAGE_OPERATION_ATLAS);
        }
        edge *= 2;
    }
    result = maage_renderer_get_capabilities(renderer, &capabilities);
    if (maage_status_failed(result)) {
        resources->recovery_status = result;
        return MAAGE_STATUS_PROPAGATE(trace, result, MAAGE_CONTEXT_GUI, MAAGE_OPERATION_ATLAS);
    }
    if (capabilities.max_texture_dimension_2d &&
        (unsigned long)edge > capabilities.max_texture_dimension_2d) {
        resources->recovery_status = maage_status_make(MAAGE_STATUS_SYSTEM_NOT_SUPPORTED, MAAGE_CONTEXT_GUI, MAAGE_OPERATION_ATLAS, 0);
        return MAAGE_STATUS_PROPAGATE(trace, resources->recovery_status, MAAGE_CONTEXT_GUI, MAAGE_OPERATION_ATLAS);
    }
    if ((size_t)edge > ((size_t)-1) / 4 ||
        (size_t)edge > ((size_t)-1) / ((size_t)edge * 4)) {
        resources->recovery_status = maage_status_make(MAAGE_STATUS_BUFFER_OVERFLOW, MAAGE_CONTEXT_GUI, MAAGE_OPERATION_ATLAS, 0);
        return MAAGE_STATUS_PROPAGATE(trace, resources->recovery_status, MAAGE_CONTEXT_GUI, MAAGE_OPERATION_ATLAS);
    }
    resources->atlas_edge = edge;
    resources->block_atlas_width = block_atlas->width;
    resources->block_atlas_height = block_atlas->height;
    memset(sources, 0, sizeof(sources)); memset(title_images,0,sizeof(title_images));
    memset(&atlas, 0, sizeof(atlas));
    for (i = 0; i < 4; ++i) {
        maage_status_trace_init(&source_trace);
        MAAGE_STATUS_TRACE(&source_trace, 0, MAAGE_CONTEXT_CLIENT, MAAGE_OPERATION_INITIALIZE);
        MAAGE_STATUS_TRACE(&source_trace, 0, MAAGE_CONTEXT_GUI, MAAGE_OPERATION_ATLAS);
        expected = i == 2 ? 128 : 256;
        status = maage_minecraft_asset_path(application_data, names[i], path, sizeof(path), &source_trace);
        if (!maage_status_failed(status)) status = maage_image_load_png(path, &sources[i], &source_trace);
        if (!maage_status_failed(status) && (sources[i].width != expected || sources[i].height != expected))
            status = maage_status_make(MAAGE_STATUS_MALFORMED_INPUT, MAAGE_CONTEXT_GUI, MAAGE_OPERATION_LOAD_TEXTURE, 0);
        resources->source_results[i] = status;
        if (maage_status_failed(status)) {
            if (!resources->failed_resources) resources->load_status = status;
            ++resources->failed_resources;
            MAAGE_STATUS_TRACE(&source_trace, status, MAAGE_CONTEXT_GUI, MAAGE_OPERATION_LOAD_TEXTURE);
            maage_image_destroy(&sources[i]);
            resources->recovery_status = maage_client_ui_decoration_fallback(&sources[i], i);
            maage_status_trace_recover(&source_trace, resources->recovery_status, MAAGE_CONTEXT_GUI, MAAGE_OPERATION_ATLAS, __FILE__, __LINE__);
            if (maage_status_failed(resources->recovery_status)) {
                if (trace) *trace = source_trace;
                goto done;
            }
            maage_log_trace_status(MAAGE_LOG_WARNING, "maage.client.ui", status, &source_trace,
                "%s; using %s. Repair resources with the Launcher.", names[i],
                i == 2 ? "the built-in minimal font" : "a generated decoration fallback");
            if (resources->failed_resources == 1) recovered_trace = source_trace;
        }
    }
    /* Only the programmer's verified installation supplies title resources. */
    {
        static const char *const names_title[8] = {
            "minecraft/textures/gui/title/minecraft.png",
            "minecraft/textures/gui/title/background/panorama_0.png",
            "minecraft/textures/gui/title/background/panorama_1.png",
            "minecraft/textures/gui/title/background/panorama_2.png",
            "minecraft/textures/gui/title/background/panorama_3.png",
            "minecraft/textures/gui/title/background/panorama_4.png",
            "minecraft/textures/gui/title/background/panorama_5.png",
            "minecraft/textures/gui/language.png"
        };
        for(i=0;i<8;++i) {
            maage_status_trace_init(&source_trace);
            status=maage_minecraft_asset_path(application_data,names_title[i],path,sizeof(path),&source_trace);
            if(!maage_status_failed(status))status=maage_image_load_png(path,&title_images[i],&source_trace);
            if(!maage_status_failed(status)&&(title_images[i].width<=0||
                title_images[i].height<=0||title_images[i].width>256||title_images[i].height>256))
                status=maage_status_make(MAAGE_STATUS_MALFORMED_INPUT,MAAGE_CONTEXT_GUI,MAAGE_OPERATION_LOAD_TEXTURE,0);
            if(maage_status_failed(status)) {
                maage_image_destroy(&title_images[i]);
                maage_log_status(MAAGE_LOG_WARNING,"maage.client.ui",status,
                    "Original title resource unavailable: %s.",names_title[i]);
            }else resources->title_available[i]=1;
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
    for(i=0;i<8;++i)if(resources->title_available[i])for(y=0;y<title_images[i].height;++y)
        memcpy(atlas.pixels+(size_t)(((i/4)*256)+y)*atlas.stride+
            (size_t)(512+(i%4)*256)*4,
            title_images[i].pixels+(size_t)y*title_images[i].stride,
            (size_t)title_images[i].width*4);
    memset(atlas.pixels + (size_t)(edge - 1) * atlas.stride +
        (size_t)(edge - 1) * 4, 255, 4);
    resources->recovery_status = maage_client_ui_font_initialize(resources, &sources[2]);
    if (maage_status_failed(resources->recovery_status)) goto done;
    pixels.width = atlas.width; pixels.height = atlas.height;
    pixels.stride = atlas.stride; pixels.pixels = atlas.pixels;
    result = maage_renderer_create_texture(renderer, &pixels, &resources->texture, trace);
    resources->recovery_status = result;
    if (maage_status_code(result) != MAAGE_STATUS_SUCCESS) {
        goto done;
    }
    maage_log_info("maage.client.ui", "Resolved inventory, HUD, font and borrowed block images into one GUI atlas.");
    ok = 1;
done:
    for (i = 0; i < 4; ++i) maage_image_destroy(&sources[i]);
    for(i=0;i<8;++i)maage_image_destroy(&title_images[i]);
    maage_image_destroy(&atlas);
    if (!ok) return MAAGE_STATUS_PROPAGATE(trace, resources->recovery_status, MAAGE_CONTEXT_GUI, MAAGE_OPERATION_INITIALIZE);
    if (trace && resources->failed_resources) *trace = recovered_trace;
    return MAAGE_STATUS_PROPAGATE(trace, maage_status_make(resources->failed_resources ? MAAGE_STATUS_PARTIAL : MAAGE_STATUS_SUCCESS,
        MAAGE_CONTEXT_GUI, MAAGE_OPERATION_INITIALIZE, 0), MAAGE_CONTEXT_GUI, MAAGE_OPERATION_INITIALIZE);
}
