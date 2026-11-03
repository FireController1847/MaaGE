#include "resources.h"
#include "maage/minecraft_assets.h"
#include "maage/storage.h"
#include "maage/log.h"
#include <limits.h>
#include <stdlib.h>
#include <string.h>

maage_status maage_client_resources_set_window_icon(const char *application_data, maage_window *window, maage_status_trace *trace) {
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
            pixels[i].width = images[i].width;
            pixels[i].height = images[i].height;
            pixels[i].stride = images[i].stride;
        }
        icon.images = pixels;
        icon.image_count = 2;
    }
    result = maage_window_set_icon(window, &icon);
done:
    for (i = 0; i < 2; ++i)
        (void) maage_image_destroy(&images[i]);
    return MAAGE_STATUS_PROPAGATE(trace, result, MAAGE_CONTEXT_CLIENT, MAAGE_OPERATION_SET);
}

static int maage_client_texture_source(const maage_client_resources *resources, maage_texture_id texture) {
    const maage_texture_definition *definition = maage_presentation_texture(maage_content_presentation(resources->content), texture);
    return definition && definition->source != MAAGE_CONTENT_ID_INVALID ? (int) definition->source : -1;
}
static const char *maage_client_source_name(const maage_client_resources *resources, unsigned long source) {
    return maage_presentation_source(maage_content_presentation(resources->content), source);
}
static int maage_client_source_sheet(const maage_presentation_registry *registry, unsigned long source, unsigned int *width, unsigned int *height) {
    const maage_texture_definition *texture;
    size_t i;
    for (i = 0; i < maage_presentation_texture_count(registry); ++i) {
        texture = maage_presentation_texture(registry, (maage_texture_id) i);
        if (texture->source == source && texture->sheet_width) {
            *width = texture->sheet_width;
            *height = texture->sheet_height;
            return 1;
        }
    }
    return 0;
}

static void maage_client_colormap(const maage_image *map, double temperature, double rainfall, float color[3]) {
    const unsigned char *pixel;
    int x, y, channel;
    if (temperature < 0) temperature = 0;
    if (temperature > 1) temperature = 1;
    if (rainfall < 0) rainfall = 0;
    if (rainfall > 1) rainfall = 1;
    rainfall *= temperature;
    x = (int) ((1.0 - temperature) * 255.0);
    y = (int) ((1.0 - rainfall) * 255.0);
    pixel = map->pixels + (size_t) y * map->stride + (size_t) x * 4;
    for (channel = 0; channel < 3; ++channel)
        color[channel] = (float) pixel[channel] / 255.0f;
}

static void maage_client_resource_failure(maage_client_resources *resources, int index, maage_status result, maage_status_trace *trace) {
    resources->source_results[index] = result;
    if (!resources->failed_resources) resources->load_status = result;
    ++resources->failed_resources;
    MAAGE_STATUS_TRACE(trace, result, MAAGE_CONTEXT_RESOURCE, MAAGE_OPERATION_LOAD_TEXTURE);
    maage_status_trace_recover(trace, maage_status_make(MAAGE_STATUS_SUCCESS, 0, MAAGE_OPERATION_ATLAS, 0), MAAGE_CONTEXT_RESOURCE, MAAGE_OPERATION_ATLAS, __FILE__, __LINE__);
    maage_log_trace_status(MAAGE_LOG_WARNING, "maage.client.resources", result, trace, "%s; using an initialized visual fallback. Repair resources with the Launcher.", maage_client_source_name(resources, (unsigned long) index));
}

typedef struct maage_client_animation_region {
    size_t texture;
    int x, y, width, height;
} maage_client_animation_region;
static int maage_client_animation_order(const void *left, const void *right) {
    const maage_client_animation_region *a = (const maage_client_animation_region *) left;
    const maage_client_animation_region *b = (const maage_client_animation_region *) right;
    if (a->height != b->height) return a->height > b->height ? -1 : 1;
    return a->texture < b->texture ? -1 : a->texture != b->texture;
}

static maage_status maage_client_content_resolve(void *, const maage_block_state_definition *, maage_block_face_definition[MAAGE_FACE_COUNT], float[4]);
static maage_status maage_client_content_resolve_appearance(void *, const maage_block_face_definition *, maage_block_face_definition *);

