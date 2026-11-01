#include "base_internal.h"
#include "maage/session_protocol.h"
#include <string.h>

static void base_block(maage_block_definition *block, int solid, int opaque, double hardness) {
    memset(block, 0, sizeof(*block));
    block->solid = block->material_solid = block->selectable = solid;
    block->opaque = opaque;
    block->hardness = hardness;
    block->light_opacity = opaque ? 15U : 0U;
    block->replaceable = !solid;
    block->flow_permeable = 0;
    block->item_drag = 0.588;
    block->unsupported_no_drops = 0;
    block->unsupported_state = MAAGE_CONTENT_ID_INVALID;
    block->selection_shape.maximum[0] = block->selection_shape.maximum[1] = block->selection_shape.maximum[2] = 1;
    if (solid) block->collision_shape = block->selection_shape;
}
maage_status maage_content_register_state(maage_block_registry *registry, const maage_content_config *config, maage_block_state_id *output, maage_block_id block, unsigned int metadata, int texture) {
    const maage_base_ids *ids = maage_base_references(registry);
    maage_block_state_definition state;
    const maage_block_definition *definition;
    int face, channel, selected, species, log_axis;
    maage_status result;
    memset(&state, 0, sizeof(state));
    state.id = (unsigned long) maage_block_registry_state_count(registry);
    state.block_id = block;
    state.metadata = metadata;
    state.geometry_key = metadata;
    state.geometry_grouped = block == ids->block_leaves || block == ids->block_sapling || block == ids->block_farmland;
    state.texture_axis = 0;
    (void) config;
    if (block == ids->block_leaves) state.geometry_key = metadata & 3U;
    if (block == ids->block_sapling) state.geometry_key = metadata & 7U;
    if (block == ids->block_farmland) state.geometry_key = metadata != 0;
    if (block == ids->block_lit_furnace) state.presentation_effect = 1;

    definition = maage_block_registry_block(registry, block);
    if (!definition) return maage_status_make(MAAGE_STATUS_INVALID_STATE, MAAGE_CONTEXT_REGISTRY, MAAGE_OPERATION_REGISTER, 0);
    state.random_ticks = definition->random_tick != NULL ||
                         block == ids->block_leaves || block == ids->block_lava || block == ids->block_torch ||
                         (block == ids->block_water && metadata != 0);
    if (block == ids->block_torch && metadata >= 1 && metadata <= 4) {
        state.selection_shape.minimum[0] = state.selection_shape.minimum[2] = 0.35;
        state.selection_shape.maximum[0] = state.selection_shape.maximum[2] = 0.65;
        state.selection_shape.minimum[1] = 0.2;
        state.selection_shape.maximum[1] = 0.8;
        log_axis = metadata <= 2 ? 0 : 2;
        state.selection_shape.minimum[log_axis] = metadata & 1 ? 0.0 : 0.7;
        state.selection_shape.maximum[log_axis] = metadata & 1 ? 0.3 : 1.0;
    }
    if (block == ids->block_water || block == ids->block_lava) {
        state.fluid_kind = block == ids->block_water ? MAAGE_FLUID_WATER : MAAGE_FLUID_LAVA;
        state.fluid_level = metadata;
    }
    for (face = 0; face < MAAGE_FACE_COUNT; ++face) {
        for (channel = 0; channel < 4; ++channel)
            state.faces[face].tint[channel] = 1.0f;
        selected = texture;
        if (block == ids->block_grass) {
            selected = MAAGE_BASE_TEXTURE_GRASS_SIDE;
            if (face == MAAGE_FACE_NEGATIVE_Y) selected = MAAGE_BASE_TEXTURE_DIRT;
            if (face == MAAGE_FACE_POSITIVE_Y) selected = MAAGE_BASE_TEXTURE_GRASS_TOP;
        }
        if (block == ids->block_farmland) selected = face == MAAGE_FACE_POSITIVE_Y ? MAAGE_BASE_TEXTURE_FARMLAND + (metadata ? 1 : 0) : MAAGE_BASE_TEXTURE_DIRT;
        if (block == ids->block_sandstone) {
            if (face == MAAGE_FACE_POSITIVE_Y) selected = MAAGE_BASE_TEXTURE_SANDSTONE_TOP;
            if (face == MAAGE_FACE_NEGATIVE_Y) selected = MAAGE_BASE_TEXTURE_SANDSTONE_BOTTOM;
        }
        if (block == ids->block_crafting_table) {
            selected = face == MAAGE_FACE_POSITIVE_Y ? MAAGE_BASE_TEXTURE_CRAFTING_TOP : (face == MAAGE_FACE_NEGATIVE_Y ? MAAGE_BASE_TEXTURE_EXTRA + 1 : ((face == MAAGE_FACE_NEGATIVE_X || face == MAAGE_FACE_NEGATIVE_Z) ? MAAGE_BASE_TEXTURE_CRAFTING_FRONT : MAAGE_BASE_TEXTURE_CRAFTING_SIDE));
        }
        if (block == ids->block_furnace || block == ids->block_lit_furnace) {
            static const unsigned int fronts[4] = {MAAGE_FACE_NEGATIVE_Z, MAAGE_FACE_POSITIVE_X, MAAGE_FACE_POSITIVE_Z, MAAGE_FACE_NEGATIVE_X};
            selected = MAAGE_BASE_TEXTURE_FURNACE;
            if (face == MAAGE_FACE_NEGATIVE_Y || face == MAAGE_FACE_POSITIVE_Y) selected += 1;
            else if ((unsigned int) face == fronts[metadata]) selected += block == ids->block_lit_furnace ? 3 : 2;
        }
        if (block == ids->block_sand && metadata == 1) selected = MAAGE_BASE_TEXTURE_FURNACE_VARIANTS;
        if (block == ids->block_planks && metadata) selected = metadata >= 3 ? MAAGE_BASE_TEXTURE_FURNACE_VARIANTS + 6 + (int) metadata - 3 : (metadata == 1 ? MAAGE_BASE_TEXTURE_SPRUCE_PLANKS : MAAGE_BASE_TEXTURE_BIRCH_PLANKS);
        if (block == ids->block_log) {
            species = metadata & 3;
            selected = species == 3 ? MAAGE_BASE_TEXTURE_FURNACE_VARIANTS + 1 : MAAGE_BASE_TEXTURE_EXTRA + (species == 1 ? 26 : (species == 2 ? 29 : 6));
            log_axis = (metadata & 12) == 4 ? 0 : ((metadata & 12) == 8 ? 2 : 1);
            if (face / 2 == log_axis) ++selected;
        } else if (block == ids->block_log2) {
            species = metadata & 1U;
            log_axis = (metadata & 12) == 4 ? 0 : ((metadata & 12) == 8 ? 2 : 1);
            selected = species ? MAAGE_BASE_TEXTURE_FURNACE_VARIANTS + 3 : MAAGE_BASE_TEXTURE_FURNACE_EXTRA + 6;
            if (face / 2 == log_axis) selected = MAAGE_BASE_TEXTURE_FURNACE_VARIANTS + (species ? 4 : 5);
        } else if (block == ids->block_leaves) {
            species = metadata & MAAGE_LEAF_SPECIES_MASK;
            selected = MAAGE_BASE_TEXTURE_EXTRA + (species == 1 ? 28 : (species == 2 ? 31 : 8));
        } else if (block == ids->block_cactus) {
            if (face == MAAGE_FACE_POSITIVE_Y) selected = MAAGE_BASE_TEXTURE_EXTRA + 23;
            if (face == MAAGE_FACE_NEGATIVE_Y) selected = MAAGE_BASE_TEXTURE_EXTRA + 24;
        }
        if (block == ids->block_grass) {
            if (face == MAAGE_FACE_POSITIVE_Y) state.faces[face].tint_mode = MAAGE_BLOCK_TINT_GRASS;
            else if (face != MAAGE_FACE_NEGATIVE_Y) {
                state.faces[face].overlay_texture = MAAGE_BASE_TEXTURE_GRASS_SIDE_OVERLAY;
                state.faces[face].snow_texture = MAAGE_BASE_TEXTURE_GRASS_SIDE_SNOW;
                state.faces[face].overlay_tint_mode = MAAGE_BLOCK_TINT_GRASS;
            }
        }
        if (block == ids->block_tall_grass && metadata) state.faces[face].tint_mode = MAAGE_BLOCK_TINT_GRASS_LOCAL;
        if (block == ids->block_leaves) {
            unsigned long color = (metadata & 3U) == 1 ? 6396257UL : 8431445UL;
            if (!(metadata & 3U)) state.faces[face].tint_mode = MAAGE_BLOCK_TINT_FOLIAGE;
            state.fast_texture = (metadata & 3U) == 1 ? MAAGE_BASE_TEXTURE_LEAVES_OPAQUE_SPRUCE : ((metadata & 3U) == 2 ? MAAGE_BASE_TEXTURE_LEAVES_OPAQUE_BIRCH : MAAGE_BASE_TEXTURE_LEAVES_OPAQUE_OAK);
            if (metadata & 3U)
                for (channel = 0; channel < 3; ++channel)
                    state.faces[face].tint[channel] = (float) ((color >> ((2 - channel) * 8)) & 255UL) / 255;
        }
        if (block == ids->block_log) state.texture_axis = (metadata & 12) == 4 ? 1 : ((metadata & 12) == 8 ? 3 : 2);
        state.faces[face].texture_id = ids->textures[selected];
        if (state.faces[face].overlay_texture) state.faces[face].overlay_texture = ids->textures[state.faces[face].overlay_texture];
        if (state.faces[face].snow_texture) state.faces[face].snow_texture = ids->textures[state.faces[face].snow_texture];
        state.faces[face].uv[2] = state.faces[face].uv[3] = 1.0f;
    }
    if (state.fast_texture) state.fast_texture = ids->textures[state.fast_texture];
    result = maage_block_registry_register_state(registry, &state);
    if (!maage_status_failed(result)) *output = state.id;
    return result;
}
maage_status maage_base_blocks(maage_content_context *context, maage_base_ids *ids) {
    maage_block_registry *registry = context->blocks;
    const maage_content_config *config = context->config;
    maage_block_definition block;
    maage_status result;
    int i, flags, species;

    base_block(&block, 0, 0, 0);
    block.behavior_context = ids;
    result = maage_block_registry_register_block(registry, "minecraft:air", &block);
    if (maage_status_failed(result)) return result;
    ids->block_air = block.id;
    result = maage_content_register_state(registry, config, &ids->state_air, block.id, 0, 0);
    if (maage_status_failed(result)) return result;
    result = maage_base_bind(context, ids);
    if (maage_status_failed(result)) return result;

    base_block(&block, 1, 1, 1.5);
    block.behavior_context = ids;
    block.drop_count = 1;
    block.required_tool = MAAGE_TOOL_PICKAXE;
    block.harvest_requires_tool = 1;
    result = maage_block_registry_register_block(registry, "minecraft:stone", &block);
    if (maage_status_failed(result)) return result;
    ids->block_stone = block.id;
    result = maage_content_register_state(registry, config, &ids->state_stone, block.id, 0, 0);
    if (maage_status_failed(result)) return result;

    base_block(&block, 1, 1, 0.6);
    block.behavior_context = ids;
    block.drop_count = 1;
    block.required_tool = MAAGE_TOOL_SHOVEL;
    block.random_tick = maage_random_tick_grass;
    result = maage_block_registry_register_block(registry, "minecraft:grass", &block);
    if (maage_status_failed(result)) return result;
    ids->block_grass = block.id;
    result = maage_content_register_state(registry, config, &ids->state_grass, block.id, 0, 2);
    if (maage_status_failed(result)) return result;

    base_block(&block, 1, 1, 0.5);
    block.behavior_context = ids;
    block.drop_count = 1;
    block.required_tool = MAAGE_TOOL_SHOVEL;
    result = maage_block_registry_register_block(registry, "minecraft:dirt", &block);
    if (maage_status_failed(result)) return result;
    ids->block_dirt = block.id;
    result = maage_content_register_state(registry, config, &ids->state_dirt, block.id, 0, 1);
    if (maage_status_failed(result)) return result;

    base_block(&block, 1, 1, -1);
    block.behavior_context = ids;
    result = maage_block_registry_register_block(registry, "minecraft:bedrock", &block);
    if (maage_status_failed(result)) return result;
    ids->block_bedrock = block.id;
    result = maage_content_register_state(registry, config, &ids->state_bedrock, block.id, 0, 4);
    if (maage_status_failed(result)) return result;

    base_block(&block, 0, 0, 100);
    block.behavior_context = ids;
    block.light_opacity = 3;
    result = maage_block_registry_register_block(registry, "minecraft:water", &block);
    if (maage_status_failed(result)) return result;
    ids->block_water = block.id;
    result = maage_content_register_state(registry, config, &ids->state_water, block.id, 0, 5);
    if (maage_status_failed(result)) return result;

    base_block(&block, 1, 1, 0.5);
    block.behavior_context = ids;
    block.drop_count = 1;
    block.required_tool = MAAGE_TOOL_SHOVEL;
    block.falls = 1;
    result = maage_block_registry_register_block(registry, "minecraft:sand", &block);
    if (maage_status_failed(result)) return result;
    ids->block_sand = block.id;
    result = maage_content_register_state(registry, config, &ids->state_sand, block.id, 0, 6);
    if (maage_status_failed(result)) return result;

    base_block(&block, 1, 1, 0.6);
    block.behavior_context = ids;
    block.drop_count = 1;
    block.required_tool = MAAGE_TOOL_SHOVEL;
    block.falls = 1;
    result = maage_block_registry_register_block(registry, "minecraft:gravel", &block);
    if (maage_status_failed(result)) return result;
    ids->block_gravel = block.id;
    result = maage_content_register_state(registry, config, &ids->state_gravel, block.id, 0, 7);
    if (maage_status_failed(result)) return result;

    base_block(&block, 1, 1, 0.8);
    block.behavior_context = ids;
    block.drop_count = 1;
    block.required_tool = MAAGE_TOOL_PICKAXE;
    block.harvest_requires_tool = 1;
    result = maage_block_registry_register_block(registry, "minecraft:sandstone", &block);
    if (maage_status_failed(result)) return result;
    ids->block_sandstone = block.id;
    result = maage_content_register_state(registry, config, &ids->state_sandstone, block.id, 0, 8);
    if (maage_status_failed(result)) return result;

    base_block(&block, 1, 1, 2);
    block.behavior_context = ids;
    block.drop_count = 1;
    block.selectable = 1;
    block.emitted_light = 0;
    block.use_neighbor_brightness = 0;
    block.required_tool = MAAGE_TOOL_PICKAXE;
    block.harvest_requires_tool = 1;
    block.required_tool_level = 0;
    result = maage_block_registry_register_block(registry, "minecraft:cobblestone", &block);
    if (maage_status_failed(result)) return result;
    ids->block_cobblestone = block.id;
    result = maage_content_register_state(registry, config, &ids->state_cobblestone, block.id, 0, MAAGE_BASE_TEXTURE_EXTRA + 0);
    if (maage_status_failed(result)) return result;

    base_block(&block, 1, 1, 2);
    block.behavior_context = ids;
    block.drop_count = 1;
    block.selectable = 1;
    block.emitted_light = 0;
    block.use_neighbor_brightness = 0;
    block.required_tool = MAAGE_TOOL_AXE;
    result = maage_block_registry_register_block(registry, "minecraft:planks", &block);
    if (maage_status_failed(result)) return result;
    ids->block_planks = block.id;
    result = maage_content_register_state(registry, config, &ids->state_planks, block.id, 0, MAAGE_BASE_TEXTURE_EXTRA + 1);
    if (maage_status_failed(result)) return result;

    base_block(&block, 1, 1, 2.5);
    block.behavior_context = ids;
    block.drop_count = 1;
    block.selectable = 1;
    block.emitted_light = 0;
    block.use_neighbor_brightness = 0;
    block.required_tool = MAAGE_TOOL_AXE;
    result = maage_block_registry_register_block(registry, "minecraft:crafting_table", &block);
    if (maage_status_failed(result)) return result;
    ids->block_crafting_table = block.id;
    result = maage_content_register_state(registry, config, &ids->state_crafting_table, block.id, 0, MAAGE_BASE_TEXTURE_EXTRA + 0);
    if (maage_status_failed(result)) return result;

    base_block(&block, 1, 1, 3);
    block.behavior_context = ids;
    block.drop_count = 1;
    block.selectable = 1;
    block.emitted_light = 0;
    block.use_neighbor_brightness = 0;
    block.required_tool = MAAGE_TOOL_PICKAXE;
    block.harvest_requires_tool = 1;
    block.required_tool_level = 2;
    result = maage_block_registry_register_block(registry, "minecraft:gold_block", &block);
    if (maage_status_failed(result)) return result;
    ids->block_gold_block = block.id;
    result = maage_content_register_state(registry, config, &ids->state_gold_block, block.id, 0, MAAGE_BASE_TEXTURE_EXTRA + MAAGE_BASE_TEXTURE_STORAGE - MAAGE_BASE_TEXTURE_EXTRA);
    if (maage_status_failed(result)) return result;

    base_block(&block, 1, 1, 5);
    block.behavior_context = ids;
    block.drop_count = 1;
    block.selectable = 1;
    block.emitted_light = 0;
    block.use_neighbor_brightness = 0;
    block.required_tool = MAAGE_TOOL_PICKAXE;
    block.harvest_requires_tool = 1;
    block.required_tool_level = 1;
    result = maage_block_registry_register_block(registry, "minecraft:iron_block", &block);
    if (maage_status_failed(result)) return result;
    ids->block_iron_block = block.id;
    result = maage_content_register_state(registry, config, &ids->state_iron_block, block.id, 0, MAAGE_BASE_TEXTURE_EXTRA + MAAGE_BASE_TEXTURE_STORAGE + 1 - MAAGE_BASE_TEXTURE_EXTRA);
    if (maage_status_failed(result)) return result;

    base_block(&block, 1, 1, 5);
    block.behavior_context = ids;
    block.drop_count = 1;
    block.selectable = 1;
    block.emitted_light = 0;
    block.use_neighbor_brightness = 0;
    block.required_tool = MAAGE_TOOL_PICKAXE;
    block.harvest_requires_tool = 1;
    block.required_tool_level = 2;
    result = maage_block_registry_register_block(registry, "minecraft:diamond_block", &block);
    if (maage_status_failed(result)) return result;
    ids->block_diamond_block = block.id;
    result = maage_content_register_state(registry, config, &ids->state_diamond_block, block.id, 0, MAAGE_BASE_TEXTURE_EXTRA + MAAGE_BASE_TEXTURE_STORAGE + 2 - MAAGE_BASE_TEXTURE_EXTRA);
    if (maage_status_failed(result)) return result;

    base_block(&block, 1, 1, 3);
    block.behavior_context = ids;
    block.drop_count = 1;
    block.selectable = 1;
    block.emitted_light = 0;
    block.use_neighbor_brightness = 0;
    block.required_tool = MAAGE_TOOL_PICKAXE;
    block.harvest_requires_tool = 1;
    block.required_tool_level = 1;
    result = maage_block_registry_register_block(registry, "minecraft:lapis_block", &block);
    if (maage_status_failed(result)) return result;
    ids->block_lapis_block = block.id;
    result = maage_content_register_state(registry, config, &ids->state_lapis_block, block.id, 0, MAAGE_BASE_TEXTURE_EXTRA + MAAGE_BASE_TEXTURE_STORAGE + 3 - MAAGE_BASE_TEXTURE_EXTRA);
    if (maage_status_failed(result)) return result;

    base_block(&block, 1, 1, 5);
    block.behavior_context = ids;
    block.drop_count = 1;
    block.selectable = 1;
    block.emitted_light = 0;
    block.use_neighbor_brightness = 0;
    block.required_tool = MAAGE_TOOL_PICKAXE;
    block.harvest_requires_tool = 1;
    block.required_tool_level = 2;
    result = maage_block_registry_register_block(registry, "minecraft:emerald_block", &block);
    if (maage_status_failed(result)) return result;
    ids->block_emerald_block = block.id;
    result = maage_content_register_state(registry, config, &ids->state_emerald_block, block.id, 0, MAAGE_BASE_TEXTURE_EXTRA + MAAGE_BASE_TEXTURE_STORAGE + 4 - MAAGE_BASE_TEXTURE_EXTRA);
    if (maage_status_failed(result)) return result;

    base_block(&block, 1, 1, 5);
    block.behavior_context = ids;
    block.drop_count = 1;
    block.selectable = 1;
    block.emitted_light = 0;
    block.use_neighbor_brightness = 0;
    block.required_tool = MAAGE_TOOL_PICKAXE;
    block.harvest_requires_tool = 1;
    block.required_tool_level = 0;
    result = maage_block_registry_register_block(registry, "minecraft:redstone_block", &block);
    if (maage_status_failed(result)) return result;
    ids->block_redstone_block = block.id;
    result = maage_content_register_state(registry, config, &ids->state_redstone_block, block.id, 0, MAAGE_BASE_TEXTURE_EXTRA + MAAGE_BASE_TEXTURE_STORAGE + 5 - MAAGE_BASE_TEXTURE_EXTRA);
    if (maage_status_failed(result)) return result;

    base_block(&block, 1, 1, 5);
    block.behavior_context = ids;
    block.drop_count = 1;
    block.selectable = 1;
    block.emitted_light = 0;
    block.use_neighbor_brightness = 0;
    block.required_tool = MAAGE_TOOL_PICKAXE;
    block.harvest_requires_tool = 1;
    block.required_tool_level = 0;
    result = maage_block_registry_register_block(registry, "minecraft:coal_block", &block);
    if (maage_status_failed(result)) return result;
    ids->block_coal_block = block.id;
    result = maage_content_register_state(registry, config, &ids->state_coal_block, block.id, 0, MAAGE_BASE_TEXTURE_EXTRA + MAAGE_BASE_TEXTURE_STORAGE + 6 - MAAGE_BASE_TEXTURE_EXTRA);
    if (maage_status_failed(result)) return result;

    base_block(&block, 1, 1, 0.2);
    block.behavior_context = ids;
    block.drop_count = 4;
    block.selectable = 1;
    block.emitted_light = 0;
    block.use_neighbor_brightness = 0;
    block.required_tool = MAAGE_TOOL_SHOVEL;
    block.harvest_requires_tool = 1;
    block.required_tool_level = 0;
    result = maage_block_registry_register_block(registry, "minecraft:snow_block", &block);
    if (maage_status_failed(result)) return result;
    ids->block_snow_block = block.id;
    result = maage_content_register_state(registry, config, &ids->state_snow_block, block.id, 0, MAAGE_BASE_TEXTURE_EXTRA + MAAGE_BASE_TEXTURE_STORAGE + 7 - MAAGE_BASE_TEXTURE_EXTRA);
    if (maage_status_failed(result)) return result;

    base_block(&block, 0, 0, 100);
    block.behavior_context = ids;
    block.drop_count = 0;
    block.selectable = 0;
    block.emitted_light = 15;
    block.use_neighbor_brightness = 1;
    result = maage_block_registry_register_block(registry, "minecraft:lava", &block);
    if (maage_status_failed(result)) return result;
    ids->block_lava = block.id;
    result = maage_content_register_state(registry, config, &ids->state_lava, block.id, 0, MAAGE_BASE_TEXTURE_EXTRA + 2);
    if (maage_status_failed(result)) return result;

    base_block(&block, 1, 1, 3);
    block.behavior_context = ids;
    block.drop_count = 1;
    block.selectable = 1;
    block.emitted_light = 0;
    block.use_neighbor_brightness = 0;
    block.required_tool = MAAGE_TOOL_PICKAXE;
    block.harvest_requires_tool = 1;
    block.required_tool_level = 2;
    result = maage_block_registry_register_block(registry, "minecraft:gold_ore", &block);
    if (maage_status_failed(result)) return result;
    ids->block_gold_ore = block.id;
    result = maage_content_register_state(registry, config, &ids->state_gold_ore, block.id, 0, MAAGE_BASE_TEXTURE_EXTRA + 3);
    if (maage_status_failed(result)) return result;

    base_block(&block, 1, 1, 3);
    block.behavior_context = ids;
    block.drop_count = 1;
    block.selectable = 1;
    block.emitted_light = 0;
    block.use_neighbor_brightness = 0;
    block.required_tool = MAAGE_TOOL_PICKAXE;
    block.harvest_requires_tool = 1;
    block.required_tool_level = 1;
    result = maage_block_registry_register_block(registry, "minecraft:iron_ore", &block);
    if (maage_status_failed(result)) return result;
    ids->block_iron_ore = block.id;
    result = maage_content_register_state(registry, config, &ids->state_iron_ore, block.id, 0, MAAGE_BASE_TEXTURE_EXTRA + 4);
    if (maage_status_failed(result)) return result;

    base_block(&block, 1, 1, 3);
    block.behavior_context = ids;
    block.drop_count = 1;
    block.selectable = 1;
    block.emitted_light = 0;
    block.use_neighbor_brightness = 0;
    block.required_tool = MAAGE_TOOL_PICKAXE;
    block.harvest_requires_tool = 1;
    block.required_tool_level = 0;
    result = maage_block_registry_register_block(registry, "minecraft:coal_ore", &block);
    if (maage_status_failed(result)) return result;
    ids->block_coal_ore = block.id;
    result = maage_content_register_state(registry, config, &ids->state_coal_ore, block.id, 0, MAAGE_BASE_TEXTURE_EXTRA + 5);
    if (maage_status_failed(result)) return result;

    base_block(&block, 1, 1, 2);
    block.behavior_context = ids;
    block.drop_count = 1;
    block.selectable = 1;
    block.emitted_light = 0;
    block.use_neighbor_brightness = 0;
    block.required_tool = MAAGE_TOOL_AXE;
    result = maage_block_registry_register_block(registry, "minecraft:log", &block);
    if (maage_status_failed(result)) return result;
    ids->block_log = block.id;
    result = maage_content_register_state(registry, config, &ids->state_log, block.id, 0, MAAGE_BASE_TEXTURE_EXTRA + 6);
    if (maage_status_failed(result)) return result;

    base_block(&block, 1, 0, 0.2);
    block.behavior_context = ids;
    block.drop_count = 0;
    block.selectable = 1;
    block.emitted_light = 0;
    block.use_neighbor_brightness = 0;
    block.light_opacity = 1;
    block.random_tick = maage_random_tick_leaves;
    result = maage_block_registry_register_block(registry, "minecraft:leaves", &block);
    if (maage_status_failed(result)) return result;
    ids->block_leaves = block.id;
    result = maage_content_register_state(registry, config, &ids->state_leaves, block.id, 0, MAAGE_BASE_TEXTURE_EXTRA + 8);
    if (maage_status_failed(result)) return result;

    base_block(&block, 1, 1, 3);
    block.behavior_context = ids;
    block.drop_count = 1;
    block.selectable = 1;
    block.emitted_light = 0;
    block.use_neighbor_brightness = 0;
    block.required_tool = MAAGE_TOOL_PICKAXE;
    block.harvest_requires_tool = 1;
    block.required_tool_level = 1;
    result = maage_block_registry_register_block(registry, "minecraft:lapis_ore", &block);
    if (maage_status_failed(result)) return result;
    ids->block_lapis_ore = block.id;
    result = maage_content_register_state(registry, config, &ids->state_lapis_ore, block.id, 0, MAAGE_BASE_TEXTURE_EXTRA + 9);
    if (maage_status_failed(result)) return result;

    base_block(&block, 0, 0, 0);
    block.behavior_context = ids;
    block.drop_count = 1;
    block.selectable = 1;
    block.emitted_light = 0;
    block.use_neighbor_brightness = 1;
    block.random_tick = maage_random_tick_bush;
    block.survives = maage_support_plant;
    block.mesh_shape = MAAGE_BLOCK_MESH_CROSS;
    block.required_tool = MAAGE_TOOL_AXE;
    block.selection_shape.minimum[0] = 0.1;
    block.selection_shape.minimum[2] = 0.1;
    block.selection_shape.maximum[0] = 0.9;
    block.selection_shape.maximum[2] = 0.9;
    block.selection_shape.maximum[1] = 0.8;
    result = maage_block_registry_register_block(registry, "minecraft:tall_grass", &block);
    if (maage_status_failed(result)) return result;
    ids->block_tall_grass = block.id;
    result = maage_content_register_state(registry, config, &ids->state_tall_grass, block.id, 1, MAAGE_BASE_TEXTURE_EXTRA + 10);
    if (maage_status_failed(result)) return result;

    base_block(&block, 0, 0, 0);
    block.behavior_context = ids;
    block.drop_count = 1;
    block.selectable = 1;
    block.emitted_light = 0;
    block.use_neighbor_brightness = 1;
    block.random_tick = maage_random_tick_bush;
    block.survives = maage_support_dead_bush;
    block.mesh_shape = MAAGE_BLOCK_MESH_CROSS;
    block.required_tool = MAAGE_TOOL_AXE;
    block.selection_shape.minimum[0] = 0.1;
    block.selection_shape.minimum[2] = 0.1;
    block.selection_shape.maximum[0] = 0.9;
    block.selection_shape.maximum[2] = 0.9;
    block.selection_shape.maximum[1] = 0.8;
    result = maage_block_registry_register_block(registry, "minecraft:dead_bush", &block);
    if (maage_status_failed(result)) return result;
    ids->block_dead_bush = block.id;
    result = maage_content_register_state(registry, config, &ids->state_dead_bush, block.id, 0, MAAGE_BASE_TEXTURE_EXTRA + 11);
    if (maage_status_failed(result)) return result;

    base_block(&block, 0, 0, 0);
    block.behavior_context = ids;
    block.drop_count = 1;
    block.selectable = 1;
    block.emitted_light = 0;
    block.use_neighbor_brightness = 1;
    block.random_tick = maage_random_tick_bush;
    block.survives = maage_support_plant;
    block.mesh_shape = MAAGE_BLOCK_MESH_CROSS;
    block.required_tool = MAAGE_TOOL_AXE;
    block.selection_shape.minimum[0] = 0.3;
    block.selection_shape.minimum[2] = 0.3;
    block.selection_shape.maximum[0] = 0.7;
    block.selection_shape.maximum[2] = 0.7;
    block.selection_shape.maximum[1] = 0.6;
    result = maage_block_registry_register_block(registry, "minecraft:dandelion", &block);
    if (maage_status_failed(result)) return result;
    ids->block_dandelion = block.id;
    result = maage_content_register_state(registry, config, &ids->state_dandelion, block.id, 0, MAAGE_BASE_TEXTURE_EXTRA + 12);
    if (maage_status_failed(result)) return result;

    base_block(&block, 0, 0, 0);
    block.behavior_context = ids;
    block.drop_count = 1;
    block.selectable = 1;
    block.emitted_light = 0;
    block.use_neighbor_brightness = 1;
    block.random_tick = maage_random_tick_bush;
    block.survives = maage_support_plant;
    block.mesh_shape = MAAGE_BLOCK_MESH_CROSS;
    block.required_tool = MAAGE_TOOL_AXE;
    block.selection_shape.minimum[0] = 0.3;
    block.selection_shape.minimum[2] = 0.3;
    block.selection_shape.maximum[0] = 0.7;
    block.selection_shape.maximum[2] = 0.7;
    block.selection_shape.maximum[1] = 0.6;
    result = maage_block_registry_register_block(registry, "minecraft:poppy", &block);
    if (maage_status_failed(result)) return result;
    ids->block_poppy = block.id;
    result = maage_content_register_state(registry, config, &ids->state_poppy, block.id, 0, MAAGE_BASE_TEXTURE_EXTRA + 13);
    if (maage_status_failed(result)) return result;

    base_block(&block, 0, 0, 0);
    block.behavior_context = ids;
    block.drop_count = 1;
    block.selectable = 1;
    block.emitted_light = 0;
    block.use_neighbor_brightness = 1;
    block.random_tick = maage_random_tick_mushroom;
    block.survives = maage_support_mushroom;
    block.mesh_shape = MAAGE_BLOCK_MESH_CROSS;
    block.required_tool = MAAGE_TOOL_AXE;
    block.selection_shape.minimum[0] = 0.3;
    block.selection_shape.minimum[2] = 0.3;
    block.selection_shape.maximum[0] = 0.7;
    block.selection_shape.maximum[2] = 0.7;
    block.selection_shape.maximum[1] = 0.4;
    result = maage_block_registry_register_block(registry, "minecraft:brown_mushroom", &block);
    if (maage_status_failed(result)) return result;
    ids->block_brown_mushroom = block.id;
    result = maage_content_register_state(registry, config, &ids->state_brown_mushroom, block.id, 0, MAAGE_BASE_TEXTURE_EXTRA + 14);
    if (maage_status_failed(result)) return result;

    base_block(&block, 0, 0, 0);
    block.behavior_context = ids;
    block.drop_count = 1;
    block.selectable = 1;
    block.emitted_light = 0;
    block.use_neighbor_brightness = 1;
    block.random_tick = maage_random_tick_mushroom;
    block.survives = maage_support_mushroom;
    block.mesh_shape = MAAGE_BLOCK_MESH_CROSS;
    block.required_tool = MAAGE_TOOL_AXE;
    block.selection_shape.minimum[0] = 0.3;
    block.selection_shape.minimum[2] = 0.3;
    block.selection_shape.maximum[0] = 0.7;
    block.selection_shape.maximum[2] = 0.7;
    block.selection_shape.maximum[1] = 0.4;
    result = maage_block_registry_register_block(registry, "minecraft:red_mushroom", &block);
    if (maage_status_failed(result)) return result;
    ids->block_red_mushroom = block.id;
    result = maage_content_register_state(registry, config, &ids->state_red_mushroom, block.id, 0, MAAGE_BASE_TEXTURE_EXTRA + 15);
    if (maage_status_failed(result)) return result;

    base_block(&block, 1, 1, 2);
    block.behavior_context = ids;
    block.drop_count = 1;
    block.selectable = 1;
    block.emitted_light = 0;
    block.use_neighbor_brightness = 0;
    block.required_tool = MAAGE_TOOL_PICKAXE;
    block.harvest_requires_tool = 1;
    block.required_tool_level = 0;
    result = maage_block_registry_register_block(registry, "minecraft:mossy_cobblestone", &block);
    if (maage_status_failed(result)) return result;
    ids->block_mossy_cobblestone = block.id;
    result = maage_content_register_state(registry, config, &ids->state_mossy_cobblestone, block.id, 0, MAAGE_BASE_TEXTURE_EXTRA + 16);
    if (maage_status_failed(result)) return result;

    base_block(&block, 1, 1, 50);
    block.behavior_context = ids;
    block.drop_count = 1;
    block.selectable = 1;
    block.emitted_light = 0;
    block.use_neighbor_brightness = 0;
    block.required_tool = MAAGE_TOOL_PICKAXE;
    block.harvest_requires_tool = 1;
    block.required_tool_level = 3;
    result = maage_block_registry_register_block(registry, "minecraft:obsidian", &block);
    if (maage_status_failed(result)) return result;
    ids->block_obsidian = block.id;
    result = maage_content_register_state(registry, config, &ids->state_obsidian, block.id, 0, MAAGE_BASE_TEXTURE_EXTRA + 17);
    if (maage_status_failed(result)) return result;

    base_block(&block, 0, 0, 0);
    block.behavior_context = ids;
    block.drop_count = 1;
    block.selectable = 1;
    block.emitted_light = 14;
    block.use_neighbor_brightness = 1;
    block.survives = maage_support_torch;
    block.mesh_shape = MAAGE_BLOCK_MESH_TORCH;
    block.selection_shape.minimum[0] = 0.4;
    block.selection_shape.minimum[2] = 0.4;
    block.selection_shape.maximum[0] = 0.6;
    block.selection_shape.maximum[2] = 0.6;
    block.selection_shape.maximum[1] = 0.6;
    result = maage_block_registry_register_block(registry, "minecraft:torch", &block);
    if (maage_status_failed(result)) return result;
    ids->block_torch = block.id;
    result = maage_content_register_state(registry, config, &ids->state_torch, block.id, 5, MAAGE_BASE_TEXTURE_EXTRA + 18);
    if (maage_status_failed(result)) return result;

    base_block(&block, 1, 1, 3);
    block.behavior_context = ids;
    block.drop_count = 1;
    block.selectable = 1;
    block.emitted_light = 0;
    block.use_neighbor_brightness = 0;
    block.required_tool = MAAGE_TOOL_PICKAXE;
    block.harvest_requires_tool = 1;
    block.required_tool_level = 2;
    result = maage_block_registry_register_block(registry, "minecraft:diamond_ore", &block);
    if (maage_status_failed(result)) return result;
    ids->block_diamond_ore = block.id;
    result = maage_content_register_state(registry, config, &ids->state_diamond_ore, block.id, 0, MAAGE_BASE_TEXTURE_EXTRA + 19);
    if (maage_status_failed(result)) return result;

    base_block(&block, 1, 1, 3);
    block.behavior_context = ids;
    block.drop_count = 1;
    block.selectable = 1;
    block.emitted_light = 0;
    block.use_neighbor_brightness = 0;
    block.required_tool = MAAGE_TOOL_PICKAXE;
    block.harvest_requires_tool = 1;
    block.required_tool_level = 2;
    result = maage_block_registry_register_block(registry, "minecraft:redstone_ore", &block);
    if (maage_status_failed(result)) return result;
    ids->block_redstone_ore = block.id;
    result = maage_content_register_state(registry, config, &ids->state_redstone_ore, block.id, 0, MAAGE_BASE_TEXTURE_EXTRA + 20);
    if (maage_status_failed(result)) return result;

    base_block(&block, 1, 0, 0.1);
    block.behavior_context = ids;
    block.drop_count = 1;
    block.selectable = 1;
    block.emitted_light = 0;
    block.use_neighbor_brightness = 1;
    block.random_tick = maage_random_tick_snow;
    block.survives = maage_support_snow;
    block.required_tool = MAAGE_TOOL_SHOVEL;
    block.harvest_requires_tool = 1;
    block.solid = 0;
    block.material_solid = 0;
    block.replaceable = 1;
    block.selection_shape.maximum[1] = 0.125;
    block.collision_shape.maximum[0] = 0;
    block.collision_shape.maximum[1] = 0;
    block.collision_shape.maximum[2] = 0;
    result = maage_block_registry_register_block(registry, "minecraft:snow", &block);
    if (maage_status_failed(result)) return result;
    ids->block_snow = block.id;
    result = maage_content_register_state(registry, config, &ids->state_snow, block.id, 0, MAAGE_BASE_TEXTURE_EXTRA + 21);
    if (maage_status_failed(result)) return result;

    base_block(&block, 1, 0, 0.5);
    block.behavior_context = ids;
    block.drop_count = 1;
    block.selectable = 1;
    block.emitted_light = 0;
    block.use_neighbor_brightness = 0;
    block.light_opacity = 3;
    block.random_tick = maage_random_tick_ice;
    block.required_tool = MAAGE_TOOL_PICKAXE;
    result = maage_block_registry_register_block(registry, "minecraft:ice", &block);
    if (maage_status_failed(result)) return result;
    ids->block_ice = block.id;
    result = maage_content_register_state(registry, config, &ids->state_ice, block.id, 0, MAAGE_BASE_TEXTURE_EXTRA + 32);
    if (maage_status_failed(result)) return result;

    base_block(&block, 1, 0, 0.4);
    block.behavior_context = ids;
    block.drop_count = 1;
    block.selectable = 1;
    block.emitted_light = 0;
    block.use_neighbor_brightness = 1;
    block.random_tick = maage_random_tick_cactus;
    block.survives = maage_support_cactus;
    block.contact_damage = 1;
    block.contact_shape.maximum[0] = 1;
    block.contact_shape.maximum[1] = 1;
    block.contact_shape.maximum[2] = 1;
    block.collision_shape.minimum[0] = 0.0625;
    block.collision_shape.minimum[2] = 0.0625;
    block.collision_shape.maximum[0] = 0.9375;
    block.collision_shape.maximum[2] = 0.9375;
    block.selection_shape = block.collision_shape;
    block.collision_shape.maximum[1] = 0.9375;
    result = maage_block_registry_register_block(registry, "minecraft:cactus", &block);
    if (maage_status_failed(result)) return result;
    ids->block_cactus = block.id;
    result = maage_content_register_state(registry, config, &ids->state_cactus, block.id, 0, MAAGE_BASE_TEXTURE_EXTRA + 22);
    if (maage_status_failed(result)) return result;

    base_block(&block, 1, 1, 3);
    block.behavior_context = ids;
    block.drop_count = 1;
    block.selectable = 1;
    block.emitted_light = 0;
    block.use_neighbor_brightness = 0;
    block.required_tool = MAAGE_TOOL_PICKAXE;
    block.harvest_requires_tool = 1;
    block.required_tool_level = 2;
    result = maage_block_registry_register_block(registry, "minecraft:emerald_ore", &block);
    if (maage_status_failed(result)) return result;
    ids->block_emerald_ore = block.id;
    result = maage_content_register_state(registry, config, &ids->state_emerald_ore, block.id, 0, MAAGE_BASE_TEXTURE_EXTRA + 25);
    if (maage_status_failed(result)) return result;
    for (i = 1; i <= 2; ++i) {
        if (maage_status_failed(result = maage_content_register_state(registry, config, &ids->logs[i], ids->block_log, (unsigned int) i, 0)) || maage_status_failed(result = maage_content_register_state(registry, config, &ids->leaves[i], ids->block_leaves, (unsigned int) i, 0))) goto failed;
        if (maage_status_failed(result = maage_content_register_state(registry, config, &ids->planks[i], ids->block_planks, (unsigned int) i, 0))) goto failed;
    }
    ids->logs[0] = ids->state_log;
    ids->leaves[0] = ids->state_leaves;
    ids->planks[0] = ids->state_planks;
    ids->torches[5] = ids->state_torch;
    ids->water[0] = ids->state_water;
    ids->lava[0] = ids->state_lava;
    ids->cactus[0] = ids->state_cactus;
    for (flags = 4; flags <= 12; flags += 4)
        for (species = 0; species < 3; ++species)
            if (maage_status_failed(result = maage_content_register_state(registry, config, &ids->leaves[flags + species], ids->block_leaves, (unsigned int) (flags + species), 0))) goto failed;
    for (i = 1; i <= 4; ++i)
        if (maage_status_failed(result = maage_content_register_state(registry, config, &ids->torches[i], ids->block_torch, (unsigned int) i, MAAGE_BASE_TEXTURE_EXTRA + 18))) goto failed;
    for (flags = 4; flags <= 8; flags += 4)
        for (species = 0; species < 3; ++species)
            if (maage_status_failed(result = maage_content_register_state(registry, config, &ids->logs[flags + species], ids->block_log, (unsigned int) (flags + species), 0))) goto failed;
    for (i = 1; i < 16; ++i) {
        if (maage_status_failed(result = maage_content_register_state(registry, config, &ids->cactus[i], ids->block_cactus, (unsigned int) i, MAAGE_BASE_TEXTURE_EXTRA + 22))) goto failed;
        if (maage_status_failed(result = maage_content_register_state(registry, config, &ids->water[i], ids->block_water, (unsigned int) i, MAAGE_BASE_TEXTURE_WATER)) || maage_status_failed(result = maage_content_register_state(registry, config, &ids->lava[i], ids->block_lava, (unsigned int) i, MAAGE_BASE_TEXTURE_EXTRA + 2))) goto failed;
    }
    ids->state_spruce_log = ids->logs[1];
    ids->state_birch_log = ids->logs[2];
    ids->state_spruce_leaves = ids->leaves[1];
    ids->state_birch_leaves = ids->leaves[2];
    for (i = 0; i < MAAGE_BASE_BIOME_COUNT; ++i)
        if (maage_status_failed(result = maage_content_register_state(registry, config, &ids->biome_grass[i], ids->block_grass, (unsigned int) i + 1, MAAGE_BASE_TEXTURE_GRASS_TOP))) goto failed;
    memset(&block, 0, sizeof(block));
    block.mesh_shape = MAAGE_BLOCK_MESH_CROSS;
    block.selectable = block.replaceable = block.use_neighbor_brightness = 1;
    block.drop_count = 1;
    block.random_tick = maage_random_tick_sapling;
    block.survives = maage_support_sapling;
    block.selection_shape.minimum[0] = block.selection_shape.minimum[2] = 0.1;
    block.selection_shape.maximum[0] = block.selection_shape.maximum[2] = 0.9;
    block.selection_shape.maximum[1] = 0.8;
    block.behavior_context = ids;
    block.unsupported_no_drops = 0;
    block.unsupported_state = MAAGE_CONTENT_ID_INVALID;
    block.item_drag = 0.588;
    block.flow_permeable = 0;
    result = maage_block_registry_register_block(registry, "minecraft:sapling", &block);
    if (maage_status_failed(result)) goto failed;
    ids->block_sapling = block.id;
    for (flags = 0; flags < 2; ++flags)
        for (species = 0; species < 3; ++species) {
            unsigned int metadata = (unsigned int) species | (flags ? 8U : 0U);
            int texture = MAAGE_BASE_TEXTURE_ITEMS + (species ? 25 + species : 23);
            result = maage_content_register_state(registry, config, &ids->saplings[metadata], ids->block_sapling, metadata, texture);
            if (maage_status_failed(result)) goto failed;
        }
    ids->state_sapling = ids->saplings[0];
    memset(&block, 0, sizeof(block));
    block.solid = block.material_solid = block.selectable = block.use_neighbor_brightness = 1;
    block.light_opacity = 15;
    block.hardness = 0.6;
    block.required_tool = MAAGE_TOOL_SHOVEL;
    block.drop_count = 1;
    block.random_tick = maage_random_tick_farmland;
    block.survives = maage_support_farmland;
    block.collision_shape.maximum[0] = block.collision_shape.maximum[2] = 1;
    block.collision_shape.maximum[1] = 15.0 / 16.0;
    block.selection_shape = block.collision_shape;
    block.behavior_context = ids;
    block.unsupported_no_drops = 0;
    block.unsupported_state = MAAGE_CONTENT_ID_INVALID;
    block.item_drag = 0.588;
    block.flow_permeable = 0;
    result = maage_block_registry_register_block(registry, "minecraft:farmland", &block);
    if (maage_status_failed(result)) goto failed;
    ids->block_farmland = block.id;
    for (i = 0; i < 8; ++i) {
        result = maage_content_register_state(registry, config, &ids->farmland[i], block.id, (unsigned int) i, MAAGE_BASE_TEXTURE_FARMLAND);
        if (maage_status_failed(result)) goto failed;
    }
    memset(&block, 0, sizeof(block));
    block.selectable = block.use_neighbor_brightness = 1;
    block.mesh_shape = MAAGE_BLOCK_MESH_CROSS;
    block.drop_count = 1;
    block.random_tick = maage_random_tick_wheat;
    block.survives = maage_support_wheat;
    block.selection_shape.maximum[0] = block.selection_shape.maximum[2] = 1;
    block.selection_shape.maximum[1] = 0.25;
    block.behavior_context = ids;
    block.unsupported_no_drops = 0;
    block.unsupported_state = MAAGE_CONTENT_ID_INVALID;
    block.item_drag = 0.588;
    block.flow_permeable = 0;
    result = maage_block_registry_register_block(registry, "minecraft:wheat", &block);
    if (maage_status_failed(result)) goto failed;
    ids->block_wheat = block.id;
    for (i = 0; i < 8; ++i) {
        result = maage_content_register_state(registry, config, &ids->wheat[i], block.id, (unsigned int) i, MAAGE_BASE_TEXTURE_WHEAT + i);
        if (maage_status_failed(result)) goto failed;
    }
    result = maage_furnace_content_blocks(registry, config);
    if (maage_status_failed(result)) return result;
    return maage_status_make(MAAGE_STATUS_SUCCESS, 0, MAAGE_OPERATION_REGISTER, 0);
failed:
    return result;
}
