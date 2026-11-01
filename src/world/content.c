#include "maage/content.h"
#include "world_internal.h"
#include <stdlib.h>
#include <string.h>

/* Shared content definitions serve headless and graphical initialization.
 * Both visit the same numeric registration
 * path; only the optional initialization-time presentation resolver differs. */
static maage_status maage_content_register_state(maage_block_registry *registry, const maage_content_config *config, maage_block_state_id *output, maage_block_id block, unsigned int metadata, int texture) {
    maage_builtin_ids *ids = maage_block_registry_initialize_ids(registry);
    maage_block_state_definition state;
    const maage_block_definition *definition;
    int face, channel, selected, species, log_axis;
    maage_status result;
    memset(&state, 0, sizeof(state));
    state.id = (unsigned long) maage_block_registry_state_count(registry);
    state.block_id = block;
    state.metadata = metadata;
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
        selected = texture;
        if (block == ids->block_grass) {
            selected = MAAGE_CONTENT_TEXTURE_GRASS_SIDE;
            if (face == MAAGE_FACE_NEGATIVE_Y) selected = MAAGE_CONTENT_TEXTURE_DIRT;
            if (face == MAAGE_FACE_POSITIVE_Y) selected = MAAGE_CONTENT_TEXTURE_GRASS_TOP;
        }
        if (block == ids->block_sandstone) {
            if (face == MAAGE_FACE_POSITIVE_Y) selected = MAAGE_CONTENT_TEXTURE_SANDSTONE_TOP;
            if (face == MAAGE_FACE_NEGATIVE_Y) selected = MAAGE_CONTENT_TEXTURE_SANDSTONE_BOTTOM;
        }
        if (block == ids->block_crafting_table) {
            selected = face == MAAGE_FACE_POSITIVE_Y ? MAAGE_CONTENT_TEXTURE_CRAFTING_TOP : (face == MAAGE_FACE_NEGATIVE_Y ? MAAGE_CONTENT_TEXTURE_EXTRA + 1 : ((face == MAAGE_FACE_NEGATIVE_X || face == MAAGE_FACE_NEGATIVE_Z) ? MAAGE_CONTENT_TEXTURE_CRAFTING_FRONT : MAAGE_CONTENT_TEXTURE_CRAFTING_SIDE));
        }
        if (block == ids->block_planks && metadata) selected = metadata == 1 ? MAAGE_CONTENT_TEXTURE_SPRUCE_PLANKS : MAAGE_CONTENT_TEXTURE_BIRCH_PLANKS;
        if (block == ids->block_log) {
            species = metadata & 3;
            selected = MAAGE_CONTENT_TEXTURE_EXTRA + (species == 1 ? 26 : (species == 2 ? 29 : 6));
            log_axis = (metadata & 12) == 4 ? 0 : ((metadata & 12) == 8 ? 2 : 1);
            if (face / 2 == log_axis) ++selected;
        } else if (block == ids->block_leaves) {
            species = metadata & MAAGE_LEAF_SPECIES_MASK;
            selected = MAAGE_CONTENT_TEXTURE_EXTRA + (species == 1 ? 28 : (species == 2 ? 31 : 8));
        } else if (block == ids->block_cactus) {
            if (face == MAAGE_FACE_POSITIVE_Y) selected = MAAGE_CONTENT_TEXTURE_EXTRA + 23;
            if (face == MAAGE_FACE_NEGATIVE_Y) selected = MAAGE_CONTENT_TEXTURE_EXTRA + 24;
        }
        state.faces[face].texture_id = (maage_texture_id) selected;
        state.faces[face].uv[2] = state.faces[face].uv[3] = 1.0f;
        for (channel = 0; channel < 4; ++channel)
            state.faces[face].tint[channel] = 1.0f;
        if (config && config->resolver) {
            result = config->resolver(config->resolver_context, ids, state.id, block, metadata, (maage_block_face) face, &state.faces[face], state.leaf_fast_uv);
            if (maage_status_failed(result)) return result;
            /* Presentation callbacks cannot rewrite logical tile identities. */
            if (state.faces[face].texture_id != (maage_texture_id) selected) return maage_status_make(MAAGE_STATUS_INVALID_STATE, MAAGE_CONTEXT_REGISTRY, MAAGE_OPERATION_REGISTER, 0);
        }
    }
    result = maage_block_registry_register_state(registry, &state);
    if (!maage_status_failed(result)) *output = state.id;
    return result;
}

