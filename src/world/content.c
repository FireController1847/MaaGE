#include "maage/content.h"
#include <stdlib.h>
#include <string.h>
typedef struct content_owner {
    void *data;
    void (*destroy)(void *);
    struct content_owner *next;
} content_owner;
struct maage_content {
    maage_content_context context;
    maage_biome_registry biomes;
};
maage_status maage_content_own(maage_content_context *context, void *data, void (*destroy)(void *)) {
    content_owner *node;
    if (!context || !data || !destroy) return maage_status_make(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_CONTEXT_REGISTRY, MAAGE_OPERATION_REGISTER, 0);
    node = (content_owner *) malloc(sizeof(*node));
    if (!node) return maage_status_make(MAAGE_STATUS_ALLOCATION_FAILED, MAAGE_CONTEXT_REGISTRY, MAAGE_OPERATION_REGISTER, 0);
    node->data = data;
    node->destroy = destroy;
    node->next = (content_owner *) context->owner;
    context->owner = node;
    return maage_status_make(MAAGE_STATUS_SUCCESS, 0, MAAGE_OPERATION_REGISTER, 0);
}
maage_status maage_content_create(const maage_content_config *config, maage_content **output, maage_status_trace *trace) {
    maage_content *content;
    maage_content_context *context;
    maage_status result;
    size_t i;
    maage_block_state_definition state, logical;
    unsigned int side;
    const maage_item_definition *item;
    const maage_block_definition *block;
    const maage_biome_definition *biome;
    if (output) *output = NULL;
    if (!output || !config || !config->register_content) return maage_status_make(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_CONTEXT_REGISTRY, MAAGE_OPERATION_CREATE, 0);
    content = (maage_content *) calloc(1, sizeof(*content));
    if (!content) return maage_status_make(MAAGE_STATUS_ALLOCATION_FAILED, MAAGE_CONTEXT_REGISTRY, MAAGE_OPERATION_CREATE, 0);
    context = &content->context;
    context->config = config;
    context->biomes = &content->biomes;
    result = maage_presentation_create(&context->presentation);
    if (!maage_status_failed(result)) result = maage_biome_registry_init(context->biomes);
    if (!maage_status_failed(result)) result = maage_block_registry_create(0, 0, &context->blocks);
    if (!maage_status_failed(result)) result = maage_item_registry_create(0, &context->items);
    if (!maage_status_failed(result)) result = maage_recipe_registry_create(context->items, &context->recipes);
    if (!maage_status_failed(result)) result = maage_smelting_registry_create(context->items, &context->smelting);
    if (!maage_status_failed(result)) result = maage_fuel_registry_create(context->items, &context->fuels);
    if (!maage_status_failed(result)) result = config->register_content(context);
    if (!maage_status_failed(result) && config->register_dependencies) result = config->register_dependencies(context);
    if (maage_status_failed(result)) goto failed;
    for (i = 0; i < maage_item_registry_count(context->items); ++i) {
        item = maage_item_registry_at(context->items, i);
        if (item->has_block && (!maage_block_registry_block(context->blocks, item->block_id) || !maage_block_registry_state(context->blocks, item->placement_state_id) || !maage_block_registry_state(context->blocks, item->icon_state_id))) {
            result = maage_status_make(MAAGE_STATUS_NOT_FOUND, MAAGE_CONTEXT_REGISTRY, MAAGE_OPERATION_FINALIZE, 0);
            goto failed;
        }
    }
    for (i = 0; i < maage_block_registry_block_count(context->blocks); ++i) {
        block = maage_block_registry_block(context->blocks, (maage_block_id) i);
        if (!block || (block->drop_count && !block->drops && !maage_item_registry_get(context->items, block->drop_item_id))) {
            result = maage_status_make(MAAGE_STATUS_NOT_FOUND, MAAGE_CONTEXT_REGISTRY, MAAGE_OPERATION_FINALIZE, 0);
            goto failed;
        }
    }
    for (i = 0; i < maage_biome_registry_count(context->biomes); ++i) {
        biome = maage_biome_registry_get(context->biomes, (maage_biome_id) i);
        if (!biome || !maage_block_registry_state(context->blocks, biome->surface) || !maage_block_registry_state(context->blocks, biome->filler)) {
            result = maage_status_make(MAAGE_STATUS_NOT_FOUND, MAAGE_CONTEXT_REGISTRY, MAAGE_OPERATION_FINALIZE, 0);
            goto failed;
        }
    }
    if (config->resolver)
        for (i = 0; i < maage_block_registry_state_count(context->blocks); ++i) {
            state = *maage_block_registry_state(context->blocks, (maage_block_state_id) i);
            logical = state;
            result = config->resolver(config->resolver_context, &logical, state.faces, state.leaf_fast_uv);
            if (!maage_status_failed(result))
                for (side = 0; side < MAAGE_FACE_COUNT; ++side) {
                    if (state.faces[side].texture_id != logical.faces[side].texture_id || state.faces[side].overlay_texture != logical.faces[side].overlay_texture || state.faces[side].snow_texture != logical.faces[side].snow_texture) {
                        result = maage_status_make(MAAGE_STATUS_INVALID_STATE, MAAGE_CONTEXT_REGISTRY, MAAGE_OPERATION_RESOLVE, 0);
                        break;
                    }
                }
            if (!maage_status_failed(result)) result = maage_block_registry_update_state(context->blocks, &state);
            if (maage_status_failed(result)) goto failed;
        }
    result = maage_presentation_finalize(context->presentation);
    if (!maage_status_failed(result)) result = maage_item_registry_finalize(context->items);
    if (!maage_status_failed(result)) result = maage_block_registry_finalize(context->blocks);
    if (!maage_status_failed(result)) result = maage_biome_registry_finalize(context->biomes);
    if (!maage_status_failed(result)) result = maage_recipe_registry_finalize(context->recipes);
    if (!maage_status_failed(result)) result = maage_smelting_finalize(context->smelting);
    if (!maage_status_failed(result)) result = maage_fuel_finalize(context->fuels);
    if (maage_status_failed(result)) goto failed;
    context->config = NULL;
    *output = content;
    return maage_status_make(MAAGE_STATUS_SUCCESS, 0, MAAGE_OPERATION_CREATE, 0);
failed:
    maage_content_destroy(content);
    return MAAGE_STATUS_PROPAGATE(trace, result, MAAGE_CONTEXT_REGISTRY, MAAGE_OPERATION_CREATE);
}
const maage_block_registry *maage_content_blocks(const maage_content *content) {
    return content ? content->context.blocks : NULL;
}
const maage_biome_registry *maage_content_biomes(const maage_content *content) {
    return content ? content->context.biomes : NULL;
}
const maage_item_registry *maage_content_items(const maage_content *content) {
    return content ? content->context.items : NULL;
}
const maage_recipe_registry *maage_content_recipes(const maage_content *content) {
    return content ? content->context.recipes : NULL;
}
const maage_smelting_registry *maage_content_smelting(const maage_content *content) {
    return content ? content->context.smelting : NULL;
}
const maage_fuel_registry *maage_content_fuels(const maage_content *content) {
    return content ? content->context.fuels : NULL;
}
maage_status maage_content_destroy(maage_content *content) {
    content_owner *node, *next;
    if (!content) return maage_status_make(MAAGE_STATUS_NOOP, 0, MAAGE_OPERATION_DESTROY, 0);
    maage_presentation_destroy(content->context.presentation);
    maage_smelting_destroy(content->context.smelting);
    maage_fuel_destroy(content->context.fuels);
    maage_recipe_registry_destroy(content->context.recipes);
    maage_item_registry_destroy(content->context.items);
    maage_block_registry_destroy(content->context.blocks);
    maage_biome_registry_destroy(&content->biomes);
    for (node = (content_owner *) content->context.owner; node; node = next) {
        next = node->next;
        node->destroy(node->data);
        free(node);
    }
    free(content);
    return maage_status_make(MAAGE_STATUS_SUCCESS, 0, MAAGE_OPERATION_DESTROY, 0);
}

const maage_presentation_registry *maage_content_presentation(const maage_content *content) {
    return content ? content->context.presentation : NULL;
}
