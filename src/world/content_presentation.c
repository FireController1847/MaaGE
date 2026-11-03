#include "registry_policy.h"
#include <stdlib.h>
#include <string.h>

struct maage_content_view {
    maage_block_registry *blocks;
    maage_block_geometry_registry *geometry;
    maage_biome_registry biomes;
};
maage_status maage_content_view_create(const maage_content *content, const maage_content_view_config *config, maage_content_view **output, maage_status_trace *trace) {
    maage_content_view *view;
    const maage_biome_registry *source = maage_content_biomes(content);
    size_t i, channel;
    maage_status result;
    if (output) *output = NULL;
    if (!output || !content || !config || !source || !source->finalized || config->biome_count != source->count || (source->count && !config->biomes) || source->count > (size_t) - 1 / sizeof(*source->view_definitions)) return MAAGE_STATUS_TRACE(trace, maage_status_make(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_CONTEXT_RESOURCE, MAAGE_OPERATION_RESOLVE, 0), MAAGE_CONTEXT_RESOURCE, MAAGE_OPERATION_RESOLVE);
    view = (maage_content_view *) calloc(1, sizeof(*view));
    if (!view) return maage_status_make(MAAGE_STATUS_ALLOCATION_FAILED, MAAGE_CONTEXT_RESOURCE, MAAGE_OPERATION_RESOLVE, 0);
    view->biomes = *source;
    view->biomes.view_source = source;
    view->biomes.view_definitions = source->count ? (maage_biome_definition *) malloc(source->count * sizeof(*source->view_definitions)) : NULL;
    if (source->count && !view->biomes.view_definitions) {
        free(view);
        return maage_status_make(MAAGE_STATUS_ALLOCATION_FAILED, MAAGE_CONTEXT_RESOURCE, MAAGE_OPERATION_RESOLVE, 0);
    }
    for (i = 0; i < source->count; ++i) {
        for (channel = 0; channel < 3; ++channel)
            if (!(config->biomes[i].grass_color[channel] >= 0 && config->biomes[i].grass_color[channel] <= 1 && config->biomes[i].foliage_color[channel] >= 0 && config->biomes[i].foliage_color[channel] <= 1)) {
                maage_content_view_destroy(view);
                return maage_status_make(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_CONTEXT_RESOURCE, MAAGE_OPERATION_RESOLVE, 0);
            }
        view->biomes.view_definitions[i] = *maage_biome_registry_get(source, (maage_biome_id) i);
        memcpy(view->biomes.view_definitions[i].grass_color, config->biomes[i].grass_color, sizeof(config->biomes[i].grass_color));
        memcpy(view->biomes.view_definitions[i].foliage_color, config->biomes[i].foliage_color, sizeof(config->biomes[i].foliage_color));
    }
    result = content_geometry_view(maage_content_geometry(content), config, &view->geometry);
    if (!maage_status_failed(result)) result = content_blocks_view(maage_content_blocks(content), view->geometry, config, &view->blocks);
    if (maage_status_failed(result)) {
        maage_content_view_destroy(view);
        return MAAGE_STATUS_PROPAGATE(trace, result, MAAGE_CONTEXT_RESOURCE, MAAGE_OPERATION_RESOLVE);
    }
    *output = view;
    return maage_status_make(MAAGE_STATUS_SUCCESS, 0, MAAGE_OPERATION_RESOLVE, 0);
}
const maage_block_registry *maage_content_view_blocks(const maage_content_view *view) {
    return view ? view->blocks : NULL;
}
const maage_biome_registry *maage_content_view_biomes(const maage_content_view *view) {
    return view ? &view->biomes : NULL;
}
maage_status maage_content_view_destroy(maage_content_view *view) {
    if (!view) return maage_status_make(MAAGE_STATUS_NOOP, 0, MAAGE_OPERATION_DESTROY, 0);
    maage_block_registry_destroy(view->blocks);
    maage_block_geometry_destroy(view->geometry);
    maage_biome_registry_destroy(&view->biomes);
    free(view);
    return maage_status_make(MAAGE_STATUS_SUCCESS, 0, MAAGE_OPERATION_DESTROY, 0);
}
