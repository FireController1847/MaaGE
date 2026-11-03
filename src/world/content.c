#include "maage/content.h"
#include <stdlib.h>
#include <string.h>
typedef struct content_owner {
    void *data;
    void (*destroy)(void *);
    struct content_owner *next;
} content_owner;
typedef struct content_module_slot {
    const maage_content_module *definition;
    void *data;
} content_module_slot;
struct maage_content {
    maage_content_context context;
    maage_biome_registry biomes;
    content_module_slot *modules;
    size_t module_count, *order;
};
static maage_status content_failure(const maage_content_config *config, const maage_content_module *module, const char *dependency, maage_content_phase phase, maage_status result) {
    if (config && config->failure) {
        config->failure->module_name = module ? module->name : NULL;
        config->failure->dependency_name = dependency;
        config->failure->phase = phase;
        config->failure->result = result;
    }
    return result;
}
static int content_module_name_valid(const char *name) {
    int colon = 0;
    const char *begin = name;
    char value;
    if (!name || !*name) return 0;
    while ((value = *name++) != '\0') {
        if (value == ':') {
            if (colon || name == begin + 1 || !*name) return 0;
            colon = 1;
        } else if (!((value >= 'a' && value <= 'z') || (value >= '0' && value <= '9') || value == '_' || value == '-' || value == '.' || (colon && value == '/'))) return 0;
    }
    return colon;
}
static size_t content_module_find(const maage_content *content, const char *name) {
    size_t i;
    for (i = 0; i < content->module_count; ++i)
        if (!strcmp(content->modules[i].definition->name, name)) return i;
    return content->module_count;
}
maage_status maage_content_module_data(const maage_content_context *context, const char *name, void **data) {
    const maage_content *content;
    size_t index;
    if (data) *data = NULL;
    if (!context || !context->lifecycle || !name || !data) return maage_status_make(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_CONTEXT_REGISTRY, MAAGE_OPERATION_RESOLVE, 0);
    content = (const maage_content *) context->lifecycle;
    index = content_module_find(content, name);
    if (index == content->module_count) return maage_status_make(MAAGE_STATUS_NOT_FOUND, MAAGE_CONTEXT_REGISTRY, MAAGE_OPERATION_RESOLVE, 0);
    *data = context->module == content->modules[index].definition ? context->module_data : content->modules[index].data;
    return maage_status_make(MAAGE_STATUS_SUCCESS, 0, MAAGE_OPERATION_RESOLVE, 0);
}
static maage_status content_modules_order(maage_content *content, const maage_content_config *config) {
    const maage_content_module *module;
    unsigned char *selected;
    size_t i, j, k, position, dependency;
    int ready;
    maage_status result = maage_status_make(MAAGE_STATUS_SUCCESS, 0, MAAGE_OPERATION_RESOLVE, 0);
    if (config->module_count > (size_t) - 1 / sizeof(*content->modules) || config->module_count > (size_t) - 1 / sizeof(*content->order)) return content_failure(config, NULL, NULL, MAAGE_CONTENT_PHASE_MODULES, maage_status_make(MAAGE_STATUS_BUFFER_OVERFLOW, MAAGE_CONTEXT_REGISTRY, MAAGE_OPERATION_REGISTER, 0));
    content->modules = (content_module_slot *) calloc(config->module_count, sizeof(*content->modules));
    content->order = (size_t *) malloc(config->module_count * sizeof(*content->order));
    selected = (unsigned char *) calloc(config->module_count, 1);
    if (!content->modules || !content->order || !selected) {
        free(selected);
        return content_failure(config, NULL, NULL, MAAGE_CONTENT_PHASE_MODULES, maage_status_make(MAAGE_STATUS_ALLOCATION_FAILED, MAAGE_CONTEXT_REGISTRY, MAAGE_OPERATION_REGISTER, 0));
    }
    for (i = 0; i < config->module_count; ++i) {
        module = config->modules[i];
        if (!module || !content_module_name_valid(module->name) || (module->dependency_count && !module->dependencies)) {
            result = content_failure(config, module, NULL, MAAGE_CONTENT_PHASE_MODULES, maage_status_make(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_CONTEXT_REGISTRY, MAAGE_OPERATION_REGISTER, 0));
            goto done;
        }
        for (j = 0; j < i; ++j)
            if (!strcmp(module->name, content->modules[j].definition->name)) {
                result = content_failure(config, module, NULL, MAAGE_CONTENT_PHASE_MODULES, maage_status_make(MAAGE_STATUS_INVALID_STATE, MAAGE_CONTEXT_REGISTRY, MAAGE_OPERATION_REGISTER, 0));
                goto done;
            }
        content->modules[i].definition = module;
    }
    content->module_count = config->module_count;
    for (i = 0; i < content->module_count; ++i) {
        module = content->modules[i].definition;
        for (j = 0; j < module->dependency_count; ++j) {
            if (!content_module_name_valid(module->dependencies[j])) {
                result = content_failure(config, module, module->dependencies[j], MAAGE_CONTENT_PHASE_MODULES, maage_status_make(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_CONTEXT_REGISTRY, MAAGE_OPERATION_RESOLVE, 0));
                goto done;
            }
            if (content_module_find(content, module->dependencies[j]) == content->module_count) {
                result = content_failure(config, module, module->dependencies[j], MAAGE_CONTENT_PHASE_MODULES, maage_status_make(MAAGE_STATUS_NOT_FOUND, MAAGE_CONTEXT_REGISTRY, MAAGE_OPERATION_RESOLVE, 0));
                goto done;
            }
            for (k = 0; k < j; ++k)
                if (!strcmp(module->dependencies[j], module->dependencies[k])) {
                    result = content_failure(config, module, module->dependencies[j], MAAGE_CONTENT_PHASE_MODULES, maage_status_make(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_CONTEXT_REGISTRY, MAAGE_OPERATION_RESOLVE, 0));
                    goto done;
                }
        }
    }
    for (position = 0; position < content->module_count; ++position) {
        for (i = 0; i < content->module_count; ++i) {
            if (selected[i]) continue;
            module = content->modules[i].definition;
            ready = 1;
            for (j = 0; j < module->dependency_count; ++j) {
                dependency = content_module_find(content, module->dependencies[j]);
                if (!selected[dependency]) {
                    ready = 0;
                    break;
                }
            }
            if (ready) break;
        }
        if (i == content->module_count) {
            for (i = 0; selected[i]; ++i) {
            }
            module = content->modules[i].definition;
            for (j = 0; j < module->dependency_count; ++j)
                if (!selected[content_module_find(content, module->dependencies[j])]) break;
            result = content_failure(config, module, j < module->dependency_count ? module->dependencies[j] : NULL, MAAGE_CONTENT_PHASE_MODULES, maage_status_make(MAAGE_STATUS_INVALID_STATE, MAAGE_CONTEXT_REGISTRY, MAAGE_OPERATION_RESOLVE, 0));
            goto done;
        }
        content->order[position] = i;
        selected[i] = 1;
    }
done:
    free(selected);
    return result;
}
static maage_status content_modules_run(maage_content *content, const maage_content_config *config, maage_content_phase phase, maage_status_trace *trace) {
    maage_content_registration callback;
    content_module_slot *slot;
    size_t i;
    maage_status result, outcome = maage_status_make(MAAGE_STATUS_SUCCESS, 0, MAAGE_OPERATION_REGISTER, 0);
    unsigned char operation = phase == MAAGE_CONTENT_PHASE_DEFINE ? MAAGE_OPERATION_REGISTER : (phase == MAAGE_CONTENT_PHASE_RESOLVE ? MAAGE_OPERATION_RESOLVE : MAAGE_OPERATION_INITIALIZE);
    for (i = 0; i < content->module_count; ++i) {
        slot = &content->modules[content->order[i]];
        callback = phase == MAAGE_CONTENT_PHASE_DEFINE ? slot->definition->define : (phase == MAAGE_CONTENT_PHASE_RESOLVE ? slot->definition->resolve : slot->definition->initialize);
        if (!callback) continue;
        content->context.module = slot->definition;
        content->context.module_data = slot->data;
        result = callback(&content->context);
        slot->data = content->context.module_data;
        content->context.module = NULL;
        content->context.module_data = NULL;
        if (maage_status_failed(result)) return content_failure(config, slot->definition, NULL, phase, MAAGE_STATUS_PROPAGATE(trace, result, MAAGE_CONTEXT_REGISTRY, operation));
        if (maage_status_is_warning(result) && !maage_status_is_warning(outcome)) outcome = result;
    }
    return outcome;
}

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
static int content_face_registered(const maage_content_context *context, const maage_block_face_definition *face) {
    return face && maage_presentation_texture(context->presentation, face->texture_id) && (!face->overlay_texture || maage_presentation_texture(context->presentation, face->overlay_texture)) && (!face->snow_texture || maage_presentation_texture(context->presentation, face->snow_texture)) && (!face->has_flow_texture || maage_presentation_texture(context->presentation, face->flow_texture));
}
static int content_face_identity_equal(const maage_block_face_definition *left, const maage_block_face_definition *right) {
    return left->texture_id == right->texture_id && left->overlay_texture == right->overlay_texture && left->snow_texture == right->snow_texture && left->has_flow_texture == right->has_flow_texture && left->flow_texture == right->flow_texture;
}
static maage_status content_validate_presentation(const maage_content_context *context) {
    const maage_block_state_definition *state;
    const maage_block_face_definition *face;
    const maage_fluid_definition *fluid;
    const maage_container_definition *container;
    const maage_block_geometry_definition *geometry;
    size_t i, j;
    for (i = 0; i < maage_block_registry_state_count(context->blocks); ++i) {
        state = maage_block_registry_state(context->blocks, (maage_block_state_id) i);
        if (!state || (state->fast_texture && !maage_presentation_texture(context->presentation, state->fast_texture))) return maage_status_make(MAAGE_STATUS_NOT_FOUND, MAAGE_CONTEXT_REGISTRY, MAAGE_OPERATION_FINALIZE, 0);
        for (j = 0; j < MAAGE_FACE_COUNT; ++j) {
            face = &state->faces[j];
            if (!content_face_registered(context, face)) return maage_status_make(MAAGE_STATUS_NOT_FOUND, MAAGE_CONTEXT_REGISTRY, MAAGE_OPERATION_FINALIZE, 0);
        }
    }
    for (i = 0; i < maage_block_geometry_count(context->geometry); ++i) {
        geometry = maage_block_geometry_get(context->geometry, (maage_block_geometry_id) i + 1);
        if (!geometry) return maage_status_make(MAAGE_STATUS_NOT_FOUND, MAAGE_CONTEXT_REGISTRY, MAAGE_OPERATION_FINALIZE, 0);
        for (j = 0; j < geometry->appearance_count; ++j)
            if (!content_face_registered(context, &geometry->appearances[j])) return maage_status_make(MAAGE_STATUS_NOT_FOUND, MAAGE_CONTEXT_REGISTRY, MAAGE_OPERATION_FINALIZE, 0);
    }
    for (i = 0; i < maage_fluid_count(context->fluids); ++i) {
        fluid = maage_fluid_get(context->fluids, (maage_fluid_id) i + 1);
        if (!fluid || (fluid->has_overlay_texture && !maage_presentation_texture(context->presentation, fluid->overlay_texture)) || (fluid->material && !maage_sound_material_get(context->sounds, fluid->material))) return maage_status_make(MAAGE_STATUS_NOT_FOUND, MAAGE_CONTEXT_REGISTRY, MAAGE_OPERATION_FINALIZE, 0);
    }
    for (i = 0; i < maage_container_count(context->containers); ++i) {
        container = maage_container_get(context->containers, (maage_container_id) i + 1);
        if (!container || !maage_presentation_texture(context->presentation, container->presentation.background) || (container->creative_catalog && (!maage_presentation_texture(context->presentation, container->presentation.creative_tabs) || !maage_creative_tab_count(context->creative)))) return maage_status_make(MAAGE_STATUS_NOT_FOUND, MAAGE_CONTEXT_REGISTRY, MAAGE_OPERATION_FINALIZE, 0);
        for (j = 0; j < container->progress_count; ++j)
            if (!maage_presentation_texture(context->presentation, container->progress[j].texture)) return maage_status_make(MAAGE_STATUS_NOT_FOUND, MAAGE_CONTEXT_REGISTRY, MAAGE_OPERATION_FINALIZE, 0);
    }
    return maage_status_make(MAAGE_STATUS_SUCCESS, 0, MAAGE_OPERATION_FINALIZE, 0);
}