static maage_status maage_content_blocks_create(const maage_content_config *config, const maage_biome_registry *biomes, maage_block_registry **output, maage_status_trace *trace) {
    static const char *const names[] = {"minecraft:air", "minecraft:stone", "minecraft:grass", "minecraft:dirt", "minecraft:bedrock", "minecraft:water", "minecraft:sand", "minecraft:gravel", "minecraft:sandstone"};

    static const int textures[] = {0, 0, 2, 1, 4, 5, 6, 7, 8};
    static const double hardness[] = {0, 1.5, 0.6, 0.5, -1, 100, 0.5, 0.6, 0.8};

    maage_block_registry *registry = NULL;
    maage_builtin_ids *ids;
    maage_status result;
    maage_block_definition block;

    int i, flags, species;
    if (output) *output = NULL;
    if (!output || !biomes) return maage_status_make(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_CONTEXT_REGISTRY, MAAGE_OPERATION_INITIALIZE, !output ? 2 : 1);

    result = maage_block_registry_create(0, 0, &registry);
    if (maage_status_failed(result)) return MAAGE_STATUS_PROPAGATE(trace, result, MAAGE_CONTEXT_REGISTRY, MAAGE_OPERATION_REGISTER);
    ids = maage_block_registry_initialize_ids(registry);
    ids->biome_ocean = biomes->ids.biome_ocean;
    ids->biome_plains = biomes->ids.biome_plains;
    ids->biome_desert = biomes->ids.biome_desert;
    ids->biome_hills = biomes->ids.biome_hills;
    ids->biome_forest = biomes->ids.biome_forest;
    ids->biome_taiga = biomes->ids.biome_taiga;
    {
        maage_block_id *references[9];
        maage_block_state_id *states[9];
        states[0] = &ids->state_air;
        states[1] = &ids->state_stone;
        states[2] = &ids->state_grass;
        states[3] = &ids->state_dirt;
        states[4] = &ids->state_bedrock;
        states[5] = &ids->state_water;
        states[6] = &ids->state_sand;
        states[7] = &ids->state_gravel;
        states[8] = &ids->state_sandstone;
        references[0] = &ids->block_air;
        references[1] = &ids->block_stone;
        references[2] = &ids->block_grass;
        references[3] = &ids->block_dirt;
        references[4] = &ids->block_bedrock;
        references[5] = &ids->block_water;
        references[6] = &ids->block_sand;
        references[7] = &ids->block_gravel;
        references[8] = &ids->block_sandstone;
        for (i = 0; i < 9; ++i) {
            memset(&block, 0, sizeof(block));

            block.solid = block.opaque = references[i] != &ids->block_air && references[i] != &ids->block_water;
            block.material_solid = block.solid;
            block.falls = references[i] == &ids->block_sand || references[i] == &ids->block_gravel;
            block.light_opacity = block.opaque ? 15U : (references[i] == &ids->block_water ? 3U : 0U);
            block.emitted_light = 0;
            if (references[i] == &ids->block_grass) block.random_tick = maage_random_tick_grass;
            block.selectable = block.solid;
            block.replaceable = !block.solid;
            block.hardness = hardness[i];
            block.drop_count = i == 0 || i == 4 || i == 5 ? 0U : 1U;
            if (references[i] == &ids->block_stone || references[i] == &ids->block_sandstone) {
                block.required_tool = MAAGE_TOOL_PICKAXE;
                block.required_tool_level = 0;
                block.harvest_requires_tool = 1;
            } else if (references[i] == &ids->block_grass || references[i] == &ids->block_dirt || references[i] == &ids->block_sand || references[i] == &ids->block_gravel) block.required_tool = MAAGE_TOOL_SHOVEL;
            if (block.solid) {
                block.collision_shape.maximum[0] = block.collision_shape.maximum[1] = block.collision_shape.maximum[2] = 1;
                block.selection_shape = block.collision_shape;
            }
            if (maage_status_failed(result = maage_block_registry_register_block(registry, names[i], &block))) goto failed;
            *references[i] = block.id;
            if (maage_status_failed(result = maage_content_register_state(registry, config, states[i], block.id, 0, textures[i]))) goto failed;
        }
    }
    {
        static const struct {
            size_t id;
            size_t state;
            const char *name;
            int texture, solid, opaque, cross;
            double hardness;
            unsigned int emission;
        } content[] = {
            {offsetof(maage_builtin_ids, block_cobblestone), offsetof(maage_builtin_ids, state_cobblestone), "cobblestone", 0, 1, 1, 0, 2, 0}, {offsetof(maage_builtin_ids, block_planks), offsetof(maage_builtin_ids, state_planks), "planks", 1, 1, 1, 0, 2, 0}, {offsetof(maage_builtin_ids, block_crafting_table), offsetof(maage_builtin_ids, state_crafting_table), "crafting_table", 0, 1, 1, 0, 2.5, 0}, {offsetof(maage_builtin_ids, block_gold_block), offsetof(maage_builtin_ids, state_gold_block), "gold_block", MAAGE_CONTENT_TEXTURE_STORAGE - MAAGE_CONTENT_TEXTURE_EXTRA, 1, 1, 0, 3, 0}, {offsetof(maage_builtin_ids, block_iron_block), offsetof(maage_builtin_ids, state_iron_block), "iron_block", MAAGE_CONTENT_TEXTURE_STORAGE + 1 - MAAGE_CONTENT_TEXTURE_EXTRA, 1, 1, 0, 5, 0}, {offsetof(maage_builtin_ids, block_diamond_block), offsetof(maage_builtin_ids, state_diamond_block), "diamond_block", MAAGE_CONTENT_TEXTURE_STORAGE + 2 - MAAGE_CONTENT_TEXTURE_EXTRA, 1, 1, 0, 5, 0}, {offsetof(maage_builtin_ids, block_lapis_block), offsetof(maage_builtin_ids, state_lapis_block), "lapis_block", MAAGE_CONTENT_TEXTURE_STORAGE + 3 - MAAGE_CONTENT_TEXTURE_EXTRA, 1, 1, 0, 3, 0}, {offsetof(maage_builtin_ids, block_emerald_block), offsetof(maage_builtin_ids, state_emerald_block), "emerald_block", MAAGE_CONTENT_TEXTURE_STORAGE + 4 - MAAGE_CONTENT_TEXTURE_EXTRA, 1, 1, 0, 5, 0}, {offsetof(maage_builtin_ids, block_redstone_block), offsetof(maage_builtin_ids, state_redstone_block), "redstone_block", MAAGE_CONTENT_TEXTURE_STORAGE + 5 - MAAGE_CONTENT_TEXTURE_EXTRA, 1, 1, 0, 5, 0}, {offsetof(maage_builtin_ids, block_coal_block), offsetof(maage_builtin_ids, state_coal_block), "coal_block", MAAGE_CONTENT_TEXTURE_STORAGE + 6 - MAAGE_CONTENT_TEXTURE_EXTRA, 1, 1, 0, 5, 0}, {offsetof(maage_builtin_ids, block_snow_block), offsetof(maage_builtin_ids, state_snow_block), "snow_block", MAAGE_CONTENT_TEXTURE_STORAGE + 7 - MAAGE_CONTENT_TEXTURE_EXTRA, 1, 1, 0, 0.2, 0}, {offsetof(maage_builtin_ids, block_lava), offsetof(maage_builtin_ids, state_lava), "lava", 2, 0, 0, 0, 100, 15}, {offsetof(maage_builtin_ids, block_gold_ore), offsetof(maage_builtin_ids, state_gold_ore), "gold_ore", 3, 1, 1, 0, 3, 0}, {offsetof(maage_builtin_ids, block_iron_ore), offsetof(maage_builtin_ids, state_iron_ore), "iron_ore", 4, 1, 1, 0, 3, 0}, {offsetof(maage_builtin_ids, block_coal_ore), offsetof(maage_builtin_ids, state_coal_ore), "coal_ore", 5, 1, 1, 0, 3, 0}, {offsetof(maage_builtin_ids, block_log), offsetof(maage_builtin_ids, state_log), "log", 6, 1, 1, 0, 2, 0}, {offsetof(maage_builtin_ids, block_leaves), offsetof(maage_builtin_ids, state_leaves), "leaves", 8, 1, 0, 0, 0.2, 0}, {offsetof(maage_builtin_ids, block_lapis_ore), offsetof(maage_builtin_ids, state_lapis_ore), "lapis_ore", 9, 1, 1, 0, 3, 0}, {offsetof(maage_builtin_ids, block_tall_grass), offsetof(maage_builtin_ids, state_tall_grass), "tall_grass", 10, 0, 0, 1, 0, 0}, {offsetof(maage_builtin_ids, block_dead_bush), offsetof(maage_builtin_ids, state_dead_bush), "dead_bush", 11, 0, 0, 1, 0, 0}, {offsetof(maage_builtin_ids, block_dandelion), offsetof(maage_builtin_ids, state_dandelion), "dandelion", 12, 0, 0, 1, 0, 0}, {offsetof(maage_builtin_ids, block_poppy), offsetof(maage_builtin_ids, state_poppy), "poppy", 13, 0, 0, 1, 0, 0}, {offsetof(maage_builtin_ids, block_brown_mushroom), offsetof(maage_builtin_ids, state_brown_mushroom), "brown_mushroom", 14, 0, 0, 1, 0, 0}, {offsetof(maage_builtin_ids, block_red_mushroom), offsetof(maage_builtin_ids, state_red_mushroom), "red_mushroom", 15, 0, 0, 1, 0, 0}, {offsetof(maage_builtin_ids, block_mossy_cobblestone), offsetof(maage_builtin_ids, state_mossy_cobblestone), "mossy_cobblestone", 16, 1, 1, 0, 2, 0}, {offsetof(maage_builtin_ids, block_obsidian), offsetof(maage_builtin_ids, state_obsidian), "obsidian", 17, 1, 1, 0, 50, 0}, {offsetof(maage_builtin_ids, block_torch), offsetof(maage_builtin_ids, state_torch), "torch", 18, 0, 0, 0, 0, 14}, {offsetof(maage_builtin_ids, block_diamond_ore), offsetof(maage_builtin_ids, state_diamond_ore), "diamond_ore", 19, 1, 1, 0, 3, 0}, {offsetof(maage_builtin_ids, block_redstone_ore), offsetof(maage_builtin_ids, state_redstone_ore), "redstone_ore", 20, 1, 1, 0, 3, 0}, {offsetof(maage_builtin_ids, block_snow), offsetof(maage_builtin_ids, state_snow), "snow", 21, 1, 0, 0, 0.1, 0}, {offsetof(maage_builtin_ids, block_ice), offsetof(maage_builtin_ids, state_ice), "ice", 32, 1, 0, 0, 0.5, 0}, {offsetof(maage_builtin_ids, block_cactus), offsetof(maage_builtin_ids, state_cactus), "cactus", 22, 1, 0, 0, 0.4, 0}, {offsetof(maage_builtin_ids, block_emerald_ore), offsetof(maage_builtin_ids, state_emerald_ore), "emerald_ore", 25, 1, 1, 0, 3, 0}};
        char name[96];
        size_t entry;
        unsigned int axis;
        for (entry = 0; entry < sizeof(content) / sizeof(content[0]); ++entry) {
            memset(&block, 0, sizeof(block));

            block.solid = content[entry].solid;
            block.material_solid = block.solid && content[entry].id != offsetof(maage_builtin_ids, block_snow);
            block.opaque = content[entry].opaque;
            block.hardness = content[entry].hardness;
            block.light_opacity = block.opaque ? 15U : (content[entry].id == offsetof(maage_builtin_ids, block_leaves) ? 1U : (content[entry].id == offsetof(maage_builtin_ids, block_ice) ? 3U : 0U));
            block.emitted_light = content[entry].emission;
            block.use_neighbor_brightness = block.light_opacity == 0;
            if (content[entry].id == offsetof(maage_builtin_ids, block_cactus)) block.random_tick = maage_random_tick_cactus;
            if (content[entry].id == offsetof(maage_builtin_ids, block_leaves)) block.random_tick = maage_random_tick_leaves;
            if (content[entry].id == offsetof(maage_builtin_ids, block_brown_mushroom) || content[entry].id == offsetof(maage_builtin_ids, block_red_mushroom)) block.random_tick = maage_random_tick_mushroom;
            if (content[entry].id == offsetof(maage_builtin_ids, block_tall_grass) || content[entry].id == offsetof(maage_builtin_ids, block_dead_bush) || content[entry].id == offsetof(maage_builtin_ids, block_dandelion) || content[entry].id == offsetof(maage_builtin_ids, block_poppy)) block.random_tick = maage_random_tick_bush;
            if (content[entry].id == offsetof(maage_builtin_ids, block_tall_grass) || content[entry].id == offsetof(maage_builtin_ids, block_dead_bush) ||
                content[entry].id == offsetof(maage_builtin_ids, block_dandelion) || content[entry].id == offsetof(maage_builtin_ids, block_poppy) ||
                content[entry].id == offsetof(maage_builtin_ids, block_torch) || content[entry].id == offsetof(maage_builtin_ids, block_cactus) ||
                content[entry].id == offsetof(maage_builtin_ids, block_snow) || content[entry].id == offsetof(maage_builtin_ids, block_brown_mushroom) ||
                content[entry].id == offsetof(maage_builtin_ids, block_red_mushroom)) block.survives = maage_support_rule;
            if (content[entry].id == offsetof(maage_builtin_ids, block_snow)) block.random_tick = maage_random_tick_snow;
            if (content[entry].id == offsetof(maage_builtin_ids, block_ice)) block.random_tick = maage_random_tick_ice;
            block.mesh_shape = content[entry].cross ? MAAGE_BLOCK_MESH_CROSS : MAAGE_BLOCK_MESH_CUBE;
            if (content[entry].id == offsetof(maage_builtin_ids, block_torch)) block.mesh_shape = MAAGE_BLOCK_MESH_TORCH;
            block.selectable = content[entry].id != offsetof(maage_builtin_ids, block_lava);
            block.replaceable = !block.solid;
            if (content[entry].id != offsetof(maage_builtin_ids, block_lava) && content[entry].id != offsetof(maage_builtin_ids, block_leaves)) {
                block.drop_count = 1;
            }
            for (axis = 0; axis < 3; ++axis)
                block.selection_shape.maximum[axis] = 1;
            if (block.solid) block.collision_shape = block.selection_shape;
            if (content[entry].cross) {
                block.selection_shape.minimum[0] = block.selection_shape.minimum[2] = 0.1;
                block.selection_shape.maximum[0] = block.selection_shape.maximum[2] = 0.9;
                block.selection_shape.maximum[1] = 0.8;
                if (content[entry].id != offsetof(maage_builtin_ids, block_tall_grass) && content[entry].id != offsetof(maage_builtin_ids, block_dead_bush)) {
                    block.selection_shape.minimum[0] = block.selection_shape.minimum[2] = 0.3;
                    block.selection_shape.maximum[0] = block.selection_shape.maximum[2] = 0.7;
                    block.selection_shape.maximum[1] = content[entry].id == offsetof(maage_builtin_ids, block_brown_mushroom) || content[entry].id == offsetof(maage_builtin_ids, block_red_mushroom) ? 0.4 : 0.6;
                }
            }
            if (content[entry].id == offsetof(maage_builtin_ids, block_torch)) {
                block.selection_shape.minimum[0] = block.selection_shape.minimum[2] = 0.4;
                block.selection_shape.maximum[0] = block.selection_shape.maximum[2] = 0.6;
                block.selection_shape.maximum[1] = 0.6;
            }
            if (content[entry].id == offsetof(maage_builtin_ids, block_snow)) {
                /* The current snow state is one visible/selectable layer,
                 * with no raised body collision. */
                block.selection_shape.maximum[1] = 0.125;
                block.solid = 0;
                memset(&block.collision_shape, 0, sizeof(block.collision_shape));
                block.replaceable = 1;
            } else if (content[entry].id == offsetof(maage_builtin_ids, block_cactus)) {
                block.contact_damage = 1;
                block.contact_shape.maximum[0] = block.contact_shape.maximum[1] = block.contact_shape.maximum[2] = 1;
                block.collision_shape.minimum[0] = block.collision_shape.minimum[2] = 0.0625;
                block.collision_shape.maximum[0] = block.collision_shape.maximum[2] = 0.9375;
                block.selection_shape = block.collision_shape;
                block.collision_shape.maximum[1] = 0.9375;
            }
            if (content[entry].id == offsetof(maage_builtin_ids, block_log) || content[entry].id == offsetof(maage_builtin_ids, block_planks) || content[entry].id == offsetof(maage_builtin_ids, block_crafting_table)) block.required_tool = MAAGE_TOOL_AXE;
            else if (block.solid && content[entry].id != offsetof(maage_builtin_ids, block_leaves) && content[entry].id != offsetof(maage_builtin_ids, block_snow) && content[entry].id != offsetof(maage_builtin_ids, block_ice) && content[entry].id != offsetof(maage_builtin_ids, block_cactus)) {
                block.required_tool = MAAGE_TOOL_PICKAXE;
                block.harvest_requires_tool = 1;
                block.required_tool_level = content[entry].id == offsetof(maage_builtin_ids, block_obsidian) ? 3U : (content[entry].id == offsetof(maage_builtin_ids, block_gold_block) || content[entry].id == offsetof(maage_builtin_ids, block_diamond_block) || content[entry].id == offsetof(maage_builtin_ids, block_emerald_block) || content[entry].id == offsetof(maage_builtin_ids, block_gold_ore) || content[entry].id == offsetof(maage_builtin_ids, block_diamond_ore) || content[entry].id == offsetof(maage_builtin_ids, block_redstone_ore) || content[entry].id == offsetof(maage_builtin_ids, block_emerald_ore) ? 2U : (content[entry].id == offsetof(maage_builtin_ids, block_iron_block) || content[entry].id == offsetof(maage_builtin_ids, block_lapis_block) || content[entry].id == offsetof(maage_builtin_ids, block_iron_ore) || content[entry].id == offsetof(maage_builtin_ids, block_lapis_ore) ? 1U : 0U));
            }
            if (content[entry].id == offsetof(maage_builtin_ids, block_snow_block)) {
                block.drop_count = 4;
            }
            if (content[entry].id == offsetof(maage_builtin_ids, block_snow) || content[entry].id == offsetof(maage_builtin_ids, block_snow_block)) {
                block.required_tool = MAAGE_TOOL_SHOVEL;
                block.harvest_requires_tool = 1;
            }
            if (content[entry].id == offsetof(maage_builtin_ids, block_ice)) block.required_tool = MAAGE_TOOL_PICKAXE;
            if (block.mesh_shape == MAAGE_BLOCK_MESH_CROSS) block.required_tool = MAAGE_TOOL_AXE;
            strcpy(name, "minecraft:");
            strcat(name, content[entry].name);
            result = maage_block_registry_register_block(registry, name, &block);
            if (maage_status_failed(result)) goto failed;
            *(unsigned long *) ((unsigned char *) ids + content[entry].id) = block.id;
            result = maage_content_register_state(registry, config, (unsigned long *) ((unsigned char *) ids + content[entry].state), block.id, content[entry].id == offsetof(maage_builtin_ids, block_tall_grass) ? 1U : (content[entry].id == offsetof(maage_builtin_ids, block_torch) ? 5U : 0U), MAAGE_CONTENT_TEXTURE_EXTRA + content[entry].texture);
            if (maage_status_failed(result)) goto failed;
        }
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
            if (maage_status_failed(result = maage_content_register_state(registry, config, &ids->torches[i], ids->block_torch, (unsigned int) i, MAAGE_CONTENT_TEXTURE_EXTRA + 18))) goto failed;
        for (flags = 4; flags <= 8; flags += 4)
            for (species = 0; species < 3; ++species)
                if (maage_status_failed(result = maage_content_register_state(registry, config, &ids->logs[flags + species], ids->block_log, (unsigned int) (flags + species), 0))) goto failed;
        for (i = 1; i < 16; ++i) {
            if (maage_status_failed(result = maage_content_register_state(registry, config, &ids->cactus[i], ids->block_cactus, (unsigned int) i, MAAGE_CONTENT_TEXTURE_EXTRA + 22))) goto failed;
            if (maage_status_failed(result = maage_content_register_state(registry, config, &ids->water[i], ids->block_water, (unsigned int) i, MAAGE_CONTENT_TEXTURE_WATER)) || maage_status_failed(result = maage_content_register_state(registry, config, &ids->lava[i], ids->block_lava, (unsigned int) i, MAAGE_CONTENT_TEXTURE_EXTRA + 2))) goto failed;
        }
        ids->state_spruce_log = ids->logs[1];
        ids->state_birch_log = ids->logs[2];
        ids->state_spruce_leaves = ids->leaves[1];
        ids->state_birch_leaves = ids->leaves[2];
    }
    for (i = 0; i < MAAGE_BIOME_COUNT; ++i)
        if (maage_status_failed(result = maage_content_register_state(registry, config, &ids->biome_grass[i], ids->block_grass, (unsigned int) i + 1, MAAGE_CONTENT_TEXTURE_GRASS_TOP))) goto failed;
    memset(&block, 0, sizeof(block));
    block.mesh_shape = MAAGE_BLOCK_MESH_CROSS;
    block.selectable = block.replaceable = block.use_neighbor_brightness = 1;
    block.drop_count = 1;
    block.random_tick = maage_random_tick_sapling;
    block.survives = maage_support_rule;
    block.selection_shape.minimum[0] = block.selection_shape.minimum[2] = 0.1;
    block.selection_shape.maximum[0] = block.selection_shape.maximum[2] = 0.9;
    block.selection_shape.maximum[1] = 0.8;
    result = maage_block_registry_register_block(registry, "minecraft:sapling", &block);
    if (maage_status_failed(result)) goto failed;
    ids->block_sapling = block.id;
    for (flags = 0; flags < 2; ++flags)
        for (species = 0; species < 3; ++species) {
            unsigned int metadata = (unsigned int) species | (flags ? 8U : 0U);
            int texture = MAAGE_CONTENT_TEXTURE_ITEMS + (species ? 25 + species : 23);
            result = maage_content_register_state(registry, config, &ids->saplings[metadata], ids->block_sapling, metadata, texture);
            if (maage_status_failed(result)) goto failed;
        }
    ids->state_sapling = ids->saplings[0];
    *output = registry;
    return maage_status_make(MAAGE_STATUS_SUCCESS, 0, MAAGE_OPERATION_REGISTER, 0);
failed:
    maage_block_registry_destroy(registry);
    if (!maage_status_failed(result)) result = maage_status_make(MAAGE_STATUS_INVARIANT_VIOLATION, MAAGE_CONTEXT_REGISTRY, MAAGE_OPERATION_REGISTER, 0);
    return MAAGE_STATUS_PROPAGATE(trace, result, MAAGE_CONTEXT_REGISTRY, MAAGE_OPERATION_REGISTER);
}

