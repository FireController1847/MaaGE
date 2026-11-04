#include "base_internal.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

static maage_status base_behavior_tag(maage_content_context *, const char *, const maage_block_definition *, unsigned long, const maage_property_id *, size_t, maage_tag_id *);
static maage_status base_behavior_contract(maage_content_context *, const char *, const maage_block_definition *, const maage_property_id *, size_t, maage_tag_id *);
static maage_status base_property(maage_content_context *context, const char *name, unsigned int kind, unsigned long minimum, unsigned long maximum, maage_property_id *output) {
    maage_property_definition definition;
    maage_status result;
    memset(&definition, 0, sizeof(definition));
    definition.kind = kind;
    definition.minimum = minimum;
    definition.maximum = maximum;
    result = maage_property_register(context->traits, name, &definition);
    if (!maage_status_failed(result)) *output = definition.id;
    return result;
}
static maage_status base_tag(maage_content_context *context, const char *name, const maage_property_id *required, size_t count, maage_tag_id *output) {
    maage_tag_definition definition;
    maage_status result;
    memset(&definition, 0, sizeof(definition));
    definition.domains = MAAGE_TRAIT_BLOCK | MAAGE_TRAIT_ITEM;
    definition.required = required;
    definition.required_count = count;
    result = maage_tag_register(context->traits, name, &definition);
    if (!maage_status_failed(result)) *output = definition.id;
    return result;
}
maage_status maage_base_traits(maage_content_context *context, maage_base_ids *ids) {
    static const char *const woods[] = {"minecraft:oak", "minecraft:spruce", "minecraft:birch", "minecraft:jungle", "minecraft:acacia", "minecraft:dark_oak"};
    maage_status result;
    size_t i;
    maage_block_definition callbacks;
    maage_tag_definition tag;
    maage_tag_id leaf_tags[3];
    maage_property_id leaf_properties[3];
    static const char *const tools[] = {"minecraft:pickaxe", "minecraft:shovel", "minecraft:axe", "minecraft:shears", "minecraft:hoe"};
    ids->traits = context->traits;
    ids->blocks = context->blocks;
    ids->items = context->items;
#define PROPERTY(field, name, kind, low, high) result = base_property(context, "minecraft:" name, kind, low, high, &ids->field); if (maage_status_failed(result)) return result
    PROPERTY(property_wood, "wood_type", MAAGE_PROPERTY_IDENTITY, 0, 0);
    PROPERTY(property_axis, "axis", MAAGE_PROPERTY_INTEGER, 0, 3);
    PROPERTY(property_persistent, "persistent", MAAGE_PROPERTY_BOOLEAN, 0, 1);
    PROPERTY(property_check_decay, "check_decay", MAAGE_PROPERTY_BOOLEAN, 0, 1);
    PROPERTY(property_stage, "stage", MAAGE_PROPERTY_INTEGER, 0, 1);
    PROPERTY(property_age, "age", MAAGE_PROPERTY_INTEGER, 0, 15);
    PROPERTY(property_crop_age, "crop_age", MAAGE_PROPERTY_INTEGER, 0, 7);
    PROPERTY(property_moisture, "moisture", MAAGE_PROPERTY_INTEGER, 0, 7);
    PROPERTY(property_facing, "facing", MAAGE_PROPERTY_INTEGER, 0, 3);
    PROPERTY(property_lit, "lit", MAAGE_PROPERTY_BOOLEAN, 0, 1);
    PROPERTY(property_color, "color", MAAGE_PROPERTY_INTEGER, 0, 15);
    PROPERTY(property_sand, "sand_type", MAAGE_PROPERTY_INTEGER, 0, 1);
    PROPERTY(property_attachment, "attachment", MAAGE_PROPERTY_INTEGER, 1, 5);
    PROPERTY(property_fluid_level, "fluid_level", MAAGE_PROPERTY_INTEGER, 0, 15);
    PROPERTY(property_grass_variant, "grass_variant", MAAGE_PROPERTY_INTEGER, 0, 6);
    PROPERTY(property_plant_variant, "plant_variant", MAAGE_PROPERTY_INTEGER, 0, 2);
#undef PROPERTY
    for (i = 0; i < 6; ++i) {
        result = maage_property_value_register(context->traits, ids->property_wood, woods[i], &ids->wood_values[i]);
        if (maage_status_failed(result)) return result;
    }
    memset(&tag, 0, sizeof(tag));
    tag.domains = MAAGE_TRAIT_ITEM;
    for (i = 1; i < 6; ++i) {
        result = maage_tag_register(context->traits, tools[i - 1], &tag);
        if (maage_status_failed(result)) return result;
        ids->tool_tags[i] = tag.id;
    }
    memset(&callbacks, 0, sizeof(callbacks));
    callbacks.behavior_context = ids;
    callbacks.id = MAAGE_CONTENT_ID_INVALID;
    callbacks.changed = maage_leaves_changed;
    callbacks.random_tick = maage_random_tick_leaves;
    callbacks.import_state = maage_base_leaf_import;
    leaf_properties[0] = ids->property_wood;
    leaf_properties[1] = ids->property_persistent;
    leaf_properties[2] = ids->property_check_decay;
    result = base_behavior_tag(context, "minecraft:leaf_neighbors", &callbacks, MAAGE_BEHAVIOR_CHANGED, NULL, 0, &leaf_tags[0]);
    if (!maage_status_failed(result)) result = base_behavior_tag(context, "minecraft:leaf_decay", &callbacks, MAAGE_BEHAVIOR_RANDOM_TICK, leaf_properties, 3, &leaf_tags[1]);
    if (!maage_status_failed(result)) result = base_behavior_tag(context, "minecraft:leaf_import", &callbacks, MAAGE_BEHAVIOR_IMPORT, leaf_properties, 3, &leaf_tags[2]);
    if (maage_status_failed(result)) return result;
    result = base_tag(context, "minecraft:wood", &ids->property_wood, 1, &ids->tag_wood);
    if (maage_status_failed(result)) return result;
    memset(&callbacks, 0, sizeof(callbacks));
    callbacks.behavior_context = ids;
    callbacks.drops = maage_base_drop_species;
    result = base_behavior_contract(context, "minecraft:wood_drops", &callbacks, &ids->property_wood, 1, &ids->tag_wood_drops);
    if (maage_status_failed(result)) return result;
    memset(&tag, 0, sizeof(tag));
    tag.domains = MAAGE_TRAIT_BLOCK | MAAGE_TRAIT_ITEM;
    tag.required = &ids->property_wood;
    tag.required_count = 1;
    tag.includes = leaf_tags;
    tag.include_count = 1;
    result = maage_tag_register(context->traits, "minecraft:log", &tag);
    ids->tag_log = tag.id;
    if (maage_status_failed(result)) return result;
    result = base_tag(context, "minecraft:planks", &ids->property_wood, 1, &ids->tag_planks);
    if (maage_status_failed(result)) return result;
    tag.domains = MAAGE_TRAIT_BLOCK;
    tag.includes = leaf_tags;
    tag.include_count = 3;
    result = maage_tag_register(context->traits, "minecraft:leaves", &tag);
    ids->tag_leaves = tag.id;
    if (!maage_status_failed(result)) result = base_tag(context, "minecraft:soil", NULL, 0, &ids->tag_soil);
    if (!maage_status_failed(result)) result = base_tag(context, "minecraft:sand", &ids->property_sand, 1, &ids->tag_sand);
    if (!maage_status_failed(result)) result = base_tag(context, "minecraft:axial", &ids->property_axis, 1, &ids->tag_axial);
    if (maage_status_failed(result)) return result;

    memset(&callbacks, 0, sizeof(callbacks));
    callbacks.behavior_context = ids;
    callbacks.random_tick = maage_random_tick_farmland;
    callbacks.survives = maage_support_farmland;
    callbacks.can_place = maage_base_support_place;
    callbacks.support_lost = maage_base_support_lost;
    callbacks.land = maage_base_farmland_land;
    result = base_behavior_contract(context, "minecraft:farmland", &callbacks, &ids->property_moisture, 1, &ids->tag_farmland);
    if (maage_status_failed(result)) return result;
    callbacks.random_tick = maage_random_tick_wheat;
    callbacks.survives = maage_support_wheat;
    callbacks.land = NULL;
    result = base_behavior_contract(context, "minecraft:crop", &callbacks, &ids->property_crop_age, 1, &ids->tag_crop);
    if (maage_status_failed(result)) return result;
    callbacks.random_tick = NULL;
    callbacks.survives = maage_support_plant;
    result = base_behavior_contract(context, "minecraft:soil_plant", &callbacks, NULL, 0, &ids->tag_soil_plant);
    if (maage_status_failed(result)) return result;
    callbacks.random_tick = maage_random_tick_mushroom;
    callbacks.survives = maage_support_mushroom;
    result = base_behavior_contract(context, "minecraft:mushroom", &callbacks, NULL, 0, &ids->tag_mushroom);
    if (maage_status_failed(result)) return result;
    callbacks.random_tick = maage_random_tick_cactus;
    callbacks.survives = maage_support_cactus;
    return base_behavior_contract(context, "minecraft:cactus", &callbacks, &ids->property_age, 1, &ids->tag_cactus);
}
maage_status maage_base_register_block(maage_block_registry *blocks, const char *name, maage_block_definition *block) {
    maage_traits_registry *traits = (maage_traits_registry *) maage_block_registry_traits(blocks);
    maage_tag_definition tag;
    maage_status result;
    const maage_base_ids *ids = (const maage_base_ids *) block->behavior_context;
    char identity[272];
    const char *path = strchr(name, ':');
    if (!path || strlen(name) + 6 >= sizeof(identity)) return maage_status_make(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_CONTEXT_REGISTRY, MAAGE_OPERATION_REGISTER, 0);
    if (!ids || block->required_tool > MAAGE_TOOL_HOE || block->special_tool_class > MAAGE_TOOL_HOE || block->tool_wear_class > MAAGE_TOOL_HOE) return maage_status_make(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_CONTEXT_REGISTRY, MAAGE_OPERATION_REGISTER, 0);
    block->has_required_tool = block->required_tool != MAAGE_TOOL_NONE;
    if (block->has_required_tool) block->required_tool = ids->tool_tags[block->required_tool];
    block->has_special_tool = block->special_tool_class != MAAGE_TOOL_NONE;
    if (block->has_special_tool) block->special_tool_class = ids->tool_tags[block->special_tool_class];
    block->has_tool_wear = block->tool_wear_class != MAAGE_TOOL_NONE;
    if (block->has_tool_wear) block->tool_wear_class = ids->tool_tags[block->tool_wear_class];
    result = maage_block_registry_register_block(blocks, name, block);
    if (maage_status_failed(result)) return result;
    sprintf(identity, "minecraft:block/%s", path + 1);
    memset(&tag, 0, sizeof(tag));
    tag.domains = MAAGE_TRAIT_BLOCK;
    result = maage_tag_register(traits, identity, &tag);
    if (!maage_status_failed(result)) result = maage_traits_assign(traits, MAAGE_TRAIT_BLOCK, block->id, &tag.id, 1, 0);
    return result;
}
maage_property_value maage_base_state_value(const maage_block_registry *blocks, const maage_block_state_definition *state, maage_property_id property) {
    maage_property_value value = MAAGE_CONTENT_ID_INVALID;
    maage_block_state_value(blocks, state, property, &value);
    return value;
}
int maage_base_state_tag(const maage_base_ids *ids, const maage_block_state_definition *state, maage_tag_id tag) {
    return ids && state && maage_traits_has(ids->traits, MAAGE_TRAIT_BLOCK, state->block_id, tag);
}