maage_status maage_content_create(const maage_content_config *config, maage_content **output, maage_status_trace *trace) {
    maage_content *content;
    maage_content_context *context;
    maage_status result, outcome = maage_status_make(MAAGE_STATUS_SUCCESS, 0, MAAGE_OPERATION_CREATE, 0);
    size_t i, appearance_index;
    const maage_block_geometry_definition *geometry;
    maage_block_face_definition appearance, logical_appearance;
    maage_block_state_definition state, logical;
    maage_biome_definition tinted;
    const maage_biome_definition *presentation_biome;
    unsigned int side;
    const maage_item_definition *item;
    const maage_block_definition *block;
    const maage_biome_definition *biome;
    if (output) *output = NULL;
    if (config && config->failure) memset(config->failure, 0, sizeof(*config->failure));
    if (!output || !config || !config->modules || !config->module_count) return content_failure(config, NULL, NULL, MAAGE_CONTENT_PHASE_MODULES, maage_status_make(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_CONTEXT_REGISTRY, MAAGE_OPERATION_CREATE, 0));
    content = (maage_content *) calloc(1, sizeof(*content));
    if (!content) return content_failure(config, NULL, NULL, MAAGE_CONTENT_PHASE_MODULES, maage_status_make(MAAGE_STATUS_ALLOCATION_FAILED, MAAGE_CONTEXT_REGISTRY, MAAGE_OPERATION_CREATE, 0));
    context = &content->context;
    context->config = config;
    context->biomes = &content->biomes;
    context->lifecycle = content;
    result = content_modules_order(content, config);
    if (maage_status_failed(result)) goto failed;
    result = maage_presentation_create(&context->presentation);
    if (!maage_status_failed(result)) result = maage_biome_registry_init(context->biomes);
    if (!maage_status_failed(result)) result = maage_block_registry_create(0, 0, &context->blocks);
    if (!maage_status_failed(result)) result = maage_item_registry_create(0, &context->items);
    if (!maage_status_failed(result)) result = maage_creative_create(context->items, context->presentation, &context->creative);
    if (!maage_status_failed(result)) result = maage_recipe_registry_create(context->items, &context->recipes);
    if (!maage_status_failed(result)) result = maage_smelting_registry_create(context->items, &context->smelting);
    if (!maage_status_failed(result)) result = maage_fuel_registry_create(context->items, &context->fuels);
    if (!maage_status_failed(result)) result = maage_fluid_registry_create(&context->fluids);
    if (!maage_status_failed(result)) result = maage_block_geometry_create(&context->geometry);
    if (!maage_status_failed(result)) result = maage_container_registry_create(&context->containers);
    if (!maage_status_failed(result)) result = maage_sound_registry_create(&context->sounds);
    if (!maage_status_failed(result)) result = maage_block_registry_bind_fluids(context->blocks, context->fluids);
    if (!maage_status_failed(result)) result = maage_block_registry_bind_geometry(context->blocks, context->geometry);
    if (!maage_status_failed(result)) result = maage_block_registry_bind_containers(context->blocks, context->containers);
    if (!maage_status_failed(result)) result = maage_block_registry_bind_sounds(context->blocks, context->sounds);
    if (maage_status_failed(result)) goto failed;
    result = content_modules_run(content, config, MAAGE_CONTENT_PHASE_DEFINE, trace);
    if (maage_status_failed(result)) goto failed;
    if (maage_status_is_warning(result)) outcome = result;
    result = content_modules_run(content, config, MAAGE_CONTENT_PHASE_RESOLVE, trace);
    if (maage_status_failed(result)) goto failed;
    if (maage_status_is_warning(result) && !maage_status_is_warning(outcome)) outcome = result;
    if (config->failure) config->failure->phase = MAAGE_CONTENT_PHASE_FINALIZE;
    result = content_validate_presentation(context);
    if (maage_status_failed(result)) goto failed;
    if (config->presentation_biomes) {
        if (maage_biome_registry_count(config->presentation_biomes) != maage_biome_registry_count(context->biomes)) {
            result = maage_status_make(MAAGE_STATUS_INVALID_STATE, MAAGE_CONTEXT_REGISTRY, MAAGE_OPERATION_RESOLVE, 0);
            goto failed;
        }
        for (i = 0; i < maage_biome_registry_count(context->biomes); ++i) {
            tinted = *maage_biome_registry_get(context->biomes, (maage_biome_id) i);
            presentation_biome = maage_biome_registry_get(config->presentation_biomes, (maage_biome_id) i);
            if (!presentation_biome || strcmp(maage_biome_registry_name(context->biomes, (maage_biome_id) i), maage_biome_registry_name(config->presentation_biomes, (maage_biome_id) i))) {
                result = maage_status_make(MAAGE_STATUS_INVALID_STATE, MAAGE_CONTEXT_REGISTRY, MAAGE_OPERATION_RESOLVE, 0);
                goto failed;
            }
            memcpy(tinted.grass_color, presentation_biome->grass_color, sizeof(tinted.grass_color));
            memcpy(tinted.foliage_color, presentation_biome->foliage_color, sizeof(tinted.foliage_color));
            result = maage_biome_registry_update(context->biomes, &tinted);
            if (maage_status_failed(result)) goto failed;
        }
    }
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
                    if (!content_face_identity_equal(&state.faces[side], &logical.faces[side])) {
                        result = maage_status_make(MAAGE_STATUS_INVALID_STATE, MAAGE_CONTEXT_REGISTRY, MAAGE_OPERATION_RESOLVE, 0);
                        break;
                    }
                }
            if (!maage_status_failed(result)) result = maage_block_registry_update_state(context->blocks, &state);
            if (maage_status_failed(result)) goto failed;
        }
    for (i = 0; i < maage_block_geometry_count(context->geometry); ++i) {
        geometry = maage_block_geometry_get(context->geometry, (maage_block_geometry_id) i + 1);
        if (geometry->appearance_count && config->resolver && !config->appearance_resolver) {
            result = maage_status_make(MAAGE_STATUS_INVALID_STATE, MAAGE_CONTEXT_REGISTRY, MAAGE_OPERATION_RESOLVE, 0);
            goto failed;
        }
        if (!config->appearance_resolver) continue;
        for (appearance_index = 0; appearance_index < geometry->appearance_count; ++appearance_index) {
            logical_appearance = geometry->appearances[appearance_index];
            appearance = logical_appearance;
            result = config->appearance_resolver(config->resolver_context, &logical_appearance, &appearance);
            if (!maage_status_failed(result) && !content_face_identity_equal(&appearance, &logical_appearance)) result = maage_status_make(MAAGE_STATUS_INVALID_STATE, MAAGE_CONTEXT_REGISTRY, MAAGE_OPERATION_RESOLVE, 0);
            if (!maage_status_failed(result)) result = maage_block_geometry_update_appearance(context->geometry, (maage_block_geometry_id) i + 1, appearance_index, &appearance);
            if (maage_status_failed(result)) goto failed;
        }
    }
    result = maage_presentation_finalize(context->presentation);
    if (!maage_status_failed(result)) result = maage_item_registry_finalize(context->items);
    if (!maage_status_failed(result)) result = maage_creative_finalize(context->creative);
    if (!maage_status_failed(result)) result = maage_sound_finalize(context->sounds);
    if (!maage_status_failed(result)) result = maage_fluid_finalize(context->fluids, context->blocks);
    if (!maage_status_failed(result)) result = maage_block_geometry_finalize(context->geometry);
    if (!maage_status_failed(result)) result = maage_container_finalize(context->containers);
    if (!maage_status_failed(result)) result = maage_block_registry_finalize(context->blocks);
    if (!maage_status_failed(result)) result = maage_biome_registry_finalize(context->biomes);
    if (!maage_status_failed(result)) result = maage_recipe_registry_finalize(context->recipes);
    if (!maage_status_failed(result)) result = maage_smelting_finalize(context->smelting);
    if (!maage_status_failed(result)) result = maage_fuel_finalize(context->fuels);
    if (maage_status_failed(result)) goto failed;
    result = content_modules_run(content, config, MAAGE_CONTENT_PHASE_INITIALIZE, trace);
    if (maage_status_failed(result)) goto failed;
    if (maage_status_is_warning(result) && !maage_status_is_warning(outcome)) outcome = result;
    context->config = NULL;
    context->lifecycle = NULL;
    free(content->order);
    free(content->modules);
    content->order = NULL;
    content->modules = NULL;
    content->module_count = 0;
    if (config->failure) memset(config->failure, 0, sizeof(*config->failure));
    *output = content;
    return outcome;