maage_status maage_client_resources_load(const char *application_data, const maage_content *content, maage_client_resources *resources, const maage_skin_reference *player, maage_client_resource_checkpoint checkpoint, void *context, maage_status_trace *trace) {
    static const float default_grass[3] = {0.50f, 0.72f, 0.35f};
    static const float default_foliage[3] = {0.35f, 0.66f, 0.30f};
    maage_image *sources = NULL, missing;
    maage_client_animation_region *regions = NULL;
    size_t region_count = 0, region_index;
    const maage_texture_definition *texture;
    const maage_presentation_registry *catalog;
    const maage_presentation_bindings *bindings;
    maage_biome_definition tinted;
    const maage_image *selected;
    maage_biome_definition *biome;
    maage_status result;
    maage_content_view_config view_config;
    maage_status_trace source_trace, recovered_trace;
    char path[MAAGE_PATH_CAPACITY];
    unsigned char *pixel, *source;
    int i, j, x, y, sx, sy, edge = 16, tile, source_index, stage, ok = 0;
    int columns = 32, rows, tile_x, tile_y, skin_y;
    int frame, animation_y, frame_edge, frame_tile, custom_height = 0, custom_x, custom_y, custom_frames, shelf_x, shelf_y, shelf_height;
    unsigned int sheet_width = 0, sheet_height = 0;
    if (!resources || !content || !maage_block_registry_is_finalized(maage_content_blocks(content))) return maage_status_make(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_CONTEXT_RESOURCE, MAAGE_OPERATION_INITIALIZE, !resources ? 1 : 2);
    memset(&missing, 0, sizeof(missing));
    memset(resources, 0, sizeof(*resources));
    resources->load_status = maage_status_make(MAAGE_STATUS_SUCCESS, 0, MAAGE_OPERATION_LOAD_TEXTURE, 0);
    resources->recovery_status = maage_status_make(MAAGE_STATUS_SUCCESS, 0, MAAGE_OPERATION_ATLAS, 0);
    resources->content = content;
    if (maage_biome_registry_count(maage_content_biomes(content)) > (size_t) - 1 / sizeof(*resources->biomes)) return maage_status_make(MAAGE_STATUS_BUFFER_OVERFLOW, MAAGE_CONTEXT_RESOURCE, MAAGE_OPERATION_LOAD, 0);
    resources->biomes = (maage_biome_definition *) calloc(maage_biome_registry_count(maage_content_biomes(content)), sizeof(*resources->biomes));
    if (!resources->biomes) return maage_status_make(MAAGE_STATUS_ALLOCATION_FAILED, MAAGE_CONTEXT_RESOURCE, MAAGE_OPERATION_LOAD, 0);
    catalog = maage_content_presentation(resources->content);
    bindings = maage_presentation_get_bindings(catalog);
    resources->texture_count = maage_presentation_texture_count(catalog);
    resources->source_count = maage_presentation_source_count(catalog);
    if (resources->texture_count > INT_MAX - MAAGE_CLIENT_BREAK_STAGES - 31 || resources->source_count > INT_MAX || resources->texture_count > (size_t) - 1 / sizeof(*regions) || resources->texture_count > (size_t) - 1 / sizeof(*resources->uv) || resources->source_count > (size_t) - 1 / sizeof(*sources) || resources->source_count > (size_t) - 1 / sizeof(*resources->source_results)) {
        resources->recovery_status = maage_status_make(MAAGE_STATUS_BUFFER_OVERFLOW, MAAGE_CONTEXT_RESOURCE, MAAGE_OPERATION_ATLAS, 0);
        goto done;
    }
    regions = (maage_client_animation_region *) calloc(resources->texture_count, sizeof(*regions));
    sources = (maage_image *) calloc(resources->source_count, sizeof(*sources));
    resources->source_results = (maage_status *) calloc(resources->source_count, sizeof(*resources->source_results));
    resources->texture_animation = (float (*)[3]) calloc(resources->texture_count, sizeof(*resources->texture_animation));
    resources->uv = (float (*)[4]) calloc(resources->texture_count, sizeof(*resources->uv));
    if (!regions || !sources || !resources->source_results || !resources->uv || !resources->texture_animation) {
        resources->recovery_status = maage_status_make(MAAGE_STATUS_ALLOCATION_FAILED, MAAGE_CONTEXT_RESOURCE, MAAGE_OPERATION_ATLAS, 0);
        goto done;
    }
    if (!bindings || bindings->skin >= resources->source_count || bindings->grass >= resources->source_count || bindings->foliage >= resources->source_count || bindings->missing >= resources->texture_count) {
        resources->recovery_status = maage_status_make(MAAGE_STATUS_NOT_FOUND, MAAGE_CONTEXT_RESOURCE, MAAGE_OPERATION_ATLAS, 0);
        goto done;
    }
    for (i = 0; i < 10; ++i)
        if (bindings->breaking[i] >= resources->source_count) {
            resources->recovery_status = maage_status_make(MAAGE_STATUS_NOT_FOUND, MAAGE_CONTEXT_RESOURCE, MAAGE_OPERATION_ATLAS, 0);
            goto done;
        }
    /* The asset-independent tile exists before ordinary source registration. */
    result = maage_image_make_missing(&missing, edge, edge);
    if (maage_status_failed(result)) {
        resources->recovery_status = result;
        goto done;
    }
    /* External names are consumed here only; mesh faces retain numeric UVs.
     * Each result is cached once for this immutable resource generation. */
    for (i = 0; i < (int) resources->source_count; ++i) {
        if (checkpoint) {
            result = checkpoint(context, trace);
            if (maage_status_failed(result)) {
                resources->recovery_status = result;
                goto done;
            }
        }
        maage_status_trace_init(&source_trace);
        MAAGE_STATUS_TRACE(&source_trace, 0, MAAGE_CONTEXT_CLIENT, MAAGE_OPERATION_INITIALIZE);
        MAAGE_STATUS_TRACE(&source_trace, 0, MAAGE_CONTEXT_RESOURCE, MAAGE_OPERATION_INITIALIZE);
        MAAGE_STATUS_TRACE(&source_trace, 0, MAAGE_CONTEXT_RESOURCE, MAAGE_OPERATION_ATLAS);
        if ((unsigned long) i == bindings->skin && player && *player->texture) {
            result = maage_skin_path(application_data, player, path);
            if (!maage_status_failed(result)) result = maage_skin_load(path, player->model, &sources[i], &source_trace);
            resources->skin_status = result;
            if (!maage_status_failed(result)) {
                resources->skin_model = player->model;
                goto source_checked;
            }
            maage_log_trace_status(MAAGE_LOG_WARNING, "maage.client.skin", result, &source_trace, "Selected cached skin unavailable; using the original default appearance.");
            if (!resources->failed_resources) {
                resources->load_status = result;
                recovered_trace = source_trace;
            }
            ++resources->failed_resources;
            maage_image_destroy(&sources[i]);
            maage_status_trace_init(&source_trace);
        }
        /* Environment sprites have independent rectangular layouts; their
         * renderer loads these registered sources into its own bounded atlas. */
        for (j = 0; j < 5; ++j)
            if (bindings->environment[j] == (unsigned long) i) break;
        if (j < 5) continue;
        result = maage_minecraft_asset_path(application_data, maage_client_source_name(resources, (unsigned long) i), path, sizeof(path), &source_trace);
        if (!maage_status_failed(result)) result = (unsigned long) i == bindings->skin ? maage_skin_load(path, MAAGE_SKIN_CLASSIC, &sources[i], &source_trace) : maage_image_load_png(path, &sources[i], &source_trace);
    source_checked:
        if (!maage_status_failed(result)) {
            /* Normalize each valid source independently into the canonical
             * 16px atlas. One unusual texture cannot resize unrelated tiles. */
            if (maage_client_source_sheet(catalog, (unsigned long) i, &sheet_width, &sheet_height) ? ((double) sources[i].width * sheet_height != (double) sources[i].height * sheet_width) : (((unsigned long) i == bindings->grass || (unsigned long) i == bindings->foliage) ? (sources[i].width != 256 || sources[i].height != 256) : ((unsigned long) i == bindings->skin ? (sources[i].width != 64 || sources[i].height != 64) : (sources[i].height != sources[i].width && !(maage_presentation_source_animated(catalog, (unsigned long) i))))))
                result = maage_status_make(MAAGE_STATUS_MALFORMED_INPUT, MAAGE_CONTEXT_RESOURCE, MAAGE_OPERATION_LOAD_TEXTURE, 0);
        }
        if (!maage_status_failed(result) && maage_presentation_source_animated(catalog, (unsigned long) i) && (sources[i].height < sources[i].width || sources[i].height % sources[i].width != 0)) result = maage_status_make(MAAGE_STATUS_MALFORMED_INPUT, MAAGE_CONTEXT_RESOURCE, MAAGE_OPERATION_LOAD_TEXTURE, 0);
        resources->source_results[i] = result;
        if (maage_status_failed(result)) {
            maage_image_destroy(&sources[i]);
            maage_client_resource_failure(resources, i, result, &source_trace);
            if (resources->failed_resources == 1) recovered_trace = source_trace;
        } else
            MAAGE_LOG_DEBUG_EVENT("maage.client.resources", maage_log_debug("maage.client.resources", "Loaded %s (%dx%d).", maage_client_source_name(resources, (unsigned long) i), sources[i].width, sources[i].height));
    }
    if (sources[bindings->grass].pixels) maage_client_colormap(&sources[bindings->grass], 0.5, 1.0, resources->inventory_grass_color);
    else
        memcpy(resources->inventory_grass_color, default_grass, sizeof(default_grass));
    for (i = 0; (size_t) i < maage_biome_registry_count(maage_content_biomes(content)); ++i) {
        if (!maage_biome_registry_get(maage_content_biomes(content), (maage_biome_id) i)) {
            resources->recovery_status = maage_status_make(MAAGE_STATUS_INVARIANT_VIOLATION, MAAGE_CONTEXT_RESOURCE, MAAGE_OPERATION_INITIALIZE, 0);
            goto done;
        }
        tinted = *maage_biome_registry_get(maage_content_biomes(content), (maage_biome_id) i);
        biome = &tinted;
        /* Absent optional colormaps use fixed, bounded green presentation tints. */
        if (sources[bindings->grass].pixels) maage_client_colormap(&sources[bindings->grass], biome->temperature, biome->rainfall, biome->grass_color);
        else
            memcpy(biome->grass_color, default_grass, sizeof(default_grass));
        if (sources[bindings->foliage].pixels) maage_client_colormap(&sources[bindings->foliage], biome->temperature, biome->rainfall, biome->foliage_color);
        else
            memcpy(biome->foliage_color, default_foliage, sizeof(default_foliage));
        resources->biomes[i] = tinted;
    }
    resources->recovery_status = maage_status_make(MAAGE_STATUS_BUFFER_OVERFLOW, MAAGE_CONTEXT_RESOURCE, MAAGE_OPERATION_ATLAS, 0);
    rows = ((int) resources->texture_count + MAAGE_CLIENT_BREAK_STAGES + columns - 1) / columns;
    if (edge > INT_MAX - 2 || edge + 2 > INT_MAX / columns || edge + 2 > INT_MAX / rows) goto done;
    tile = edge + 2;
    resources->image.width = tile * columns;
    if (resources->image.width < 1024) resources->image.width = 1024;
    animation_y = tile * rows;
    for (i = 0; i < (int) resources->texture_count; ++i) {
        texture = maage_presentation_texture(catalog, (maage_texture_id) i);
        if ((!texture->frame_ticks && !texture->sheet_width) || texture->source == MAAGE_CONTENT_ID_INVALID || !sources[texture->source].pixels) continue;
        if (texture->sheet_width) {
            if (texture->sheet_width > INT_MAX - 2U || texture->sheet_height > INT_MAX - 2U) goto done;
            regions[region_count].texture = (size_t) i;
            regions[region_count].width = (int) texture->sheet_width + 2;
            regions[region_count++].height = (int) texture->sheet_height + 2;
            if ((int) texture->sheet_width + 2 > resources->image.width) resources->image.width = (int) texture->sheet_width + 2;
            continue;
        }
        custom_frames = sources[texture->source].height / sources[texture->source].width;
        if (texture->frame_edge > INT_MAX - 2U) goto done;
        frame_edge = texture->frame_edge ? (int) texture->frame_edge : edge;
        frame_tile = frame_edge + 2;
        if (custom_frames > INT_MAX / frame_tile) goto done;
        if (frame_tile > resources->image.width) resources->image.width = frame_tile;
        regions[region_count].texture = (size_t) i;
        regions[region_count].width = frame_tile;
        regions[region_count++].height = custom_frames * frame_tile;
    }
    /* Keep animation strips contiguous while packing tall strips beside GUI
     * textures. Registration order does not force a sparse, growing atlas. */
    qsort(regions, region_count, sizeof(*regions), maage_client_animation_order);
    shelf_x = 0;
    shelf_y = animation_y;
    shelf_height = 0;
    for (region_index = 0; region_index < region_count; ++region_index) {
        if (shelf_x > resources->image.width - regions[region_index].width) {
            if (shelf_y > INT_MAX - shelf_height) goto done;
            shelf_y += shelf_height;
            shelf_x = shelf_height = 0;
        }
        regions[region_index].x = shelf_x;
        regions[region_index].y = shelf_y;
        shelf_x += regions[region_index].width;
        if (regions[region_index].height > shelf_height) shelf_height = regions[region_index].height;
    }
    if (shelf_y > INT_MAX - shelf_height) goto done;
    custom_height = shelf_y + shelf_height - animation_y;
    if (animation_y > INT_MAX - custom_height - 66) goto done;
    skin_y = animation_y + custom_height;
    resources->image.height = skin_y + 66;
    if ((size_t) resources->image.width > (size_t) - 1 / 4) goto done;
    resources->image.stride = (size_t) resources->image.width * 4;
    if ((size_t) resources->image.height > (size_t) - 1 / resources->image.stride) goto done;
    resources->image.pixels = (unsigned char *) calloc((size_t) resources->image.height, resources->image.stride);
    if (!resources->image.pixels) {
        resources->recovery_status = maage_status_make(MAAGE_STATUS_ALLOCATION_FAILED, MAAGE_CONTEXT_RESOURCE, MAAGE_OPERATION_ATLAS, 0);
        goto done;
    }
    for (i = 0; i < (int) resources->texture_count + MAAGE_CLIENT_BREAK_STAGES; ++i) {
        tile_x = (i % columns) * tile;
        tile_y = (i / columns) * tile;
        stage = i - (int) resources->texture_count;
        source_index = stage >= 0 ? (int) bindings->breaking[stage] : maage_client_texture_source(resources, (maage_texture_id) i);
        /* Missing opaque variants degrade to a solid version of the cutout
         * source, not the magenta/black missing-texture tile. */
        texture = stage >= 0 ? NULL : maage_presentation_texture(catalog, (maage_texture_id) i);
        if (texture && (source_index < 0 || !sources[source_index].pixels) && texture->fallback != MAAGE_CONTENT_ID_INVALID) source_index = (int) texture->fallback;
        selected = source_index >= 0 && sources[source_index].pixels ? &sources[source_index] : &missing;
        /* The canonical tile is square; registered entity sheets need independent X/Y sampling. */
        for (y = 0; y < tile; ++y) {
            sy = y == 0 ? 0 : (y > edge ? edge - 1 : y - 1);
            for (x = 0; x < tile; ++x) {
                sx = x == 0 ? 0 : (x > edge ? edge - 1 : x - 1);
                pixel = resources->image.pixels + (size_t) (tile_y + y) * resources->image.stride + (size_t) (tile_x + x) * 4;
                source = selected->pixels + (size_t) (sy * selected->height / edge) * selected->stride + (size_t) (sx * selected->width / edge) * 4;
                memcpy(pixel, source, 4);
                /* An unavailable crack stage is an omitted decoration. */
                if (stage >= 0 && selected == &missing) memset(pixel, 0, 4);
                if (selected == &missing) continue;
                if (texture && texture->opaque) pixel[3] = 255;
            }
        }
        if (stage >= 0) {
            resources->breaking_uv[stage][0] = (float) (tile_x + 1) / resources->image.width;
            resources->breaking_uv[stage][1] = (float) (tile_y + 1) / resources->image.height;
            resources->breaking_uv[stage][2] = (float) (tile_x + 1 + edge) / resources->image.width;
            resources->breaking_uv[stage][3] = (float) (tile_y + 1 + edge) / resources->image.height;
            continue;
        }
        resources->uv[i][0] = (float) (tile_x + 1) / resources->image.width;
        resources->uv[i][1] = (float) (tile_y + 1) / resources->image.height;
        resources->uv[i][2] = (float) (tile_x + 1 + edge) / resources->image.width;
        resources->uv[i][3] = (float) (tile_y + 1 + edge) / resources->image.height;
    }
    selected = sources[bindings->skin].pixels ? &sources[bindings->skin] : &missing;
    for (y = 0; y < 66; ++y)
        for (x = 0; x < 66; ++x) {
            sx = x == 0 ? 0 : (x > 64 ? 63 : x - 1);
            sy = y == 0 ? 0 : (y > 64 ? 63 : y - 1);
            memcpy(resources->image.pixels + (size_t) (skin_y + y) * resources->image.stride + (size_t) x * 4, selected->pixels + (size_t) (sy * selected->height / 64) * selected->stride + (size_t) (sx * selected->width / 64) * 4, 4);
        }
    resources->skin_uv[0] = 1.0f / resources->image.width;
    resources->skin_uv[1] = (float) (skin_y + 1) / resources->image.height;
    resources->skin_uv[2] = 65.0f / resources->image.width;
    resources->skin_uv[3] = (float) (skin_y + 65) / resources->image.height;
    /* Every failed visual resolves to the same canonical tile in the shared
     * renderer-owned atlas; registration never publishes a missing handle. */
    for (i = 0; i < (int) resources->texture_count; ++i) {
        if ((maage_texture_id) i == bindings->missing) continue;
        source_index = maage_client_texture_source(resources, (maage_texture_id) i);
        if (source_index >= 0 && sources[source_index].pixels) continue;
        texture = maage_presentation_texture(catalog, (maage_texture_id) i);
        if (texture && texture->fallback != MAAGE_CONTENT_ID_INVALID && sources[texture->fallback].pixels) continue;
        memcpy(resources->uv[i], resources->uv[bindings->missing], sizeof(resources->uv[i]));
    }
    /* Every registered animation owns a padded immutable strip. */
    for (region_index = 0; region_index < region_count; ++region_index) {
        i = (int) regions[region_index].texture;
        custom_x = regions[region_index].x;
        custom_y = regions[region_index].y;
        texture = maage_presentation_texture(catalog, (maage_texture_id) i);
        if ((!texture->frame_ticks && !texture->sheet_width) || texture->source == MAAGE_CONTENT_ID_INVALID || !sources[texture->source].pixels) continue;
        selected = &sources[texture->source];
        if (texture->sheet_width) {
            for (y = 0; y < (int) texture->sheet_height + 2; ++y)
                for (x = 0; x < (int) texture->sheet_width + 2; ++x) {
                    sx = x == 0 ? 0 : (x > (int) texture->sheet_width ? (int) texture->sheet_width - 1 : x - 1);
                    sy = y == 0 ? 0 : (y > (int) texture->sheet_height ? (int) texture->sheet_height - 1 : y - 1);
                    pixel = resources->image.pixels + (size_t) (custom_y + y) * resources->image.stride + (size_t) (custom_x + x) * 4;
                    source = selected->pixels + (size_t) ((double) sy * selected->height / texture->sheet_height) * selected->stride + (size_t) ((double) sx * selected->width / texture->sheet_width) * 4;
                    memcpy(pixel, source, 4);
                    if (texture->opaque) pixel[3] = 255;
                }
            resources->uv[i][0] = (float) (custom_x + 1) / resources->image.width;
            resources->uv[i][1] = (float) (custom_y + 1) / resources->image.height;
            resources->uv[i][2] = (float) (custom_x + texture->sheet_width + 1) / resources->image.width;
            resources->uv[i][3] = (float) (custom_y + texture->sheet_height + 1) / resources->image.height;
            continue;
        }
        custom_frames = selected->height / selected->width;
        frame_edge = texture->frame_edge ? (int) texture->frame_edge : edge;
        frame_tile = frame_edge + 2;
        if (frame_tile > resources->image.width) goto done;
        for (frame = 0; frame < custom_frames; ++frame)
            for (y = 0; y < frame_tile; ++y)
                for (x = 0; x < frame_tile; ++x) {
                    sx = x == 0 ? 0 : (x > frame_edge ? frame_edge - 1 : x - 1);
                    sy = y == 0 ? 0 : (y > frame_edge ? frame_edge - 1 : y - 1);
                    pixel = resources->image.pixels + (size_t) (custom_y + frame * frame_tile + y) * resources->image.stride + (size_t) (custom_x + x) * 4;
                    source = selected->pixels + (size_t) (frame * selected->width + sy * selected->width / frame_edge) * selected->stride + (size_t) (sx * selected->width / frame_edge) * 4;
                    memcpy(pixel, source, 4);
                    if (texture->opaque) pixel[3] = 255;
                }
        resources->uv[i][0] = (float) (custom_x + 1) / resources->image.width;
        resources->uv[i][1] = (float) (custom_y + 1) / resources->image.height;
        resources->uv[i][2] = (float) (custom_x + frame_edge + 1) / resources->image.width;
        resources->uv[i][3] = (float) (custom_y + frame_edge + 1) / resources->image.height;
        if (custom_frames > 1) {
            resources->texture_animation[i][0] = (float) frame_tile / resources->image.height;
            resources->texture_animation[i][1] = (float) (texture->pingpong ? -custom_frames : custom_frames);
            resources->texture_animation[i][2] = (float) texture->frame_ticks;
        }
    }
    pixel = resources->image.pixels + (size_t) (resources->image.height - 1) * resources->image.stride + (size_t) (resources->image.width - 1) * 4;
    memset(pixel, 255, 4);
    resources->white_uv[0] = resources->white_uv[2] = ((float) resources->image.width - 0.5f) / resources->image.width;
    resources->white_uv[1] = resources->white_uv[3] = ((float) resources->image.height - 0.5f) / resources->image.height;
    resources->atlas.pixels = resources->image.pixels;
    resources->atlas.width = resources->image.width;
    resources->atlas.height = resources->image.height;
    resources->atlas.stride = resources->image.stride;
    resources->recovery_status = maage_status_make(MAAGE_STATUS_SUCCESS, 0, MAAGE_OPERATION_ATLAS, 0);
    ok = 1;
done:
    if (sources)
        for (j = 0; j < (int) resources->source_count; ++j)
            maage_image_destroy(&sources[j]);
    free(sources);
    free(regions);
    maage_image_destroy(&missing);
    if (!ok) {
        maage_image_destroy(&resources->image);
        memset(&resources->atlas, 0, sizeof(resources->atlas));
    }
    if (ok) {
        memset(&view_config, 0, sizeof(view_config));
        view_config.resolver = maage_client_content_resolve;
        view_config.appearance_resolver = maage_client_content_resolve_appearance;
        view_config.context = resources;
        view_config.biomes = resources->biomes;
        view_config.biome_count = maage_biome_registry_count(maage_content_biomes(content));
        resources->recovery_status = maage_content_view_create(content, &view_config, &resources->view, trace);
        if (maage_status_failed(resources->recovery_status)) ok = 0;
    }
    if (!ok) return MAAGE_STATUS_PROPAGATE(trace, resources->recovery_status, MAAGE_CONTEXT_RESOURCE, MAAGE_OPERATION_INITIALIZE);
    MAAGE_LOG_DEBUG_EVENT("maage.client.resources", maage_log_debug("maage.client.resources", "LOAD prepared %lu sources / %lu textures; atlas=%dx%d.", (unsigned long) resources->source_count, (unsigned long) resources->texture_count, resources->atlas.width, resources->atlas.height));
    if (trace && resources->failed_resources) *trace = recovered_trace;
    return MAAGE_STATUS_PROPAGATE(trace, maage_status_make(resources->failed_resources ? MAAGE_STATUS_PARTIAL : MAAGE_STATUS_SUCCESS, MAAGE_CONTEXT_RESOURCE, MAAGE_OPERATION_INITIALIZE, 0), MAAGE_CONTEXT_CLIENT, MAAGE_OPERATION_INITIALIZE);
}

