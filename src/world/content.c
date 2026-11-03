#include "maage/content.h"
#include "registry_policy.h"
#include "maage/log.h"
#include "maage/time.h"
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
    content_policy policy;
    const maage_content_module *const *submitted;
    int finalized, loaded, initialized;
    content_module_slot *modules;
    size_t module_count, *order;
};
static maage_status content_failure(const maage_content_config *config, const maage_content_module *module, const char *dependency, maage_content_phase phase, maage_status result, const char *reason) {
    if (config && config->failure) {
        config->failure->module_name = module ? module->name : NULL;
        config->failure->dependency_name = dependency;
        config->failure->phase = phase;
        config->failure->result = result;
        config->failure->reason = reason;
        config->failure->execution = config->execution;
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
        } else if (!((value >= 'a' && value <= 'z') || (value >= '0' && value <= '9') || value == '_' || value == '-' || value == '.')) return 0;
    }
    return 1;
}
static size_t content_module_find(const maage_content *content, const char *name) {
    size_t i;
    for (i = 0; i < content->module_count; ++i)
        if (!strcmp(content->modules[i].definition->name, name)) return i;
    return content->module_count;
}
maage_status maage_content_module_data(const maage_content_context *context, const char *name, void **data) {
    const maage_content *content;
    size_t index, dependency;
    if (data) *data = NULL;
    if (!context || !context->lifecycle || !name || !data) return maage_status_make(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_CONTEXT_REGISTRY, MAAGE_OPERATION_RESOLVE, 0);
    content = (const maage_content *) context->lifecycle;
    index = content_module_find(content, name);
    if (index == content->module_count) return maage_status_make(MAAGE_STATUS_NOT_FOUND, MAAGE_CONTEXT_REGISTRY, MAAGE_OPERATION_RESOLVE, 0);
    if (context->module && context->module != content->modules[index].definition) {
        for (dependency = 0; dependency < context->module->dependency_count; ++dependency)
            if (!strcmp(context->module->dependencies[dependency], name)) break;
        if (context->phase == MAAGE_CONTENT_PHASE_BIND_INTERNAL || dependency == context->module->dependency_count) return maage_status_make(MAAGE_STATUS_ACCESS_DENIED, MAAGE_CONTEXT_REGISTRY, MAAGE_OPERATION_RESOLVE, 0);
    }
    *data = context->module == content->modules[index].definition ? context->module_data : content->modules[index].data;
    return maage_status_make(MAAGE_STATUS_SUCCESS, 0, MAAGE_OPERATION_RESOLVE, 0);
}
static maage_status content_modules_order(maage_content *content, const maage_content_config *config) {
    const maage_content_module *module;
    unsigned char *selected;
    size_t i, j, k, position, dependency;
    int ready, parent_dependency;
    const char *colon;
    size_t parent;
    maage_status result = maage_status_make(MAAGE_STATUS_SUCCESS, 0, MAAGE_OPERATION_RESOLVE, 0);
    if (config->module_count > (size_t) - 1 / sizeof(*content->modules) || config->module_count > (size_t) - 1 / sizeof(*content->order)) return content_failure(config, NULL, NULL, MAAGE_CONTENT_PHASE_MODULES, maage_status_make(MAAGE_STATUS_BUFFER_OVERFLOW, MAAGE_CONTEXT_REGISTRY, MAAGE_OPERATION_REGISTER, 0), "Module array size overflow");
    content->modules = (content_module_slot *) calloc(config->module_count, sizeof(*content->modules));
    content->order = (size_t *) malloc(config->module_count * sizeof(*content->order));
    selected = (unsigned char *) calloc(config->module_count, 1);
    if (!content->modules || !content->order || !selected) {
        free(selected);
        return content_failure(config, NULL, NULL, MAAGE_CONTENT_PHASE_MODULES, maage_status_make(MAAGE_STATUS_ALLOCATION_FAILED, MAAGE_CONTEXT_REGISTRY, MAAGE_OPERATION_REGISTER, 0), "Module graph allocation failed");
    }
    for (i = 0; i < config->module_count; ++i) {
        module = config->modules[i];
        if (!module || !content_module_name_valid(module->name) || (module->dependency_count && !module->dependencies) || (module->sides & ~(MAAGE_EXECUTION_CLIENT | MAAGE_EXECUTION_INTEGRATED_SERVER | MAAGE_EXECUTION_DEDICATED_SERVER))) {
            result = content_failure(config, module, NULL, MAAGE_CONTENT_PHASE_MODULES, maage_status_make(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_CONTEXT_REGISTRY, MAAGE_OPERATION_REGISTER, 0), "Invalid module name or dependency array");
            goto done;
        }
        for (j = 0; j < i; ++j)
            if (!strcmp(module->name, content->modules[j].definition->name)) {
                result = content_failure(config, module, NULL, MAAGE_CONTENT_PHASE_MODULES, maage_status_make(MAAGE_STATUS_INVALID_STATE, MAAGE_CONTEXT_REGISTRY, MAAGE_OPERATION_REGISTER, 0), "Duplicate module identity");
                goto done;
            }
        MAAGE_LOG_DEBUG_EVENT("maage.content", maage_log_debug("maage.content", "Module %s: %s; dependencies=%lu.", module->name, strchr(module->name, ':') ? "submodule" : "root", (unsigned long) module->dependency_count));
        content->modules[i].definition = module;
    }
    content->module_count = config->module_count;
    for (i = 0; i < content->module_count; ++i) {
        module = content->modules[i].definition;
        colon = strchr(module->name, ':');
        if (colon) {
            parent_dependency = 0;
            for (parent = 0; parent < content->module_count; ++parent)
                if (strlen(content->modules[parent].definition->name) == (size_t) (colon - module->name) && !strncmp(content->modules[parent].definition->name, module->name, (size_t) (colon - module->name))) break;
            if (parent == content->module_count) {
                result = content_failure(config, module, NULL, MAAGE_CONTENT_PHASE_MODULES, maage_status_make(MAAGE_STATUS_NOT_FOUND, MAAGE_CONTEXT_REGISTRY, MAAGE_OPERATION_RESOLVE, 0), "Missing submodule root parent");
                goto done;
            }
            for (j = 0; j < module->dependency_count; ++j)
                if (module->dependencies[j] && !strcmp(module->dependencies[j], content->modules[parent].definition->name)) parent_dependency = 1;
            if (!parent_dependency) {
                result = content_failure(config, module, content->modules[parent].definition->name, MAAGE_CONTENT_PHASE_MODULES, maage_status_make(MAAGE_STATUS_INVALID_STATE, MAAGE_CONTEXT_REGISTRY, MAAGE_OPERATION_RESOLVE, 0), "Submodule must explicitly depend on its root parent");
                goto done;
            }
            MAAGE_LOG_DEBUG_EVENT("maage.content", maage_log_debug("maage.content", "Submodule %s: parent=%s, shared namespace enforced.", module->name, content->modules[parent].definition->name));
        }
        for (j = 0; j < module->dependency_count; ++j) {
            if (!content_module_name_valid(module->dependencies[j])) {
                result = content_failure(config, module, module->dependencies[j], MAAGE_CONTENT_PHASE_MODULES, maage_status_make(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_CONTEXT_REGISTRY, MAAGE_OPERATION_RESOLVE, 0), "Invalid dependency name");
                goto done;
            }
            if (content_module_find(content, module->dependencies[j]) == content->module_count) {
                result = content_failure(config, module, module->dependencies[j], MAAGE_CONTENT_PHASE_MODULES, maage_status_make(MAAGE_STATUS_NOT_FOUND, MAAGE_CONTEXT_REGISTRY, MAAGE_OPERATION_RESOLVE, 0), "Missing dependency");
                goto done;
            }
            MAAGE_LOG_DEBUG_EVENT("maage.content", maage_log_debug("maage.content", "Dependency validated: %s -> %s.", module->name, module->dependencies[j]));
            for (k = 0; k < j; ++k)
                if (!strcmp(module->dependencies[j], module->dependencies[k])) {
                    result = content_failure(config, module, module->dependencies[j], MAAGE_CONTENT_PHASE_MODULES, maage_status_make(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_CONTEXT_REGISTRY, MAAGE_OPERATION_RESOLVE, 0), "Duplicate dependency");
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
            result = content_failure(config, module, j < module->dependency_count ? module->dependencies[j] : NULL, MAAGE_CONTENT_PHASE_MODULES, maage_status_make(MAAGE_STATUS_INVALID_STATE, MAAGE_CONTEXT_REGISTRY, MAAGE_OPERATION_RESOLVE, 0), "Dependency cycle");
            goto done;
        }
        content->order[position] = i;
        selected[i] = 1;
        MAAGE_LOG_DEBUG_EVENT("maage.content", maage_log_debug("maage.content", "Loading order %lu: %s.", (unsigned long) position + 1, content->modules[i].definition->name));
    }
done:
    free(selected);
    return result;
}
const char *maage_content_phase_name(maage_content_phase phase) {
    switch (phase) {
    case MAAGE_CONTENT_PHASE_MODULES:
        return "MODULES";
    case MAAGE_CONTENT_PHASE_DEFINE:
        return "DEFINE";
    case MAAGE_CONTENT_PHASE_BIND_INTERNAL:
        return "BIND, INTERNAL";
    case MAAGE_CONTENT_PHASE_BIND_EXTERNAL:
        return "BIND, EXTERNAL";
    case MAAGE_CONTENT_PHASE_BIND_COMPATIBILITY:
        return "BIND, COMPATIBILITY";
    case MAAGE_CONTENT_PHASE_FINALIZE:
        return "FINALIZE";
    case MAAGE_CONTENT_PHASE_LOAD:
        return "LOAD";
    case MAAGE_CONTENT_PHASE_INITIALIZE:
        return "INITIALIZE";
    }
    return "UNKNOWN";
}
static const char *content_side_name(maage_execution_side side) {
    switch (side) {
    case MAAGE_EXECUTION_CLIENT:
        return "Client";
    case MAAGE_EXECUTION_INTEGRATED_SERVER:
        return "Integrated Server";
    case MAAGE_EXECUTION_DEDICATED_SERVER:
        return "Dedicated Server";
    }
    return "Invalid side";
}
static maage_status content_modules_run(maage_content *content, const maage_content_config *config, maage_content_phase phase, maage_status_trace *trace) {
    maage_content_registration callbacks[2];
    content_module_slot *slot;
    const maage_content_module *module;
    int worked, detail;
    double began;
    size_t i, hook;
    maage_status result, outcome = maage_status_make(MAAGE_STATUS_SUCCESS, 0, MAAGE_OPERATION_REGISTER, 0);
    unsigned char operation = phase == MAAGE_CONTENT_PHASE_DEFINE ? MAAGE_OPERATION_REGISTER : (phase == MAAGE_CONTENT_PHASE_INITIALIZE ? MAAGE_OPERATION_INITIALIZE : MAAGE_OPERATION_RESOLVE);
    content->context.phase = phase;
    detail = maage_log_enabled("maage.content", MAAGE_LOG_DEBUG);
    for (i = 0; i < content->module_count; ++i) {
        slot = &content->modules[content->order[i]];
        module = slot->definition;
        callbacks[1] = NULL;
        worked = 0;
        began = detail ? maage_time_monotonic_seconds() : -1;
        switch (phase) {
        case MAAGE_CONTENT_PHASE_DEFINE:
            callbacks[0] = module->define;
            break;
        case MAAGE_CONTENT_PHASE_BIND_INTERNAL:
            callbacks[0] = module->bind_internal;
            callbacks[1] = module->side_bind_internal;
            break;
        case MAAGE_CONTENT_PHASE_BIND_EXTERNAL:
            callbacks[0] = module->bind_external;
            callbacks[1] = module->side_bind_external;
            break;
        case MAAGE_CONTENT_PHASE_BIND_COMPATIBILITY:
            callbacks[0] = module->bind_compatibility;
            callbacks[1] = module->side_bind_compatibility;
            break;
        default:
            callbacks[0] = module->initialize;
            break;
        }
        for (hook = 0; hook < 2; ++hook) {
            if (!callbacks[hook] || (phase == MAAGE_CONTENT_PHASE_BIND_EXTERNAL && !module->dependency_count) || ((hook || phase == MAAGE_CONTENT_PHASE_INITIALIZE) && !(module->sides & config->execution.side))) continue;
            if ((hook || phase == MAAGE_CONTENT_PHASE_INITIALIZE) && (module->required_capabilities & config->execution.capabilities) != module->required_capabilities) return content_failure(config, module, NULL, phase, maage_status_make(MAAGE_STATUS_SYSTEM_NOT_SUPPORTED, MAAGE_CONTEXT_REGISTRY, operation, 0), "Required side capability unavailable");
            worked = 1;
            content->context.module = module;
            content->context.module_data = slot->data;
            result = callbacks[hook](&content->context);
            slot->data = content->context.module_data;
            content->context.module = NULL;
            content->context.module_data = NULL;
            if (!maage_status_failed(result) && maage_status_failed(content->policy.failure)) result = content->policy.failure;
            if (maage_status_failed(result)) {
                if (maage_status_failed(content->policy.failure)) memcpy(config->failure->content_name, content->policy.content_name, sizeof(config->failure->content_name));
                return content_failure(config, module, content->policy.dependency, phase, MAAGE_STATUS_PROPAGATE(trace, result, MAAGE_CONTEXT_REGISTRY, operation), content->policy.reason ? content->policy.reason : "Module callback failed");
            }
            if (maage_status_is_warning(result)) {
                if (!maage_status_is_warning(outcome)) outcome = result;
                maage_log_trace_status(MAAGE_LOG_WARNING, "maage.content", result, trace, "%s: %s completed with warning; side=%s.", maage_content_phase_name(phase), module->name, content_side_name(config->execution.side));
                maage_status_trace_init(trace);
            }
        }
        if (worked && detail) MAAGE_LOG_DEBUG_EVENT("maage.content", maage_log_debug("maage.content", "%s | %s | %s | Complete (%.3f ms).", content_side_name(config->execution.side), module->name, maage_content_phase_name(phase), began >= 0 ? (maage_time_monotonic_seconds() - began) * 1000 : 0));
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
static maage_status content_validate_presentation(const maage_content_context *context) {
    const maage_block_state_definition *state;
    const maage_block_face_definition *face;
    const maage_fluid_definition *fluid;
    const maage_container_definition *container;
    const maage_block_geometry_definition *geometry;
    const maage_model_definition *model;
    const maage_entity_type_definition *entity;
    const maage_item_definition *item;
    const maage_item_variant_definition *variant;
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
    for (i = 0; i < maage_model_registry_count(context->models); ++i) {
        model = maage_model_registry_get(context->models, (maage_model_id) i + 1);
        if (!model || !maage_presentation_texture(context->presentation, model->texture_id)) return maage_status_make(MAAGE_STATUS_NOT_FOUND, MAAGE_CONTEXT_REGISTRY, MAAGE_OPERATION_FINALIZE, 0);
        for (j = 0; j < model->part_count; ++j)
            if (model->parts[j].texture_id != MAAGE_CONTENT_ID_INVALID && !maage_presentation_texture(context->presentation, model->parts[j].texture_id)) return maage_status_make(MAAGE_STATUS_NOT_FOUND, MAAGE_CONTEXT_REGISTRY, MAAGE_OPERATION_FINALIZE, 0);
    }
    for (i = 0; i < maage_entity_type_count(context->entity_types); ++i) {
        entity = maage_entity_type_at(context->entity_types, i);
        if (!entity || (entity->model_id && !maage_model_registry_get(context->models, entity->model_id)) || (entity->model_id && !maage_presentation_texture(context->presentation, entity->texture_id))) return maage_status_make(MAAGE_STATUS_NOT_FOUND, MAAGE_CONTEXT_REGISTRY, MAAGE_OPERATION_FINALIZE, 0);
        for (j = 0; j < entity->texture_count; ++j)
            if (!maage_presentation_texture(context->presentation, entity->textures[j])) return maage_status_make(MAAGE_STATUS_NOT_FOUND, MAAGE_CONTEXT_REGISTRY, MAAGE_OPERATION_FINALIZE, 0);
        if ((entity->ambient_sound && !maage_sound_get(context->sounds, entity->ambient_sound)) || (entity->hurt_sound && !maage_sound_get(context->sounds, entity->hurt_sound)) || (entity->death_sound && !maage_sound_get(context->sounds, entity->death_sound)) || (entity->attack_sound && !maage_sound_get(context->sounds, entity->attack_sound))) return maage_status_make(MAAGE_STATUS_NOT_FOUND, MAAGE_CONTEXT_REGISTRY, MAAGE_OPERATION_FINALIZE, 0);
    }
    for (i = 0; i < maage_item_registry_count(context->items); ++i) {
        item = maage_item_registry_at(context->items, i);
        if (!item || (item->model == MAAGE_ITEM_MODEL_REGISTERED && !maage_model_registry_get(context->models, item->registered_model)) || (item->spawn_type && !maage_entity_type_get(context->entity_types, item->spawn_type))) return maage_status_make(MAAGE_STATUS_NOT_FOUND, MAAGE_CONTEXT_REGISTRY, MAAGE_OPERATION_FINALIZE, 0);
        for (j = 0; j < item->sprite_layer_count; ++j)
            if (!maage_presentation_texture(context->presentation, item->sprite_layers[j].texture)) return maage_status_make(MAAGE_STATUS_NOT_FOUND, MAAGE_CONTEXT_REGISTRY, MAAGE_OPERATION_FINALIZE, 0);
        for (j = 0; j < item->variant_count; ++j) {
            variant = maage_item_variant_get(context->items, item->id, (maage_item_variant_id) j);
            if (!variant || (item->has_block && (!maage_block_registry_state(context->blocks, variant->icon_state_id) || !maage_block_registry_state(context->blocks, variant->placement_state_id))) || (item->model == MAAGE_ITEM_MODEL_SPRITE && !item->sprite_layer_count && !maage_presentation_texture(context->presentation, variant->texture_id))) {
                if (context->config && context->config->failure) {
                    strncpy(context->config->failure->content_name, maage_item_registry_name(context->items, item->id), sizeof(context->config->failure->content_name) - 1);
                    context->config->failure->content_name[sizeof(context->config->failure->content_name) - 1] = '\0';
                    context->config->failure->reason = "Item variant references an unregistered state or texture";
                }
                return maage_status_make(MAAGE_STATUS_NOT_FOUND, MAAGE_CONTEXT_REGISTRY, MAAGE_OPERATION_FINALIZE, 0);
            }
        }
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

/* Reserved named identities must correspond to successful physical insertions.
 * This rejects callbacks which continue after a failed registration. */
static maage_status content_validate_registrations(const maage_content *content, maage_content_failure *failure) {
    const maage_content_context *context = &content->context;
    size_t actual[CONTENT_BEHAVIOR + 1], claimed[CONTENT_BEHAVIOR + 1], i, j;
    memset(claimed, 0, sizeof(claimed));
    actual[CONTENT_BLOCK] = content_blocks_identity_count(context->blocks, CONTENT_BLOCK);
    actual[CONTENT_ITEM] = maage_item_registry_count(context->items);
    actual[CONTENT_BIOME] = maage_biome_registry_count(context->biomes);
    actual[CONTENT_FLUID] = maage_fluid_count(context->fluids);
    actual[CONTENT_GEOMETRY] = maage_block_geometry_count(context->geometry);
    actual[CONTENT_CONTAINER] = maage_container_count(context->containers);
    actual[CONTENT_CREATIVE] = maage_creative_tab_count(context->creative);
    actual[CONTENT_SOUND_RESOURCE] = maage_sound_resource_count(context->sounds);
    actual[CONTENT_SOUND] = maage_sound_count(context->sounds);
    actual[CONTENT_MATERIAL] = maage_sound_material_count(context->sounds);
    actual[CONTENT_MUSIC] = maage_sound_music_count(context->sounds);
    actual[CONTENT_AMBIENT] = maage_sound_ambient_count(context->sounds);
    actual[CONTENT_ENTITY] = content_blocks_identity_count(context->blocks, CONTENT_ENTITY);
    actual[CONTENT_FEATURE] = content_blocks_identity_count(context->blocks, CONTENT_FEATURE);
    actual[CONTENT_ORE] = content_blocks_identity_count(context->blocks, CONTENT_ORE);
    actual[CONTENT_ENTITY_TYPE] = maage_entity_type_count(context->entity_types);
    actual[CONTENT_MODEL] = maage_model_registry_count(context->models);
    actual[CONTENT_PROPERTY] = maage_property_count(context->traits);
    actual[CONTENT_PROPERTY_VALUE] = 0;
    for (j = 0; j < maage_property_count(context->traits); ++j)
        actual[CONTENT_PROPERTY_VALUE] += maage_property_value_count(context->traits, (maage_property_id) j);
    actual[CONTENT_TAG] = maage_tag_count(context->traits);
    actual[CONTENT_BEHAVIOR] = maage_behavior_count(context->traits);
    for (i = 0; i < content->policy.count; ++i)
        if (content->policy.identities[i].domain <= CONTENT_BEHAVIOR) ++claimed[content->policy.identities[i].domain];
    for (i = 0; i <= CONTENT_BEHAVIOR; ++i)
        if (actual[i] != claimed[i]) {
            failure->dependency_name = content_policy_domain_name((content_domain) i);
            failure->reason = "Callback continued after unsuccessful registration";
            return maage_status_make(MAAGE_STATUS_INVALID_STATE, MAAGE_CONTEXT_REGISTRY, MAAGE_OPERATION_FINALIZE, 0);
        }
    return maage_status_make(MAAGE_STATUS_SUCCESS, 0, MAAGE_OPERATION_FINALIZE, 0);
}

static void content_contract_target(maage_content_failure *failure, const char *subject, const char *property, const char *reason) {
    size_t length;
    strncpy(failure->content_name, subject ? subject : "<unnamed>", sizeof(failure->content_name) - 1);
    failure->content_name[sizeof(failure->content_name) - 1] = '\0';
    if (property) {
        length = strlen(failure->content_name);
        if (length + 4 < sizeof(failure->content_name)) {
            strcat(failure->content_name, " -> ");
            strncat(failure->content_name, property, sizeof(failure->content_name) - strlen(failure->content_name) - 1);
        }
    }
    failure->reason = reason;
}
/* Validate contracts before freezing to preserve the offending subject/property
 * in caller-owned diagnostics even after a failed composition is destroyed. */
static maage_status content_validate_contract(const maage_content_context *context, unsigned int domain, unsigned long subject, maage_property_set_id properties, const char *name) {
    const maage_property_set *set, *constants;
    const maage_tag_definition *tag;
    maage_status result = maage_traits_validate(context->traits, domain, subject, properties);
    maage_content_failure *failure = context->config->failure;
    size_t i, j;
    if (!maage_status_failed(result)) return result;
    content_contract_target(failure, name, NULL, "Invalid typed property configuration or tag contract");
    set = maage_property_set_get(context->traits, properties);
    constants = maage_property_set_get(context->traits, maage_traits_constants(context->traits, domain, subject));
    if (set && constants) {
        for (i = 0; i < set->count; ++i)
            if (maage_property_set_value(context->traits, maage_traits_constants(context->traits, domain, subject), set->values[i].property, NULL)) {
                content_contract_target(failure, name, maage_property_name(context->traits, set->values[i].property), "State/variant property overlaps a type constant");
                return MAAGE_STATUS_TRACE(context->trace, result, MAAGE_CONTEXT_REGISTRY, MAAGE_OPERATION_FINALIZE);
            }
        for (i = 0; i < maage_tag_count(context->traits); ++i) {
            if (!maage_traits_has(context->traits, domain, subject, (maage_tag_id) i)) continue;
            tag = maage_tag_get(context->traits, (maage_tag_id) i);
            for (j = 0; j < tag->required_count; ++j)
                if (!maage_traits_value(context->traits, domain, subject, properties, tag->required[j], NULL)) {
                    content_contract_target(failure, name, maage_property_name(context->traits, tag->required[j]), "Missing required property in effective tag contract");
                    return MAAGE_STATUS_TRACE(context->trace, result, MAAGE_CONTEXT_REGISTRY, MAAGE_OPERATION_FINALIZE);
                }
        }
    }
    return MAAGE_STATUS_TRACE(context->trace, result, MAAGE_CONTEXT_REGISTRY, MAAGE_OPERATION_FINALIZE);
}
static maage_status content_validate_contracts(const maage_content_context *context) {
    const maage_block_state_definition *state;
    const maage_item_definition *item;
    const maage_item_variant_definition *variant;
    const char *name;
    size_t i, j;
    maage_status result;
    for (i = 0; i < maage_block_registry_block_count(context->blocks); ++i) {
        for (j = 0; j < maage_tag_count(context->traits); ++j)
            if (maage_traits_has(context->traits, MAAGE_TRAIT_BLOCK, (maage_block_id) i, (maage_tag_id) j)) break;
        if (j == maage_tag_count(context->traits)) {
            content_contract_target(context->config->failure, maage_block_registry_block_name(context->blocks, (maage_block_id) i), NULL, "Every block must implement at least one registered tag");
            return MAAGE_STATUS_TRACE(context->trace, maage_status_make(MAAGE_STATUS_INVALID_STATE, MAAGE_CONTEXT_REGISTRY, MAAGE_OPERATION_FINALIZE, 0), MAAGE_CONTEXT_REGISTRY, MAAGE_OPERATION_FINALIZE);
        }
    }
    for (i = 0; i < maage_block_registry_state_count(context->blocks); ++i) {
        state = maage_block_registry_state(context->blocks, (maage_block_state_id) i);
        name = state ? maage_block_registry_block_name(context->blocks, state->block_id) : NULL;
        result = content_validate_contract(context, MAAGE_TRAIT_BLOCK, state ? state->block_id : MAAGE_CONTENT_ID_INVALID, state ? state->properties : MAAGE_CONTENT_ID_INVALID, name);
        if (maage_status_failed(result)) return result;
    }
    for (i = 0; i < maage_item_registry_count(context->items); ++i) {
        item = maage_item_registry_at(context->items, i);
        for (j = 0; j < item->variant_count; ++j) {
            variant = maage_item_variant_get(context->items, item->id, (maage_item_variant_id) j);
            result = content_validate_contract(context, MAAGE_TRAIT_ITEM, item->id, variant ? variant->properties : MAAGE_CONTENT_ID_INVALID, maage_item_registry_name(context->items, item->id));
            if (maage_status_failed(result)) return result;
        }
    }
    return maage_status_make(MAAGE_STATUS_SUCCESS, 0, MAAGE_OPERATION_FINALIZE, 0);
}

maage_status maage_content_create(const maage_content_config *config, maage_content **output, maage_status_trace *trace) {
    maage_content *content = NULL;
    maage_content_config loading;
    maage_content_failure local_failure;
    maage_content_context *context;
    double began = maage_time_monotonic_seconds(), ended;
    maage_content_phase phase;
    maage_status result, outcome = maage_status_make(MAAGE_STATUS_SUCCESS, 0, MAAGE_OPERATION_CREATE, 0);
    size_t i;
    const maage_item_definition *item;
    const maage_block_definition *block;
    const maage_biome_definition *biome;
    if (config) {
        loading = *config;
        if (!loading.failure) loading.failure = &local_failure;
    } else {
        memset(&loading, 0, sizeof(loading));
        loading.failure = &local_failure;
    }
    config = &loading;
    if (output) *output = NULL;
    memset(config->failure, 0, sizeof(*config->failure));
    config->failure->execution = config->execution;
    if (!output || !config->modules || !config->module_count) {
        result = content_failure(config, NULL, NULL, MAAGE_CONTENT_PHASE_MODULES, maage_status_make(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_CONTEXT_REGISTRY, MAAGE_OPERATION_CREATE, 0), "Invalid composition arguments");
        goto failed;
    }
    content = (maage_content *) calloc(1, sizeof(*content));
    if (!content) {
        result = content_failure(config, NULL, NULL, MAAGE_CONTENT_PHASE_MODULES, maage_status_make(MAAGE_STATUS_ALLOCATION_FAILED, MAAGE_CONTEXT_REGISTRY, MAAGE_OPERATION_CREATE, 0), "Content allocation failed");
        goto failed;
    }
    context = &content->context;
    context->config = config;
    context->trace = trace;
    context->execution = config->execution;
    content->submitted = config->modules;
    content->policy.context = context;
    if ((config->execution.side != MAAGE_EXECUTION_CLIENT && config->execution.side != MAAGE_EXECUTION_INTEGRATED_SERVER && config->execution.side != MAAGE_EXECUTION_DEDICATED_SERVER) ||
        (config->execution.side == MAAGE_EXECUTION_CLIENT && config->execution.capabilities != MAAGE_EXECUTION_PRESENTATION) ||
        (config->execution.side != MAAGE_EXECUTION_CLIENT && config->execution.capabilities != (MAAGE_EXECUTION_AUTHORITY | MAAGE_EXECUTION_HEADLESS))) {
        result = content_failure(config, NULL, NULL, MAAGE_CONTENT_PHASE_MODULES, maage_status_make(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_CONTEXT_REGISTRY, MAAGE_OPERATION_INITIALIZE, 0), "Invalid execution side/capabilities");
        goto failed;
    }
    maage_log_info("maage.content", "Loading %lu modules (%s).", (unsigned long) config->module_count, content_side_name(config->execution.side));
    context->biomes = &content->biomes;
    context->lifecycle = content;
    result = content_modules_order(content, config);
    if (maage_status_failed(result)) goto failed;
    result = maage_traits_registry_create(&context->traits);
    if (!maage_status_failed(result)) result = maage_presentation_create(&context->presentation);
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
    if (!maage_status_failed(result)) result = maage_entity_type_registry_create(&context->entity_types);
    if (!maage_status_failed(result)) result = maage_model_registry_create(&context->models);
    if (!maage_status_failed(result)) result = maage_block_registry_bind_traits(context->blocks, context->traits);
    if (!maage_status_failed(result)) result = maage_item_registry_bind_traits(context->items, context->traits);
    if (!maage_status_failed(result)) result = maage_item_registry_bind_blocks(context->items, context->blocks);
    if (!maage_status_failed(result)) result = maage_block_registry_bind_fluids(context->blocks, context->fluids);
    if (!maage_status_failed(result)) result = maage_block_registry_bind_geometry(context->blocks, context->geometry);
    if (!maage_status_failed(result)) result = maage_block_registry_bind_containers(context->blocks, context->containers);
    if (!maage_status_failed(result)) result = maage_block_registry_bind_sounds(context->blocks, context->sounds);
    if (maage_status_failed(result)) goto failed;
    content_traits_policy(context->traits, &content->policy);
    content_blocks_policy(context->blocks, &content->policy);
    content_items_policy(context->items, &content->policy);
    context->biomes->ownership_policy = &content->policy;
    content_fluids_policy(context->fluids, &content->policy);
    content_geometry_policy(context->geometry, &content->policy);
    content_containers_policy(context->containers, &content->policy);
    content_creative_policy(context->creative, &content->policy);
    content_sounds_policy(context->sounds, &content->policy);
    content_presentation_policy(context->presentation, &content->policy);
    content_entity_types_policy(context->entity_types, &content->policy);
    content_models_policy(context->models, &content->policy);
    for (phase = MAAGE_CONTENT_PHASE_DEFINE; phase <= MAAGE_CONTENT_PHASE_BIND_COMPATIBILITY; phase = (maage_content_phase) (phase + 1)) {
        result = content_modules_run(content, config, phase, trace);
        if (maage_status_failed(result)) goto failed;
        if (maage_status_is_warning(result) && !maage_status_is_warning(outcome)) outcome = result;
    }
    content->policy.engine = 1;
    context->phase = MAAGE_CONTENT_PHASE_FINALIZE;

    if (config->failure) config->failure->phase = MAAGE_CONTENT_PHASE_FINALIZE;
    if (maage_block_registry_traits(context->blocks) != context->traits || maage_item_registry_traits(context->items) != context->traits || maage_item_registry_blocks(context->items) != context->blocks || maage_block_registry_fluids(context->blocks) != context->fluids || maage_block_registry_geometry(context->blocks) != context->geometry || maage_block_registry_containers(context->blocks) != context->containers || maage_block_registry_sounds(context->blocks) != context->sounds) {
        result = maage_status_make(MAAGE_STATUS_INVALID_STATE, MAAGE_CONTEXT_REGISTRY, MAAGE_OPERATION_FINALIZE, 0);
        config->failure->reason = "Module changed composition-owned registry links";
        goto failed;
    }
    result = content_validate_registrations(content, config->failure);
    if (maage_status_failed(result)) goto failed;
    config->failure->dependency_name = "tag/property contracts";
    result = content_validate_contracts(context);
    if (maage_status_failed(result)) goto failed;
    config->failure->dependency_name = "presentation references";
    result = content_validate_presentation(context);
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
        if (!block || (block->drop_count && !block->drops && !maage_traits_has_behavior(context->traits, MAAGE_TRAIT_BLOCK, block->id, MAAGE_BEHAVIOR_DROPS) && !maage_item_registry_get(context->items, block->drop_item_id))) {
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
    config->failure->dependency_name = "maage_presentation_finalize";
    result = maage_presentation_finalize(context->presentation);
    if (maage_status_failed(result)) goto failed;
    config->failure->dependency_name = "maage_traits_registry_finalize";
    result = maage_traits_registry_finalize(context->traits);
    if (maage_status_failed(result)) goto failed;
    config->failure->dependency_name = "maage_item_registry_finalize";
    result = maage_item_registry_finalize(context->items);
    if (maage_status_failed(result)) goto failed;
    config->failure->dependency_name = "maage_creative_finalize";
    result = maage_creative_finalize(context->creative);
    if (maage_status_failed(result)) goto failed;
    config->failure->dependency_name = "maage_sound_finalize";
    result = maage_sound_finalize(context->sounds);
    if (maage_status_failed(result)) goto failed;
    config->failure->dependency_name = "maage_fluid_finalize";
    result = maage_fluid_finalize(context->fluids, context->blocks);
    if (maage_status_failed(result)) goto failed;
    config->failure->dependency_name = "maage_block_geometry_finalize";
    result = maage_block_geometry_finalize(context->geometry);
    if (maage_status_failed(result)) goto failed;
    config->failure->dependency_name = "maage_container_finalize";
    result = maage_container_finalize(context->containers);
    if (maage_status_failed(result)) goto failed;
    config->failure->dependency_name = "maage_block_registry_finalize";
    result = maage_block_registry_finalize(context->blocks);
    if (maage_status_failed(result)) goto failed;
    config->failure->dependency_name = "maage_biome_registry_finalize";
    result = maage_biome_registry_finalize(context->biomes);
    if (maage_status_failed(result)) goto failed;
    config->failure->dependency_name = "maage_recipe_registry_finalize";
    result = maage_recipe_registry_finalize(context->recipes);
    if (maage_status_failed(result)) goto failed;
    config->failure->dependency_name = "maage_smelting_finalize";
    result = maage_smelting_finalize(context->smelting);
    if (maage_status_failed(result)) goto failed;
    config->failure->dependency_name = "maage_fuel_finalize";
    result = maage_fuel_finalize(context->fuels);
    if (maage_status_failed(result)) goto failed;
    config->failure->dependency_name = "maage_model_registry_finalize";
    result = maage_model_registry_finalize(context->models);
    if (maage_status_failed(result)) goto failed;
    config->failure->dependency_name = "maage_entity_type_finalize";
    result = maage_entity_type_finalize(context->entity_types, context->items, context->blocks, context->biomes);
    if (maage_status_failed(result)) goto failed;
    config->failure->dependency_name = NULL;
    content->policy.engine = 0;
    content->policy.sealed = 1;
    content->finalized = 1;
    maage_log_info("maage.content", "%s | FINALIZE | blocks=%lu states=%lu items=%lu biomes=%lu creative_tabs=%lu sounds=%lu entity_types=%lu models=%lu.", content_side_name(config->execution.side), (unsigned long) maage_block_registry_block_count(context->blocks), (unsigned long) maage_block_registry_state_count(context->blocks), (unsigned long) maage_item_registry_count(context->items), (unsigned long) maage_biome_registry_count(context->biomes), (unsigned long) maage_creative_tab_count(context->creative), (unsigned long) maage_sound_count(context->sounds), (unsigned long) maage_entity_type_count(context->entity_types), (unsigned long) maage_model_registry_count(context->models));
    if (config->identity_peer) {
        result = maage_content_validate_identity(content, config->identity_peer, trace);
        if (maage_status_failed(result)) {
            config->failure->reason = "Numeric Client/Server identity mismatch";
            goto failed;
        }
    }
    if (config->execution.side != MAAGE_EXECUTION_CLIENT) {
        result = content_modules_run(content, config, MAAGE_CONTENT_PHASE_INITIALIZE, trace);
        if (maage_status_failed(result)) goto failed;
        content->initialized = 1;
        if (maage_status_is_warning(result) && !maage_status_is_warning(outcome)) outcome = result;
        maage_log_info("maage.content", "%s | INITIALIZE | Complete.", content_side_name(config->execution.side));
    }
    context->config = NULL;
    context->trace = NULL;
    ended = maage_time_monotonic_seconds();
    if (began >= 0 && ended >= began) maage_log_info("maage.content", "Composed %lu modules successfully: side=%s elapsed=%.3f ms.", (unsigned long) content->module_count, content_side_name(config->execution.side), (ended - began) * 1000);
    else maage_log_info("maage.content", "Composed %lu modules successfully: side=%s elapsed unavailable.", (unsigned long) content->module_count, content_side_name(config->execution.side));
    if (config->failure) memset(config->failure, 0, sizeof(*config->failure));
    *output = content;
    return outcome;
failed:
    if (content && maage_status_failed(content->policy.failure)) {
        if (!config->failure->reason) config->failure->reason = content->policy.reason;
        if (!config->failure->dependency_name) config->failure->dependency_name = content->policy.dependency;
        if (!config->failure->content_name[0]) memcpy(config->failure->content_name, content->policy.content_name, sizeof(config->failure->content_name));
    }
    if (config->failure && !maage_status_failed(config->failure->result)) config->failure->result = result;
    result = MAAGE_STATUS_PROPAGATE(trace, result, MAAGE_CONTEXT_REGISTRY, MAAGE_OPERATION_CREATE);
    maage_log_trace_status(MAAGE_LOG_ERROR, "maage.content", result, trace, "Loading failed: module=%s phase=%s dependency/registry=%s content=%s side=%s reason=%s.", config->failure->module_name ? config->failure->module_name : "<shared registries>", maage_content_phase_name(config->failure->phase), config->failure->dependency_name ? config->failure->dependency_name : "<none>", config->failure->content_name[0] ? config->failure->content_name : "<none>", content_side_name(config->execution.side), config->failure->reason ? config->failure->reason : "Registry creation, validation or finalization failed");
    maage_content_destroy(content);
    return result;
}
const maage_entity_type_registry *maage_content_entity_types(const maage_content *content) {
    return content ? content->context.entity_types : NULL;
}
const maage_model_registry *maage_content_models(const maage_content *content) {
    return content ? content->context.models : NULL;
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
    maage_entity_type_destroy(content->context.entity_types);
    maage_model_registry_destroy(content->context.models);
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
    maage_traits_registry_destroy(content->context.traits);
    for (node = (content_owner *) content->context.owner; node; node = next) {
        next = node->next;
        node->destroy(node->data);
        free(node);
    }
    content_policy_destroy(&content->policy);
    free(content->order);
    free(content->modules);
    free(content);
    return maage_status_make(MAAGE_STATUS_SUCCESS, 0, MAAGE_OPERATION_DESTROY, 0);
}

const maage_traits_registry *maage_content_traits(const maage_content *content) {
    return content ? content->context.traits : NULL;
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

const maage_execution_context *maage_content_execution(const maage_content *content) {
    return content ? &content->context.execution : NULL;
}
maage_status maage_content_composition(const maage_content *content, const maage_content_module *const **modules, size_t *count) {
    if (modules) *modules = NULL;
    if (count) *count = 0;
    if (!content || !content->finalized || !modules || !count) return maage_status_make(MAAGE_STATUS_INVALID_STATE, MAAGE_CONTEXT_REGISTRY, MAAGE_OPERATION_RESOLVE, 0);
    *modules = content->submitted;
    *count = content->module_count;
    return maage_status_make(MAAGE_STATUS_SUCCESS, 0, MAAGE_OPERATION_RESOLVE, 0);
}
static maage_status content_identity_mismatch(const maage_content *content, maage_status_trace *trace, unsigned char domain, const char *name) {
    maage_content_failure *failure = content && content->context.config ? content->context.config->failure : NULL;
    if (failure) {
        failure->dependency_name = domain >= 2 && domain < CONTENT_INVENTORY_BINDING + 3 ? content_policy_domain_name((content_domain) (domain - 2)) : (domain == 32 ? "block states" : (domain == 33 ? "presentation atlas" : (domain == 34 ? "creative entries" : (domain == 35 ? "traits" : (domain == 36 ? "item variants" : "identity maps")))));
        if (name) {
            strncpy(failure->content_name, name, sizeof(failure->content_name) - 1);
            failure->content_name[sizeof(failure->content_name) - 1] = '\0';
        }
    }
    return MAAGE_STATUS_TRACE(trace, maage_status_make(MAAGE_STATUS_INVALID_STATE, MAAGE_CONTEXT_REGISTRY, MAAGE_OPERATION_RESOLVE, domain), MAAGE_CONTEXT_REGISTRY, MAAGE_OPERATION_FINALIZE);
}
maage_status maage_content_validate_identity(const maage_content *content, const maage_content *peer, maage_status_trace *trace) {
    const content_identity *left, *right;
    const maage_block_state_definition *state, *peer_state;
    const maage_texture_definition *texture, *peer_texture;
    const maage_creative_entry *entry, *peer_entry;
    const maage_content_context *a, *b;
    const maage_item_definition *item, *peer_item;
    const maage_item_variant_definition *variant, *peer_variant;
    size_t i, j;
    if (!content || !peer || !content->finalized || !peer->finalized) return content_identity_mismatch(content, trace, 0, NULL);
    a = &content->context;
    b = &peer->context;
    for (i = 0; i < content->policy.count; ++i) {
        left = &content->policy.identities[i];
        right = content_policy_identity(&peer->policy, left->domain, left->name);
        if (!right || left->id != right->id || strcmp(left->owner->name, right->owner->name)) return content_identity_mismatch(content, trace, (unsigned char) (left->domain + 2), left->name);
    }
    if (content->policy.count != peer->policy.count)
        for (i = 0; i < peer->policy.count; ++i) {
            right = &peer->policy.identities[i];
            if (!content_policy_identity(&content->policy, right->domain, right->name)) return content_identity_mismatch(content, trace, (unsigned char) (right->domain + 2), right->name);
        }
    if (maage_status_failed(maage_traits_validate_identity(a->traits, b->traits))) return content_identity_mismatch(content, trace, 35, NULL);
    for (i = 0; i < maage_item_registry_count(a->items); ++i) {
        item = maage_item_registry_at(a->items, i);
        peer_item = maage_item_registry_at(b->items, i);
        if (!item || !peer_item || item->id != peer_item->id || item->variant_count != peer_item->variant_count) return content_identity_mismatch(content, trace, 36, NULL);
        for (j = 0; j < item->variant_count; ++j) {
            variant = maage_item_variant_get(a->items, item->id, (maage_item_variant_id) j);
            peer_variant = maage_item_variant_get(b->items, peer_item->id, (maage_item_variant_id) j);
            if (!variant || !peer_variant || variant->properties != peer_variant->properties) return content_identity_mismatch(content, trace, 36, NULL);
        }
    }
    if (maage_block_registry_state_count(a->blocks) != maage_block_registry_state_count(b->blocks)) return content_identity_mismatch(content, trace, 32, NULL);
    for (i = 0; i < maage_block_registry_state_count(a->blocks); ++i) {
        state = maage_block_registry_state(a->blocks, (maage_block_state_id) i);
        peer_state = maage_block_registry_state(b->blocks, (maage_block_state_id) i);
        if (!state || !peer_state || state->block_id != peer_state->block_id || state->properties != peer_state->properties) return content_identity_mismatch(content, trace, 32, NULL);
    }
    if (maage_presentation_source_count(a->presentation) != maage_presentation_source_count(b->presentation) || maage_presentation_texture_count(a->presentation) != maage_presentation_texture_count(b->presentation)) return content_identity_mismatch(content, trace, 33, NULL);
    for (i = 0; i < maage_presentation_source_count(a->presentation); ++i)
        if (strcmp(maage_presentation_source(a->presentation, (unsigned long) i), maage_presentation_source(b->presentation, (unsigned long) i))) return content_identity_mismatch(content, trace, 33, NULL);
    for (i = 0; i < maage_presentation_texture_count(a->presentation); ++i) {
        texture = maage_presentation_texture(a->presentation, (maage_texture_id) i);
        peer_texture = maage_presentation_texture(b->presentation, (maage_texture_id) i);
        if (texture->source != peer_texture->source || texture->fallback != peer_texture->fallback || texture->opaque != peer_texture->opaque || texture->frame_ticks != peer_texture->frame_ticks || texture->pingpong != peer_texture->pingpong || texture->frame_edge != peer_texture->frame_edge || texture->sheet_width != peer_texture->sheet_width || texture->sheet_height != peer_texture->sheet_height) return content_identity_mismatch(content, trace, 33, NULL);
    }
    if (maage_creative_entry_count(a->creative) != maage_creative_entry_count(b->creative)) return content_identity_mismatch(content, trace, 34, NULL);
    for (i = 0; i < maage_creative_entry_count(a->creative); ++i) {
        entry = maage_creative_get(a->creative, (maage_creative_entry_id) i + 1);
        peer_entry = maage_creative_get(b->creative, (maage_creative_entry_id) i + 1);
        if (!entry || !peer_entry || entry->tab != peer_entry->tab || entry->stack.item_id != peer_entry->stack.item_id || entry->stack.variant != peer_entry->stack.variant || entry->stack.count != peer_entry->stack.count || entry->stack.damage != peer_entry->stack.damage) return content_identity_mismatch(content, trace, 34, NULL);
    }
    MAAGE_LOG_DEBUG_EVENT("maage.content", maage_log_debug("maage.content", "Numeric identities validated: %s -> %s.", content_side_name(a->execution.side), content_side_name(b->execution.side)));
    return maage_status_make(MAAGE_STATUS_SUCCESS, 0, MAAGE_OPERATION_RESOLVE, 0);
}

maage_status maage_content_load_complete(maage_content *content, maage_status_trace *trace) {
    if (!content || !content->finalized || content->context.execution.side != MAAGE_EXECUTION_CLIENT) return MAAGE_STATUS_TRACE(trace, maage_status_make(MAAGE_STATUS_INVALID_STATE, MAAGE_CONTEXT_REGISTRY, MAAGE_OPERATION_LOAD, 0), MAAGE_CONTEXT_REGISTRY, MAAGE_OPERATION_LOAD);
    content->loaded = 1;
    content->context.phase = MAAGE_CONTENT_PHASE_LOAD;
    maage_log_info("maage.content", "Client | LOAD | Resources ready.");
    return maage_status_make(MAAGE_STATUS_SUCCESS, 0, MAAGE_OPERATION_LOAD, 0);
}
maage_status maage_content_initialize(maage_content *content, maage_status_trace *trace) {
    maage_content_config config;
    maage_content_failure failure;
    maage_status result;
    if (!content || !content->finalized || content->initialized || (content->context.execution.side == MAAGE_EXECUTION_CLIENT && !content->loaded)) return MAAGE_STATUS_TRACE(trace, maage_status_make(MAAGE_STATUS_INVALID_STATE, MAAGE_CONTEXT_REGISTRY, MAAGE_OPERATION_INITIALIZE, 0), MAAGE_CONTEXT_REGISTRY, MAAGE_OPERATION_INITIALIZE);
    memset(&config, 0, sizeof(config));
    memset(&failure, 0, sizeof(failure));
    config.execution = content->context.execution;
    config.modules = content->submitted;
    config.module_count = content->module_count;
    config.failure = &failure;
    content->initialized = -1; /* Failed partial initialization cannot be retried. */
    content->context.trace = trace;
    content->context.config = &config;
    result = content_modules_run(content, &config, MAAGE_CONTENT_PHASE_INITIALIZE, trace);
    content->context.config = NULL;
    content->context.trace = NULL;
    if (maage_status_failed(result)) {
        maage_log_trace_status(MAAGE_LOG_ERROR, "maage.content", result, trace, "%s | %s | INITIALIZE | %s.", content_side_name(config.execution.side), failure.module_name ? failure.module_name : "<registries>", failure.reason ? failure.reason : "Failed");
        return result;
    }
    content->initialized = 1;
    maage_log_info("maage.content", "%s | INITIALIZE | Complete.", content_side_name(config.execution.side));
    return result;
}