failed:
    if (config->failure && !maage_status_failed(config->failure->result)) config->failure->result = result;
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
    maage_creative_destroy(content->context.creative);
    maage_presentation_destroy(content->context.presentation);
    maage_smelting_destroy(content->context.smelting);
    maage_fuel_destroy(content->context.fuels);
    maage_recipe_registry_destroy(content->context.recipes);
    maage_item_registry_destroy(content->context.items);
    maage_block_registry_destroy(content->context.blocks);
    maage_fluid_destroy(content->context.fluids);
    maage_block_geometry_destroy(content->context.geometry);
    maage_container_registry_destroy(content->context.containers);
    maage_sound_registry_destroy(content->context.sounds);
    maage_biome_registry_destroy(&content->biomes);
    for (node = (content_owner *) content->context.owner; node; node = next) {
        next = node->next;
        node->destroy(node->data);
        free(node);
    }
    free(content->order);
    free(content->modules);
    free(content);
    return maage_status_make(MAAGE_STATUS_SUCCESS, 0, MAAGE_OPERATION_DESTROY, 0);
}

const maage_presentation_registry *maage_content_presentation(const maage_content *content) {
    return content ? content->context.presentation : NULL;
}

const maage_fluid_registry *maage_content_fluids(const maage_content *content) {
    return content ? content->context.fluids : NULL;
}
const maage_block_geometry_registry *maage_content_geometry(const maage_content *content) {
    return content ? content->context.geometry : NULL;
}

const maage_container_registry *maage_content_containers(const maage_content *content) {
    return content ? content->context.containers : NULL;
}

const maage_sound_registry *maage_content_sounds(const maage_content *content) {
    return content ? content->context.sounds : NULL;
}

const maage_creative_registry *maage_content_creative(const maage_content *content) {
    return content ? content->context.creative : NULL;
}
