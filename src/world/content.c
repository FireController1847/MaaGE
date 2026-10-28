#include "maage/content.h"
#include <stdlib.h>
#include <string.h>

/* Shared 1.7.10 logical definitions are extracted unchanged from the Client.
 * Headless and graphical initialization visit the same numeric registration
 * path; only the optional initialization-time presentation resolver differs. */
static maage_status maage_content_register_state(maage_block_registry *registry,
    const maage_content_config *config, maage_block_state_id id,
    maage_block_id block, unsigned int metadata, int texture)
{
    maage_block_state_definition state;
    int face, channel, selected, level, species, log_axis;
    maage_status result;
    memset(&state, 0, sizeof(state));
    state.id = id; state.block_id = block; state.metadata = metadata;
    if (block == MAAGE_BLOCK_TORCH && metadata >= 1 && metadata <= 4) {
        state.selection_shape.minimum[0] = state.selection_shape.minimum[2] = 0.35;
        state.selection_shape.maximum[0] = state.selection_shape.maximum[2] = 0.65;
        state.selection_shape.minimum[1] = 0.2; state.selection_shape.maximum[1] = 0.8;
        log_axis = metadata <= 2 ? 0 : 2;
        state.selection_shape.minimum[log_axis] = metadata & 1 ? 0.0 : 0.7;
        state.selection_shape.maximum[log_axis] = metadata & 1 ? 0.3 : 1.0;
    }
    if (block == MAAGE_BLOCK_WATER || block == MAAGE_BLOCK_LAVA) {
        state.fluid_kind = block == MAAGE_BLOCK_WATER ? MAAGE_FLUID_WATER : MAAGE_FLUID_LAVA;
        state.fluid_level = metadata; state.fluid_source_state = block;
        state.fluid_states[0] = block;
        for (level = 1; level < 16; ++level) state.fluid_states[level] =
            (block == MAAGE_BLOCK_WATER ? MAAGE_STATE_WATER_FLOW_BASE : MAAGE_STATE_LAVA_FLOW_BASE) + level - 1;
    }
    for (face = 0; face < MAAGE_FACE_COUNT; ++face) {
        selected = texture;
        if (block == MAAGE_BLOCK_GRASS) {
            selected = MAAGE_CONTENT_TEXTURE_GRASS_SIDE;
            if (face == MAAGE_FACE_NEGATIVE_Y) selected = MAAGE_CONTENT_TEXTURE_DIRT;
            if (face == MAAGE_FACE_POSITIVE_Y)
                selected = MAAGE_CONTENT_TEXTURE_GRASS_TOP;
        }
        if (block == MAAGE_BLOCK_SANDSTONE) {
            if (face == MAAGE_FACE_POSITIVE_Y) selected = MAAGE_CONTENT_TEXTURE_SANDSTONE_TOP;
            if (face == MAAGE_FACE_NEGATIVE_Y) selected = MAAGE_CONTENT_TEXTURE_SANDSTONE_BOTTOM;
        }
        if (block == MAAGE_BLOCK_CRAFTING_TABLE) {
            selected = face == MAAGE_FACE_POSITIVE_Y ? MAAGE_CONTENT_TEXTURE_CRAFTING_TOP :
                (face == MAAGE_FACE_NEGATIVE_Y ? MAAGE_CONTENT_TEXTURE_EXTRA + 1 :
                ((face == MAAGE_FACE_NEGATIVE_X || face == MAAGE_FACE_NEGATIVE_Z) ? MAAGE_CONTENT_TEXTURE_CRAFTING_FRONT : MAAGE_CONTENT_TEXTURE_CRAFTING_SIDE));
        }
        if (block == MAAGE_BLOCK_PLANKS && metadata)
            selected = metadata == 1 ? MAAGE_CONTENT_TEXTURE_SPRUCE_PLANKS : MAAGE_CONTENT_TEXTURE_BIRCH_PLANKS;
        if (block == MAAGE_BLOCK_LOG) {
            species = metadata & 3;
            selected = MAAGE_CONTENT_TEXTURE_EXTRA + (species == 1 ? 26 : (species == 2 ? 29 : 6));
            log_axis = (metadata & 12) == 4 ? 0 : ((metadata & 12) == 8 ? 2 : 1);
            if (face / 2 == log_axis) ++selected;
        } else if (block == MAAGE_BLOCK_LEAVES)
            selected = MAAGE_CONTENT_TEXTURE_EXTRA + (metadata == 1 ? 28 : (metadata == 2 ? 31 : 8));
        else if (block == MAAGE_BLOCK_CACTUS) {
            if (face == MAAGE_FACE_POSITIVE_Y) selected = MAAGE_CONTENT_TEXTURE_EXTRA + 23;
            if (face == MAAGE_FACE_NEGATIVE_Y) selected = MAAGE_CONTENT_TEXTURE_EXTRA + 24;
        }
        state.faces[face].texture_id = (maage_texture_id)selected;
        state.faces[face].uv[2] = state.faces[face].uv[3] = 1.0f;
        for (channel = 0; channel < 4; ++channel) state.faces[face].tint[channel] = 1.0f;
        if (config && config->resolver) {
            result = config->resolver(config->resolver_context, id, block, metadata,
                (maage_block_face)face, &state.faces[face], state.leaf_fast_uv);
            if (maage_status_failed(result)) return result;
            /* Presentation callbacks cannot rewrite logical tile identities. */
            if (state.faces[face].texture_id != (maage_texture_id)selected)
                return maage_status_make(MAAGE_STATUS_INVALID_STATE,
                    MAAGE_CONTEXT_REGISTRY, MAAGE_OPERATION_REGISTER, 0);
        }
    }
    return maage_block_registry_register_state(registry, &state);
}