/* Presentation is resolved once from the Client-owned atlas. All block and
 * state identity, geometry, gameplay and persistence fields are World-owned. */
static int maage_client_texture_available(const maage_client_resources *resources, maage_texture_id id) {
    const maage_texture_definition *texture = maage_presentation_texture(maage_content_presentation(resources->content), id);
    return texture && ((texture->source < resources->source_count && !maage_status_failed(resources->source_results[texture->source])) || (texture->fallback < resources->source_count && !maage_status_failed(resources->source_results[texture->fallback])));
}
static maage_status maage_client_content_resolve_appearance(void *context, const maage_block_face_definition *logical, maage_block_face_definition *face) {
    const maage_client_resources *resources = (const maage_client_resources *) context;
    const maage_texture_definition *texture;
    if (!resources || !logical || !face) return maage_status_make(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_CONTEXT_RESOURCE, MAAGE_OPERATION_RESOLVE, 0);
    *face = *logical;
    if (face->texture_id >= resources->texture_count) return maage_status_make(MAAGE_STATUS_NOT_FOUND, MAAGE_CONTEXT_RESOURCE, MAAGE_OPERATION_RESOLVE, 0);
    memcpy(face->uv, resources->uv[face->texture_id], sizeof(face->uv));
    memcpy(face->animation, resources->texture_animation[face->texture_id], sizeof(face->animation));
    texture = maage_presentation_texture(maage_content_presentation(resources->content), face->texture_id);
    if (!texture || texture->source == MAAGE_CONTENT_ID_INVALID || maage_status_failed(resources->source_results[texture->source])) {
        face->tint_mode = face->overlay_tint_mode = 0;
        face->tint[0] = face->tint[1] = face->tint[2] = face->tint[3] = 1;
    }
    memset(face->overlay_uv, 0, sizeof(face->overlay_uv));
    memset(face->snow_uv, 0, sizeof(face->snow_uv));
    if (face->overlay_texture && face->overlay_texture < resources->texture_count && maage_client_texture_available(resources, face->overlay_texture)) memcpy(face->overlay_uv, resources->uv[face->overlay_texture], sizeof(face->overlay_uv));
    if (face->snow_texture && face->snow_texture < resources->texture_count && maage_client_texture_available(resources, face->snow_texture)) memcpy(face->snow_uv, resources->uv[face->snow_texture], sizeof(face->snow_uv));
    if (face->has_flow_texture) {
        if (face->flow_texture >= resources->texture_count) return maage_status_make(MAAGE_STATUS_NOT_FOUND, MAAGE_CONTEXT_RESOURCE, MAAGE_OPERATION_RESOLVE, 0);
        memcpy(face->flow_uv, resources->uv[face->flow_texture], sizeof(face->flow_uv));
        memcpy(face->flow_animation, resources->texture_animation[face->flow_texture], sizeof(face->flow_animation));
    }
    return maage_status_make(MAAGE_STATUS_SUCCESS, 0, MAAGE_OPERATION_RESOLVE, 0);
}
static maage_status maage_client_content_resolve(void *context, const maage_block_state_definition *state, maage_block_face_definition faces[MAAGE_FACE_COUNT], float leaf_fast_uv[4]) {
    const maage_client_resources *resources = (const maage_client_resources *) context;
    maage_status result;
    unsigned int side;
    if (!resources || !state) return maage_status_make(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_CONTEXT_RESOURCE, MAAGE_OPERATION_RESOLVE, 0);
    for (side = 0; side < MAAGE_FACE_COUNT; ++side) {
        result = maage_client_content_resolve_appearance(context, &state->faces[side], &faces[side]);
        if (maage_status_failed(result)) return result;
    }
    if (state->fast_texture && state->fast_texture < resources->texture_count) memcpy(leaf_fast_uv, resources->uv[state->fast_texture], sizeof(state->leaf_fast_uv));
    return maage_status_make(MAAGE_STATUS_SUCCESS, 0, MAAGE_OPERATION_RESOLVE, 0);
}