struct maage_content {
    maage_biome_registry biomes;
    maage_block_registry *blocks;
    maage_item_registry *items;
    maage_recipe_registry *recipes;
};

maage_status maage_content_create(const maage_content_config *config, maage_content **output, maage_status_trace *trace) {
    maage_content *content;
    maage_status result;
    const maage_biome_definition *source;
    unsigned int id;
    if (!output) return maage_status_make(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_CONTEXT_REGISTRY, MAAGE_OPERATION_CREATE, 1);
    *output = NULL;
    content = (maage_content *) calloc(1, sizeof(*content));
    if (!content) return MAAGE_STATUS_PROPAGATE(trace, maage_status_make(MAAGE_STATUS_ALLOCATION_FAILED, MAAGE_CONTEXT_REGISTRY, MAAGE_OPERATION_CREATE, 0), MAAGE_CONTEXT_REGISTRY, MAAGE_OPERATION_CREATE);
    result = maage_biome_registry_init(&content->biomes);
    if (maage_status_failed(result)) goto failed;
    if (config && config->presentation_biomes) {
        for (id = 0; id < MAAGE_BIOME_COUNT; ++id) {
            source = maage_biome_registry_get(config->presentation_biomes, id);
            if (!source) {
                result = maage_status_make(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_CONTEXT_REGISTRY, MAAGE_OPERATION_INITIALIZE, 0);
                goto failed;
            }
            memcpy(content->biomes.builtin_definitions[id].grass_color, source->grass_color, sizeof(source->grass_color));
            memcpy(content->biomes.builtin_definitions[id].foliage_color, source->foliage_color, sizeof(source->foliage_color));
        }
    }
    result = maage_content_blocks_create(config, &content->biomes, &content->blocks, trace);
    if (maage_status_failed(result)) goto failed;
    content->biomes.ids = *maage_block_registry_builtin_ids(content->blocks);
    result = maage_biome_registry_finalize(&content->biomes);
    if (maage_status_failed(result)) goto failed;
    result = maage_item_registry_create_builtin(content->blocks, &content->items);
    if (maage_status_failed(result)) goto failed;
    *maage_block_registry_initialize_ids(content->blocks) = *maage_item_registry_builtin_ids(content->items);
    result = maage_block_registry_bind_items(content->blocks, content->items);
    if (maage_status_failed(result)) goto failed;
    result = maage_block_registry_finalize(content->blocks);
    if (maage_status_failed(result)) goto failed;
    result = maage_recipe_registry_create_builtin(content->items, &content->recipes);
    if (maage_status_failed(result)) goto failed;
    *output = content;
    return maage_status_make(MAAGE_STATUS_SUCCESS, 0, MAAGE_OPERATION_CREATE, 0);
failed:
    maage_content_destroy(content);
    return MAAGE_STATUS_PROPAGATE(trace, result, MAAGE_CONTEXT_REGISTRY, MAAGE_OPERATION_CREATE);
}

const maage_block_registry *maage_content_blocks(const maage_content *content) {
    return content ? content->blocks : NULL;
}

const maage_biome_registry *maage_content_biomes(const maage_content *content) {
    return content ? &content->biomes : NULL;
}

const maage_item_registry *maage_content_items(const maage_content *content) {
    return content ? content->items : NULL;
}

const maage_recipe_registry *maage_content_recipes(const maage_content *content) {
    return content ? content->recipes : NULL;
}

maage_status maage_content_destroy(maage_content *content) {
    if (!content) return maage_status_make(MAAGE_STATUS_NOOP, 0, MAAGE_OPERATION_DESTROY, 0);
    /* No borrowed World/job/gameplay readers may remain at this boundary. */
    maage_recipe_registry_destroy(content->recipes);
    maage_item_registry_destroy(content->items);
    maage_block_registry_destroy(content->blocks);
    maage_biome_registry_destroy(&content->biomes);
    free(content);
    return maage_status_make(MAAGE_STATUS_SUCCESS, 0, MAAGE_OPERATION_DESTROY, 0);
}
