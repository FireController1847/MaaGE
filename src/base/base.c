#include "base_internal.h"
#include "maage/gameplay.h"
#include "maage/session_protocol.h"
#include <stdlib.h>
#include <string.h>

const maage_base_ids *maage_base_generation_references(const maage_block_registry *registry) {
    const maage_registry_bindings *bindings = maage_block_registry_bindings(registry);
    return bindings ? (const maage_base_ids *) bindings->generation_context : NULL;
}
const maage_base_ids *maage_base_state_references(const maage_block_registry *registry, const maage_block_state_definition *state) {
    const maage_block_definition *block = state ? maage_block_registry_block(registry, state->block_id) : NULL;
    return block ? (const maage_base_ids *) block->behavior_context : NULL;
}

maage_status maage_base_bind(maage_content_context *context, maage_base_ids *ids) {
    maage_registry_bindings bindings;
    memset(&bindings, 0, sizeof(bindings));
    bindings.block_air = ids->block_air;
    bindings.state_air = ids->state_air;
    bindings.state_boundary = ids->state_bedrock;
    bindings.state_ui_background = ids->state_dirt;
    bindings.generation_context = ids;
    bindings.generate = maage_base_generate;
    bindings.spawn = maage_base_spawn;
    return maage_block_registry_bind(context->blocks, &bindings);
}
maage_status base_item_register(maage_content_context *context, const char *name, maage_item_definition *item, unsigned long fuel_ticks, maage_item_id *id) {
    maage_fuel_definition fuel;
    maage_status result;
    if (item->max_metadata) {
        if (item->has_block) item->icon_state = maage_base_item_icon;
        item->icon_texture = maage_base_item_texture;
        item->variant_name = maage_base_item_name;
    }
    item->texture_id = ((const maage_base_ids *) item->behavior_context)->textures[item->texture_id];
    result = maage_item_registry_register(context->items, name, item);
    if (maage_status_failed(result)) return result;
    *id = item->id;
    if (fuel_ticks) {
        memset(&fuel, 0, sizeof(fuel));
        fuel.item = item->id;
        fuel.metadata = MAAGE_SMELTING_METADATA_ANY;
        fuel.ticks = fuel_ticks;
        if (item->container_item) {
            fuel.remainder.item_id = item->container_item;
            fuel.remainder.count = 1;
        }
        result = maage_fuel_register(context->fuels, &fuel);
    }
    return result;
}
maage_status base_block_item(maage_content_context *context, const char *name, const char *label, maage_block_id block, maage_block_state_id state, unsigned int metadata, maage_item_placement placement, unsigned long fuel, maage_item_id *id) {
    maage_item_definition item;
    const maage_block_definition *type = maage_block_registry_block(context->blocks, block);
    const maage_block_state_definition *icon = maage_block_registry_state(context->blocks, state);
    if (!type || !icon) return maage_status_make(MAAGE_STATUS_NOT_FOUND, MAAGE_CONTEXT_REGISTRY, MAAGE_OPERATION_REGISTER, 0);
    memset(&item, 0, sizeof(item));
    item.behavior_context = context->module_data;
    item.has_block = 1;
    item.block_id = block;
    item.placement_state_id = item.icon_state_id = state;
    item.max_stack = 64;
    item.mining_speed = 1;
    item.texture_id = maage_base_logical_texture((const maage_base_ids *) item.behavior_context, icon->faces[MAAGE_FACE_POSITIVE_Z].texture_id);
    item.model = type->mesh_shape == MAAGE_BLOCK_MESH_CUBE ? MAAGE_ITEM_MODEL_BLOCK : MAAGE_ITEM_MODEL_SPRITE;
    item.max_metadata = metadata;
    item.placement = placement;
    item.display_name = label;
    return base_item_register(context, name, &item, fuel, id);
}
maage_status base_tool(maage_content_context *context, const char *name, const char *label, unsigned int durability, unsigned int tool_class, unsigned int level, double speed, unsigned int texture, unsigned long fuel, maage_item_id *id) {
    maage_item_definition item;
    memset(&item, 0, sizeof(item));
    item.behavior_context = context->module_data;
    item.max_stack = 1;
    item.durability = durability;
    item.tool_class = tool_class;
    item.tool_level = level;
    item.mining_speed = speed;
    item.model = MAAGE_ITEM_MODEL_SPRITE;
    item.texture_index = texture;
    item.texture_id = MAAGE_BASE_TEXTURE_ITEMS + texture;
    item.display_name = label;
    item.break_wear = tool_class == MAAGE_TOOL_HOE ? 0 : 1;
    item.use_durability = tool_class == MAAGE_TOOL_HOE;
    if (tool_class == MAAGE_TOOL_HOE) item.use_placement = maage_base_hoe_placement;
    return base_item_register(context, name, &item, fuel, id);
}
maage_status base_smelt(maage_smelting_registry *registry, maage_item_id input, unsigned int metadata, maage_item_id output, unsigned int output_metadata, double xp) {
    maage_smelting_recipe recipe;
    memset(&recipe, 0, sizeof(recipe));
    recipe.input = input;
    recipe.metadata = metadata;
    recipe.output.item_id = output;
    recipe.output.metadata = output_metadata;
    recipe.output.count = 1;
    recipe.experience = xp;
    return maage_smelting_register(registry, &recipe);
}
static maage_status maage_base_define(maage_content_context *context) {
    maage_base_ids *ids;
    maage_status result;
    ids = (maage_base_ids *) malloc(sizeof(*ids));
    if (!ids) return maage_status_make(MAAGE_STATUS_ALLOCATION_FAILED, MAAGE_CONTEXT_REGISTRY, MAAGE_OPERATION_REGISTER, 0);
    memset(ids, 255, sizeof(*ids));
    result = maage_content_own(context, ids, free);
    if (maage_status_failed(result)) {
        free(ids);
        return result;
    }
    context->module_data = ids;
    /* Definitions precede every module's internal binding stage. */
    result = maage_base_textures(context, ids);
    if (!maage_status_failed(result)) result = maage_base_sounds(context, ids);
    if (!maage_status_failed(result)) result = maage_base_bind(context, ids);
    if (!maage_status_failed(result)) result = maage_base_biomes(context->biomes, ids);
    if (!maage_status_failed(result)) result = maage_base_geometry(context, ids);
    if (!maage_status_failed(result)) result = maage_base_fluids_register(context, ids);
    if (!maage_status_failed(result)) result = maage_base_containers(context, ids);
    if (!maage_status_failed(result)) result = maage_base_blocks(context, ids);
    if (!maage_status_failed(result)) result = maage_base_items(context, ids);
    if (!maage_status_failed(result)) result = maage_base_creative(context, ids);
    return result;
}
static maage_status maage_base_bind_internal(maage_content_context *context) {
    maage_base_ids *ids = (maage_base_ids *) context->module_data;
    maage_biome_definition biome;
    size_t i;
    maage_status result = maage_base_bind(context, ids);
    if (!maage_status_failed(result)) result = maage_base_resolve_blocks(context, ids);
    if (!maage_status_failed(result)) result = maage_base_fluids_resolve(context, ids);
    if (!maage_status_failed(result)) result = maage_base_furnace_entity(context, ids);
    if (maage_status_failed(result)) return result;
    for (i = 0; i < MAAGE_BASE_BIOME_COUNT; ++i) {
        biome = *maage_biome_registry_get(context->biomes, ids->biome_ids[i]);
        biome.surface = ids->biome_grass[i];
        biome.filler = ids->state_dirt;
        if (biome.id == ids->biome_desert) biome.surface = biome.filler = ids->state_sand;
        if (biome.id == ids->biome_ocean) biome.surface = ids->state_gravel;
        result = maage_biome_registry_update(context->biomes, &biome);
        if (maage_status_failed(result)) return result;
    }
    result = maage_base_generation_register(context, ids);
    if (!maage_status_failed(result)) result = maage_base_recipes(context, ids);
    if (!maage_status_failed(result)) result = maage_base_smelting(context, ids);
    return result;
}
const maage_content_module maage_base_module = {
    "minecraft", NULL, 0,
    maage_base_define, maage_base_bind_internal, NULL, NULL,
    0, 0, NULL, NULL, NULL, NULL};