typedef struct base_behavior_provider {
    maage_block_definition callbacks;
    unsigned long event;
} base_behavior_provider;
static maage_status base_behavior_invoke(const void *context, void *payload) {
    const base_behavior_provider *provider = (const base_behavior_provider *) context;
    const maage_block_definition *block = &provider->callbacks;
    maage_block_behavior_call *call = (maage_block_behavior_call *) payload;
    maage_status result;
    int valid = 1;
    switch (provider->event) {
    case MAAGE_BEHAVIOR_RANDOM_TICK:
        return block->random_tick(call->world, call->x, call->y, call->z, call->state, call->random_tick);
    case MAAGE_BEHAVIOR_SCHEDULED_TICK:
        return block->scheduled_tick(call->world, call->x, call->y, call->z, call->state);
    case MAAGE_BEHAVIOR_SURVIVES:
        return block->survives(call->read_world, call->x, call->y, call->z, call->state, call->support);
    case MAAGE_BEHAVIOR_CAN_PLACE:
        result = block->can_place(call->read_world, call->x, call->y, call->z, call->state, &valid);
        *call->valid = *call->valid && valid;
        return result;
    case MAAGE_BEHAVIOR_DROPS:
        return block->drops(call->blocks, call->items, call->state, call->tool, call->random, call->drops);
    case MAAGE_BEHAVIOR_USE:
        return block->use(call->world, call->target, call->container);
    case MAAGE_BEHAVIOR_LAND:
        return block->land(call->world, call->x, call->y, call->z, call->state, call->distance, call->random);
    case MAAGE_BEHAVIOR_CHANGED:
        return block->changed(call->world, call->x, call->y, call->z, call->before, call->after);
    case MAAGE_BEHAVIOR_NEIGHBOR_CHANGED:
        return block->neighbor_changed(call->world, call->x, call->y, call->z, call->state, call->neighbor, call->deferred, call->continuation);
    case MAAGE_BEHAVIOR_SUPPORT_LOST:
        return block->support_lost(call->world, call->x, call->y, call->z, call->state);
    case MAAGE_BEHAVIOR_IMPORT:
        return block->import_state(call->blocks, call->state, call->replacement);
    default:
        return maage_status_make(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_CONTEXT_REGISTRY, MAAGE_OPERATION_UPDATE, 0);
    }
}
static int base_behavior_present(const maage_block_definition *block, unsigned long event) {
    switch (event) {
    case MAAGE_BEHAVIOR_RANDOM_TICK:
        return block->random_tick != NULL;
    case MAAGE_BEHAVIOR_SCHEDULED_TICK:
        return block->scheduled_tick != NULL;
    case MAAGE_BEHAVIOR_SURVIVES:
        return block->survives != NULL;
    case MAAGE_BEHAVIOR_CAN_PLACE:
        return block->can_place != NULL;
    case MAAGE_BEHAVIOR_DROPS:
        return block->drops != NULL;
    case MAAGE_BEHAVIOR_USE:
        return block->use != NULL;
    case MAAGE_BEHAVIOR_LAND:
        return block->land != NULL;
    case MAAGE_BEHAVIOR_CHANGED:
        return block->changed != NULL;
    case MAAGE_BEHAVIOR_NEIGHBOR_CHANGED:
        return block->neighbor_changed != NULL;
    case MAAGE_BEHAVIOR_SUPPORT_LOST:
        return block->support_lost != NULL;
    case MAAGE_BEHAVIOR_IMPORT:
        return block->import_state != NULL;
    default:
        return 0;
    }
}
static maage_status base_behavior_register(maage_content_context *context, const char *identity, const maage_block_definition *block, unsigned long event, maage_behavior_id *output) {
    base_behavior_provider *provider;
    maage_behavior_definition behavior;
    maage_status result;
    provider = (base_behavior_provider *) malloc(sizeof(*provider));
    if (!provider) return maage_status_make(MAAGE_STATUS_ALLOCATION_FAILED, MAAGE_CONTEXT_REGISTRY, MAAGE_OPERATION_REGISTER, 0);
    provider->callbacks = *block;
    provider->event = event;
    result = maage_content_own(context, provider, free);
    if (maage_status_failed(result)) {
        free(provider);
        return result;
    }
    memset(&behavior, 0, sizeof(behavior));
    behavior.event = event;
    behavior.domains = MAAGE_TRAIT_BLOCK;
    behavior.mode = event == MAAGE_BEHAVIOR_DROPS || event == MAAGE_BEHAVIOR_USE || event == MAAGE_BEHAVIOR_IMPORT || event == MAAGE_BEHAVIOR_SURVIVES || event == MAAGE_BEHAVIOR_SUPPORT_LOST ? MAAGE_BEHAVIOR_EXCLUSIVE : MAAGE_BEHAVIOR_COMPOSE;
    behavior.context = provider;
    behavior.callback = base_behavior_invoke;
    result = maage_behavior_register(context->traits, identity, &behavior);
    if (!maage_status_failed(result)) *output = behavior.id;
    return result;
}
static maage_status base_behavior_tag(maage_content_context *context, const char *identity, const maage_block_definition *block, unsigned long event, const maage_property_id *required, size_t required_count, maage_tag_id *output) {
    maage_behavior_id behavior;
    maage_tag_definition tag;
    maage_status result = base_behavior_register(context, identity, block, event, &behavior);
    if (maage_status_failed(result)) return result;
    memset(&tag, 0, sizeof(tag));
    tag.domains = MAAGE_TRAIT_BLOCK;
    tag.required = required;
    tag.required_count = required_count;
    tag.behaviors = &behavior;
    tag.behavior_count = 1;
    if (!required_count && block->id < maage_block_registry_block_count(context->blocks)) {
        const maage_property_set *configuration = NULL;
        const maage_block_state_definition *state;
        maage_property_id *properties;
        size_t i, j;
        for (i = 0; i < maage_block_registry_state_count(context->blocks); ++i) {
            state = maage_block_registry_state(context->blocks, (maage_block_state_id) i);
            if (state && state->block_id == block->id) {
                configuration = maage_property_set_get(context->traits, state->properties);
                break;
            }
        }
        properties = configuration && configuration->count ? (maage_property_id *) malloc(configuration->count * sizeof(*properties)) : NULL;
        if (configuration && configuration->count && !properties) return maage_status_make(MAAGE_STATUS_ALLOCATION_FAILED, MAAGE_CONTEXT_REGISTRY, MAAGE_OPERATION_REGISTER, 0);
        for (j = 0; configuration && j < configuration->count; ++j)
            properties[j] = configuration->values[j].property;
        tag.required = properties;
        tag.required_count = configuration ? configuration->count : 0;
        result = maage_tag_register(context->traits, identity, &tag);
        free(properties);
    } else result = maage_tag_register(context->traits, identity, &tag);
    if (!maage_status_failed(result)) *output = tag.id;
    return result;
}
static maage_status base_behavior_contract(maage_content_context *context, const char *identity, const maage_block_definition *block, const maage_property_id *required, size_t required_count, maage_tag_id *output) {
    static const char *const events[] = {"random_tick", "scheduled_tick", "survives", "can_place", "drops", "use", "land", "changed", "neighbor_changed", "support_lost", "import"};
    maage_behavior_id behaviors[MAAGE_BEHAVIOR_IMPORT];
    maage_tag_definition tag;
    maage_status result;
    unsigned long event;
    size_t count = 0;
    char name[320];
    if (strlen(identity) + 17 >= sizeof(name)) return maage_status_make(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_CONTEXT_REGISTRY, MAAGE_OPERATION_REGISTER, 0);
    for (event = MAAGE_BEHAVIOR_RANDOM_TICK; event <= MAAGE_BEHAVIOR_IMPORT; ++event) {
        if (!base_behavior_present(block, event)) continue;
        sprintf(name, "%s/%s", identity, events[event - 1]);
        result = base_behavior_register(context, name, block, event, &behaviors[count]);
        if (maage_status_failed(result)) return result;
        ++count;
    }
    memset(&tag, 0, sizeof(tag));
    tag.domains = MAAGE_TRAIT_BLOCK;
    tag.required = required;
    tag.required_count = required_count;
    tag.behaviors = behaviors;
    tag.behavior_count = count;
    result = maage_tag_register(context->traits, identity, &tag);
    if (!maage_status_failed(result)) *output = tag.id;
    return result;
}
maage_status maage_base_traits_bind(maage_content_context *context, const maage_base_ids *ids) {
    maage_block_definition block;
    maage_tag_id tags[16];
    maage_status result;
    size_t index, count;
    unsigned long event;
    int shared_support;
    static const char *const events[] = {"random_tick", "scheduled_tick", "survives", "can_place", "drops", "use", "land", "changed", "neighbor_changed", "support_lost", "import"};
    const char *name;
    char identity[320];
    for (index = 0; index < maage_block_registry_block_count(context->blocks); ++index) {
        block = *maage_block_registry_block(context->blocks, (maage_block_id) index);
        if (block.behavior_context != ids) continue;
        shared_support = block.survives == maage_support_wheat || block.survives == maage_support_farmland || block.survives == maage_support_plant || block.survives == maage_support_mushroom || block.survives == maage_support_cactus;
        if (block.survives) {
            block.support_lost = maage_base_support_lost;
            if (!block.falls && !block.can_place) block.can_place = maage_base_support_place;
        }
        name = maage_block_registry_block_name(context->blocks, block.id);
        sprintf(identity, "minecraft:block/%s", strchr(name, ':') + 1);
        result = maage_tag_find(context->traits, identity, &tags[0]);
        if (maage_status_failed(result)) return result;
        count = 1;
        if (block.id == ids->block_log || block.id == ids->block_log2) {
            tags[count++] = ids->tag_log;
            tags[count++] = ids->tag_wood;
            tags[count++] = ids->tag_axial;
        }
        if (block.id == ids->block_planks) {
            tags[count++] = ids->tag_planks;
            tags[count++] = ids->tag_wood;
        }
        if (block.id == ids->block_leaves) {
            tags[count++] = ids->tag_leaves;
        }
        if (block.id == ids->block_grass || block.id == ids->block_dirt) tags[count++] = ids->tag_soil;
        if (block.id == ids->block_sand) tags[count++] = ids->tag_sand;
        if (block.id == ids->block_farmland) tags[count++] = ids->tag_farmland;
        if (block.id == ids->block_wheat) tags[count++] = ids->tag_crop;
        if (block.survives == maage_support_plant) tags[count++] = ids->tag_soil_plant;
        if (block.random_tick == maage_random_tick_mushroom) tags[count++] = ids->tag_mushroom;
        if (block.random_tick == maage_random_tick_cactus) tags[count++] = ids->tag_cactus;
        if (block.drops == maage_base_drop_species) tags[count++] = ids->tag_wood_drops;
        for (event = MAAGE_BEHAVIOR_RANDOM_TICK; event <= MAAGE_BEHAVIOR_IMPORT; ++event) {
            if (!base_behavior_present(&block, event)) continue;
            if (event == MAAGE_BEHAVIOR_RANDOM_TICK && (block.random_tick == maage_random_tick_wheat || block.random_tick == maage_random_tick_farmland || block.random_tick == maage_random_tick_mushroom || block.random_tick == maage_random_tick_cactus)) continue;
            if (shared_support && (event == MAAGE_BEHAVIOR_SURVIVES || event == MAAGE_BEHAVIOR_CAN_PLACE || event == MAAGE_BEHAVIOR_SUPPORT_LOST)) continue;
            if (event == MAAGE_BEHAVIOR_LAND && block.land == maage_base_farmland_land) continue;
            if (event == MAAGE_BEHAVIOR_DROPS && block.drops == maage_base_drop_species) continue;
            if ((block.id == ids->block_log || block.id == ids->block_log2 || block.id == ids->block_leaves) && event == MAAGE_BEHAVIOR_CHANGED) continue;
            if (block.id == ids->block_leaves && (event == MAAGE_BEHAVIOR_RANDOM_TICK || event == MAAGE_BEHAVIOR_IMPORT)) continue;
            sprintf(identity, "minecraft:behavior/%s/%s", strchr(name, ':') + 1, events[event - 1]);
            result = base_behavior_tag(context, identity, &block, event, NULL, 0, &tags[count]);
            if (maage_status_failed(result)) return result;
            ++count;
        }
        result = maage_traits_assign(context->traits, MAAGE_TRAIT_BLOCK, block.id, tags, count, 0);
        if (maage_status_failed(result)) return result;
        block.random_tick = NULL;
        block.scheduled_tick = NULL;
        block.survives = NULL;
        block.can_place = NULL;
        block.drops = NULL;
        block.use = NULL;
        block.land = NULL;
        block.changed = NULL;
        block.import_state = NULL;
        block.neighbor_changed = NULL;
        block.support_lost = NULL;
        result = maage_block_registry_update_block(context->blocks, &block);
        if (maage_status_failed(result)) return result;
    }
    return maage_status_make(MAAGE_STATUS_SUCCESS, 0, MAAGE_OPERATION_REGISTER, 0);
}

maage_status maage_base_change_property(maage_world *world, int x, int y, int z, const maage_block_state_definition *state, maage_property_id property, maage_property_value value) {
    maage_block_state_id changed;
    maage_status result = maage_block_state_change(maage_world_registry(world), state, property, value, &changed);
    return maage_status_failed(result) ? result : maage_world_set(world, x, y, z, state->id, changed);
}