static maage_status maage_content_blocks_create(
    const maage_content_config *config, const maage_biome_registry *biomes,
    maage_block_registry **output, maage_status_trace *trace)
{
    static const char *const names[] = {"minecraft:air", "minecraft:stone", "minecraft:grass", "minecraft:dirt", "minecraft:bedrock", "minecraft:water", "minecraft:sand", "minecraft:gravel", "minecraft:sandstone"};
    static const maage_block_id ids[] = {0, 1, 2, 3, 7, 9, 12, 13, 24};
    static const int textures[] = {0, 0, 2, 1, 4, 5, 6, 7, 8};
    static const double hardness[] = {0, 1.5, 0.6, 0.5, -1, 100, 0.5, 0.6, 0.8};
    static const unsigned long drops[] = {0, 4, 3, 3, 0, 0, 12, 13, 24};
    maage_block_registry *registry = NULL;
    maage_status result;
    maage_block_definition block;
    int i;
    if (output) *output = NULL;
    if (!output || !biomes) return maage_status_make(MAAGE_STATUS_INVALID_ARGUMENT,
        MAAGE_CONTEXT_REGISTRY, MAAGE_OPERATION_INITIALIZE, !output ? 2 : 1);
    if (!biomes->finalized) return maage_status_make(MAAGE_STATUS_INVALID_STATE,
        MAAGE_CONTEXT_REGISTRY, MAAGE_OPERATION_INITIALIZE, 0);
    result = maage_block_registry_create(0, 0, &registry);
    if (maage_status_failed(result)) return MAAGE_STATUS_PROPAGATE(trace, result, MAAGE_CONTEXT_REGISTRY, MAAGE_OPERATION_REGISTER);
    for (i = 0; i < 9; ++i) {
        memset(&block, 0, sizeof(block));
        block.id = ids[i];
        block.solid = block.opaque = ids[i] != MAAGE_BLOCK_AIR && ids[i] != MAAGE_BLOCK_WATER;
        block.light_opacity = block.opaque ? 15U : (ids[i] == MAAGE_BLOCK_WATER ? 3U : 0U);
        block.emitted_light = 0;
        block.selectable = block.solid;
        block.replaceable = !block.solid;
        block.hardness = hardness[i];
        block.drop_item_id = drops[i]; block.drop_count = drops[i] ? 1U : 0U;
        if (ids[i] == MAAGE_BLOCK_STONE || ids[i] == MAAGE_BLOCK_SANDSTONE) {
            block.required_tool = MAAGE_TOOL_PICKAXE; block.required_tool_level = 0; block.harvest_requires_tool = 1;
        } else if (ids[i] == MAAGE_BLOCK_GRASS || ids[i] == MAAGE_BLOCK_DIRT ||
            ids[i] == MAAGE_BLOCK_SAND || ids[i] == MAAGE_BLOCK_GRAVEL)
            block.required_tool = MAAGE_TOOL_SHOVEL;
        if (block.solid) {
            block.collision_shape.maximum[0] = block.collision_shape.maximum[1] = block.collision_shape.maximum[2] = 1;
            block.selection_shape = block.collision_shape;
        }
        if (maage_status_failed(result = maage_block_registry_register_block(registry, names[i], &block)) ||
            maage_status_failed(result = maage_content_register_state(registry, config, ids[i], ids[i], 0, textures[i]))) goto failed;
    }
    {
        static const struct {
            maage_block_id id; const char *name;
            int texture, solid, opaque, cross; double hardness;
            unsigned int emission;
        } content[] = {
            {4,"cobblestone",0,1,1,0,2,0}, {5,"planks",1,1,1,0,2,0},
            {MAAGE_BLOCK_CRAFTING_TABLE,"crafting_table",0,1,1,0,2.5,0},
            {MAAGE_BLOCK_GOLD_BLOCK,"gold_block",MAAGE_CONTENT_TEXTURE_STORAGE - MAAGE_CONTENT_TEXTURE_EXTRA,1,1,0,3,0},
            {MAAGE_BLOCK_IRON_BLOCK,"iron_block",MAAGE_CONTENT_TEXTURE_STORAGE + 1 - MAAGE_CONTENT_TEXTURE_EXTRA,1,1,0,5,0},
            {MAAGE_BLOCK_DIAMOND_BLOCK,"diamond_block",MAAGE_CONTENT_TEXTURE_STORAGE + 2 - MAAGE_CONTENT_TEXTURE_EXTRA,1,1,0,5,0},
            {MAAGE_BLOCK_LAPIS_BLOCK,"lapis_block",MAAGE_CONTENT_TEXTURE_STORAGE + 3 - MAAGE_CONTENT_TEXTURE_EXTRA,1,1,0,3,0},
            {MAAGE_BLOCK_EMERALD_BLOCK,"emerald_block",MAAGE_CONTENT_TEXTURE_STORAGE + 4 - MAAGE_CONTENT_TEXTURE_EXTRA,1,1,0,5,0},
            {MAAGE_BLOCK_REDSTONE_BLOCK,"redstone_block",MAAGE_CONTENT_TEXTURE_STORAGE + 5 - MAAGE_CONTENT_TEXTURE_EXTRA,1,1,0,5,0},
            {MAAGE_BLOCK_COAL_BLOCK,"coal_block",MAAGE_CONTENT_TEXTURE_STORAGE + 6 - MAAGE_CONTENT_TEXTURE_EXTRA,1,1,0,5,0},
            {MAAGE_BLOCK_SNOW_BLOCK,"snow_block",MAAGE_CONTENT_TEXTURE_STORAGE + 7 - MAAGE_CONTENT_TEXTURE_EXTRA,1,1,0,0.2,0},
            {11,"lava",2,0,0,0,100,15}, {14,"gold_ore",3,1,1,0,3,0},
            {15,"iron_ore",4,1,1,0,3,0}, {16,"coal_ore",5,1,1,0,3,0},
            {17,"log",6,1,1,0,2,0}, {18,"leaves",8,1,0,0,0.2,0},
            {21,"lapis_ore",9,1,1,0,3,0}, {31,"tall_grass",10,0,0,1,0,0},
            {32,"dead_bush",11,0,0,1,0,0}, {37,"dandelion",12,0,0,1,0,0},
            {38,"poppy",13,0,0,1,0,0}, {39,"brown_mushroom",14,0,0,1,0,0},
            {40,"red_mushroom",15,0,0,1,0,0}, {48,"mossy_cobblestone",16,1,1,0,2,0},
            {49,"obsidian",17,1,1,0,50,0}, {50,"torch",18,0,0,0,0,14},
            {56,"diamond_ore",19,1,1,0,3,0}, {73,"redstone_ore",20,1,1,0,3,0},
            {78,"snow",21,1,0,0,0.1,0}, {79,"ice",32,1,0,0,0.5,0},
            {81,"cactus",22,1,0,0,0.4,0}, {129,"emerald_ore",25,1,1,0,3,0}
        };
        char name[96];
        size_t entry;
        unsigned int axis;
        for (entry = 0; entry < sizeof(content) / sizeof(content[0]); ++entry) {
            memset(&block, 0, sizeof(block));
            block.id = content[entry].id; block.solid = content[entry].solid;
            block.opaque = content[entry].opaque; block.hardness = content[entry].hardness;
            block.light_opacity = block.opaque ? 15U :
                (block.id == MAAGE_BLOCK_LEAVES ? 1U : (block.id == MAAGE_BLOCK_ICE ? 3U : 0U));
            block.emitted_light = content[entry].emission;
            block.mesh_shape = content[entry].cross ? MAAGE_BLOCK_MESH_CROSS : MAAGE_BLOCK_MESH_CUBE;
            if (block.id == MAAGE_BLOCK_TORCH) block.mesh_shape = MAAGE_BLOCK_MESH_TORCH;
            block.selectable = block.id != MAAGE_BLOCK_LAVA; block.replaceable = !block.solid;
            if (block.id != MAAGE_BLOCK_LAVA && block.id != MAAGE_BLOCK_LEAVES) {
                block.drop_item_id = block.id; block.drop_count = 1;
            }
            for (axis = 0; axis < 3; ++axis) block.selection_shape.maximum[axis] = 1;
            if (block.solid) block.collision_shape = block.selection_shape;
            if (content[entry].cross) {
                block.selection_shape.minimum[0] = block.selection_shape.minimum[2] = 0.1;
                block.selection_shape.maximum[0] = block.selection_shape.maximum[2] = 0.9;
                block.selection_shape.maximum[1] = 0.8;
                if (block.id != MAAGE_BLOCK_TALL_GRASS && block.id != MAAGE_BLOCK_DEAD_BUSH) {
                    block.selection_shape.minimum[0] = block.selection_shape.minimum[2] = 0.3;
                    block.selection_shape.maximum[0] = block.selection_shape.maximum[2] = 0.7;
                    block.selection_shape.maximum[1] = block.id == MAAGE_BLOCK_BROWN_MUSHROOM ||
                        block.id == MAAGE_BLOCK_RED_MUSHROOM ? 0.4 : 0.6;
                }
            }
            if (block.id == MAAGE_BLOCK_TORCH) {
                block.selection_shape.minimum[0] = block.selection_shape.minimum[2] = 0.4;
                block.selection_shape.maximum[0] = block.selection_shape.maximum[2] = 0.6;
                block.selection_shape.maximum[1] = 0.6;
            }
            if (block.id == MAAGE_BLOCK_SNOW) {
                block.collision_shape.maximum[1] = block.selection_shape.maximum[1] = 0.125;
                block.replaceable = 1;
            } else if (block.id == MAAGE_BLOCK_CACTUS) {
                block.collision_shape.minimum[0] = block.collision_shape.minimum[2] = 0.0625;
                block.collision_shape.maximum[0] = block.collision_shape.maximum[2] = 0.9375;
                block.selection_shape = block.collision_shape;
                block.collision_shape.maximum[1] = 0.9375;
            }
            if (block.id == MAAGE_BLOCK_LOG || block.id == MAAGE_BLOCK_PLANKS || block.id == MAAGE_BLOCK_CRAFTING_TABLE) block.required_tool = MAAGE_TOOL_AXE;
            else if (block.solid && block.id != MAAGE_BLOCK_LEAVES && block.id != MAAGE_BLOCK_SNOW &&
                block.id != MAAGE_BLOCK_ICE && block.id != MAAGE_BLOCK_CACTUS) {
                block.required_tool = MAAGE_TOOL_PICKAXE; block.harvest_requires_tool = 1;
                block.required_tool_level = block.id == MAAGE_BLOCK_OBSIDIAN ? 3U :
                    (block.id == MAAGE_BLOCK_GOLD_BLOCK ||
                     block.id == MAAGE_BLOCK_DIAMOND_BLOCK || block.id == MAAGE_BLOCK_EMERALD_BLOCK ||
                     block.id == MAAGE_BLOCK_GOLD_ORE || block.id == MAAGE_BLOCK_DIAMOND_ORE ||
                     block.id == MAAGE_BLOCK_REDSTONE_ORE || block.id == MAAGE_BLOCK_EMERALD_ORE ? 2U :
                     (block.id == MAAGE_BLOCK_IRON_BLOCK || block.id == MAAGE_BLOCK_LAPIS_BLOCK || block.id == MAAGE_BLOCK_IRON_ORE || block.id == MAAGE_BLOCK_LAPIS_ORE ? 1U : 0U));
            }
            if (block.id == MAAGE_BLOCK_SNOW_BLOCK) { block.drop_item_id = MAAGE_ITEM_SNOWBALL; block.drop_count = 4; }
            if (block.id == MAAGE_BLOCK_SNOW || block.id == MAAGE_BLOCK_SNOW_BLOCK) { block.required_tool = MAAGE_TOOL_SHOVEL; block.harvest_requires_tool = 1; }
            if (block.id == MAAGE_BLOCK_ICE) block.required_tool = MAAGE_TOOL_PICKAXE;
            if (block.mesh_shape == MAAGE_BLOCK_MESH_CROSS)
                block.required_tool = MAAGE_TOOL_AXE;
            strcpy(name, "minecraft:"); strcat(name, content[entry].name);
            if (maage_status_failed(result = maage_block_registry_register_block(registry, name, &block)) ||
                maage_status_failed(result = maage_content_register_state(registry, config, block.id, block.id,
                    block.id == MAAGE_BLOCK_TALL_GRASS ? 1U : (block.id == MAAGE_BLOCK_TORCH ? 5U : 0U),
                    MAAGE_CONTENT_TEXTURE_EXTRA + content[entry].texture))) goto failed;
        }
        if (maage_status_failed(result = maage_content_register_state(registry, config, MAAGE_STATE_SPRUCE_LOG, MAAGE_BLOCK_LOG, 1, 0)) ||
            maage_status_failed(result = maage_content_register_state(registry, config, MAAGE_STATE_BIRCH_LOG, MAAGE_BLOCK_LOG, 2, 0)) ||
            maage_status_failed(result = maage_content_register_state(registry, config, MAAGE_STATE_SPRUCE_LEAVES, MAAGE_BLOCK_LEAVES, 1, 0)) ||
            maage_status_failed(result = maage_content_register_state(registry, config, MAAGE_STATE_BIRCH_LEAVES, MAAGE_BLOCK_LEAVES, 2, 0))) goto failed;
        for (i = 1; i <= 2; ++i)
            if (maage_status_failed(result = maage_content_register_state(registry, config,
                MAAGE_STATE_SPRUCE_PLANKS + i - 1, MAAGE_BLOCK_PLANKS, (unsigned int)i, 0))) goto failed;
        for (i = 0; i < 4; ++i)
            if (maage_status_failed(result = maage_content_register_state(registry, config,
                MAAGE_STATE_TORCH_NEGATIVE_X + i, MAAGE_BLOCK_TORCH, (unsigned int)i + 1,
                MAAGE_CONTENT_TEXTURE_EXTRA + 18))) goto failed;
        for (i = 0; i < 3; ++i) {
            if (maage_status_failed(result = maage_content_register_state(registry, config,
                    MAAGE_STATE_LOG_X_BASE + i, MAAGE_BLOCK_LOG, (unsigned int)i | 4U, 0)) ||
                maage_status_failed(result = maage_content_register_state(registry, config,
                    MAAGE_STATE_LOG_Z_BASE + i, MAAGE_BLOCK_LOG, (unsigned int)i | 8U, 0))) goto failed;
        }
        for (i = 1; i < 16; ++i) {
            if (maage_status_failed(result = maage_content_register_state(registry, config, MAAGE_STATE_WATER_FLOW_BASE + i - 1,
                    MAAGE_BLOCK_WATER, (unsigned int)i, MAAGE_CONTENT_TEXTURE_WATER)) ||
                maage_status_failed(result = maage_content_register_state(registry, config, MAAGE_STATE_LAVA_FLOW_BASE + i - 1,
                    MAAGE_BLOCK_LAVA, (unsigned int)i, MAAGE_CONTENT_TEXTURE_EXTRA + 2))) goto failed;
        }
    }
    for (i = 0; i < MAAGE_BIOME_COUNT; ++i) {
        if (!biomes->builtin_names[i] || maage_status_failed(result = maage_content_register_state(registry, config,
                MAAGE_STATE_BIOME_GRASS(i), MAAGE_BLOCK_GRASS,
                (unsigned int)i + 1, MAAGE_CONTENT_TEXTURE_GRASS_TOP))) goto failed;
    }
    if (maage_status_failed(result = maage_block_registry_finalize(registry))) goto failed;
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

maage_status maage_content_create(const maage_content_config *config,
    maage_content **output, maage_status_trace *trace)
{
    maage_content *content;
    maage_status result;
    const maage_biome_definition *source;
    unsigned int id;
    if (!output) return maage_status_make(MAAGE_STATUS_INVALID_ARGUMENT,
        MAAGE_CONTEXT_REGISTRY, MAAGE_OPERATION_CREATE, 1);
    *output = NULL;
    content = (maage_content *)calloc(1, sizeof(*content));
    if (!content) return MAAGE_STATUS_PROPAGATE(trace,
        maage_status_make(MAAGE_STATUS_ALLOCATION_FAILED, MAAGE_CONTEXT_REGISTRY,
            MAAGE_OPERATION_CREATE, 0), MAAGE_CONTEXT_REGISTRY, MAAGE_OPERATION_CREATE);
    result = maage_biome_registry_init(&content->biomes);
    if (maage_status_failed(result)) goto failed;
    if (config && config->presentation_biomes) {
        for (id = 0; id < MAAGE_BIOME_COUNT; ++id) {
            source = maage_biome_registry_get(config->presentation_biomes, id);
            if (!source) {
                result = maage_status_make(MAAGE_STATUS_INVALID_ARGUMENT,
                    MAAGE_CONTEXT_REGISTRY, MAAGE_OPERATION_INITIALIZE, 0);
                goto failed;
            }
            memcpy(content->biomes.builtin_definitions[id].grass_color,
                source->grass_color, sizeof(source->grass_color));
            memcpy(content->biomes.builtin_definitions[id].foliage_color,
                source->foliage_color, sizeof(source->foliage_color));
        }
    }
    result = maage_biome_registry_finalize(&content->biomes);
    if (maage_status_failed(result)) goto failed;
    result = maage_content_blocks_create(config, &content->biomes,
        &content->blocks, trace);
    if (maage_status_failed(result)) goto failed;
    result = maage_item_registry_create_builtin(content->blocks, &content->items);
    if (maage_status_failed(result)) goto failed;
    result = maage_recipe_registry_create_builtin(content->items, &content->recipes);
    if (maage_status_failed(result)) goto failed;
    *output = content;
    return maage_status_make(MAAGE_STATUS_SUCCESS, 0, MAAGE_OPERATION_CREATE, 0);
failed:
    maage_content_destroy(content);
    return MAAGE_STATUS_PROPAGATE(trace, result,
        MAAGE_CONTEXT_REGISTRY, MAAGE_OPERATION_CREATE);
}

const maage_block_registry *maage_content_blocks(const maage_content *content)
{
    return content ? content->blocks : NULL;
}

const maage_biome_registry *maage_content_biomes(const maage_content *content)
{
    return content ? &content->biomes : NULL;
}

const maage_item_registry *maage_content_items(const maage_content *content)
{
    return content ? content->items : NULL;
}

const maage_recipe_registry *maage_content_recipes(const maage_content *content)
{
    return content ? content->recipes : NULL;
}

maage_status maage_content_destroy(maage_content *content)
{
    if (!content) return maage_status_make(MAAGE_STATUS_NOOP, 0,
        MAAGE_OPERATION_DESTROY, 0);
    /* No borrowed World/job/gameplay readers may remain at this boundary. */
    maage_recipe_registry_destroy(content->recipes);
    maage_item_registry_destroy(content->items);
    maage_block_registry_destroy(content->blocks);
    maage_biome_registry_destroy(&content->biomes);
    free(content);
    return maage_status_make(MAAGE_STATUS_SUCCESS, 0, MAAGE_OPERATION_DESTROY, 0);
}