void maage_client_item_face_tint(const float grass_color[3], const maage_block_face_definition *face, unsigned int mode, float color[4]) {
    static const float foliage_color[3] = {72.0f / 255, 181.0f / 255, 24.0f / 255};
    int channel;
    for (channel = 0; channel < 3; ++channel) {
        color[channel] = face->tint[channel];
        if (mode == MAAGE_BLOCK_TINT_GRASS || mode == MAAGE_BLOCK_TINT_GRASS_LOCAL) color[channel] *= grass_color[channel];
        else if (mode == MAAGE_BLOCK_TINT_FOLIAGE) color[channel] *= foliage_color[channel];
    }
    color[3] = face->tint[3];
}

const maage_block_state_definition *maage_client_item_icon_state(const maage_block_registry *blocks, const maage_item_definition *item, unsigned int metadata) {
    if (!item) return NULL;
    return maage_block_registry_state(blocks, item->icon_state ? item->icon_state(item, metadata) : item->icon_state_id);
}

maage_status maage_client_resources_destroy(maage_client_resources *resources) {
    if (!resources) return maage_status_make(MAAGE_STATUS_NOOP, 0, MAAGE_OPERATION_DESTROY, 0);
    maage_image_destroy(&resources->image);
    maage_content_view_destroy(resources->view);
    free(resources->biomes);
    free(resources->source_results);
    free(resources->uv);
    free(resources->texture_animation);
    memset(resources, 0, sizeof(*resources));
    return maage_status_make(MAAGE_STATUS_SUCCESS, 0, MAAGE_OPERATION_DESTROY, 0);
}

maage_texture_id maage_client_item_texture(const maage_item_definition *item, unsigned int metadata) {
    return item ? (item->icon_texture ? item->icon_texture(item, metadata) : item->texture_id) : MAAGE_CONTENT_ID_INVALID;
}

const char *maage_client_item_name(const maage_item_registry *items, const maage_item_stack *stack) {
    const maage_item_definition *item = stack ? maage_item_registry_get(items, stack->item_id) : NULL;
    return item ? (item->variant_name ? item->variant_name(item, stack->metadata) : item->display_name) : "";
}
