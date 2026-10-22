#include "maage/content.h"
#include <stdlib.h>
#include <string.h>

/* Shared 1.7.10 logical definitions are extracted unchanged from the Client.
 * Headless and graphical initialization visit the same numeric registration
 * path; only the optional initialization-time presentation resolver differs. */
static maage_status maage_content_register_state(maage_block_registry *registry,
    const maage_content_config *config, maage_block_state_id id,
    maage_block_id block, unsigned int metadata, int texture, int grass_biome)
{
    maage_block_state_definition state;
    int face, channel, selected, level;
    maage_status result;
    memset(&state, 0, sizeof(state));
    state.id = id; state.block_id = block; state.metadata = metadata;
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
            selected = grass_biome >= 0 ? MAAGE_CONTENT_TEXTURE_BIOME_SIDE(grass_biome) : MAAGE_CONTENT_TEXTURE_GRASS_SIDE;
            if (face == MAAGE_FACE_NEGATIVE_Y) selected = MAAGE_CONTENT_TEXTURE_DIRT;
            if (face == MAAGE_FACE_POSITIVE_Y)
                selected = grass_biome >= 0 ? MAAGE_CONTENT_TEXTURE_BIOME_TOP(grass_biome) : MAAGE_CONTENT_TEXTURE_GRASS_TOP;
        }
        if (block == MAAGE_BLOCK_SANDSTONE) {
            if (face == MAAGE_FACE_POSITIVE_Y) selected = MAAGE_CONTENT_TEXTURE_SANDSTONE_TOP;
            if (face == MAAGE_FACE_NEGATIVE_Y) selected = MAAGE_CONTENT_TEXTURE_SANDSTONE_BOTTOM;
        }
        if (block == MAAGE_BLOCK_LOG) {
            selected = MAAGE_CONTENT_TEXTURE_EXTRA + (metadata == 1 ? 26 : (metadata == 2 ? 29 : 6));
            if (face == MAAGE_FACE_NEGATIVE_Y || face == MAAGE_FACE_POSITIVE_Y) ++selected;
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

maage_status maage_content_blocks_create(
    const maage_content_config *config, const maage_biome_registry *biomes,
    maage_block_registry **output, maage_status_trace *trace)
{
    static const char *const names[] = {"minecraft:air", "minecraft:stone", "minecraft:grass", "minecraft:dirt", "minecraft:bedrock", "minecraft:water", "minecraft:sand", "minecraft:gravel", "minecraft:sandstone"};
    static const maage_block_id ids[] = {0, 1, 2, 3, 7, 9, 12, 13, 24};
    static const int textures[] = {0, 0, 2, 1, 4, 5, 6, 7, 8};
    static const double hardness[] = {0, 1.5, 0.6, 0.5, -1, 100, 0.5, 0.6, 0.8};
    static const unsigned long drops[] = {0, 1, 3, 3, 0, 0, 12, 13, 24};
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
        block.emitted_light = 0;
        block.selectable = block.solid;
        block.replaceable = !block.solid;
        block.hardness = hardness[i];
        block.drop_item_id = drops[i]; block.drop_count = drops[i] ? 1U : 0U;
        if (ids[i] == MAAGE_BLOCK_STONE || ids[i] == MAAGE_BLOCK_SANDSTONE) {
            block.required_tool = MAAGE_TOOL_PICKAXE; block.required_tool_level = 1;
        } else if (ids[i] == MAAGE_BLOCK_GRASS || ids[i] == MAAGE_BLOCK_DIRT ||
            ids[i] == MAAGE_BLOCK_SAND || ids[i] == MAAGE_BLOCK_GRAVEL)
            block.required_tool = MAAGE_TOOL_SHOVEL;
        if (block.solid) {
            block.collision_shape.maximum[0] = block.collision_shape.maximum[1] = block.collision_shape.maximum[2] = 1;
            block.selection_shape = block.collision_shape;
        }
        if (maage_status_failed(result = maage_block_registry_register_block(registry, names[i], &block)) ||
            maage_status_failed(result = maage_content_register_state(registry, config, ids[i], ids[i], 0, textures[i], -1))) goto failed;
    }
    {
        static const struct {
            maage_block_id id; const char *name;
            int texture, solid, opaque, cross; double hardness;
            unsigned int emission;
        } content[] = {
            {4,"cobblestone",0,1,1,0,2,0}, {5,"planks",1,1,1,0,2,0},
            {11,"lava",2,0,0,0,100,15}, {14,"gold_ore",3,1,1,0,3,0},
            {15,"iron_ore",4,1,1,0,3,0}, {16,"coal_ore",5,1,1,0,3,0},
            {17,"log",6,1,1,0,2,0}, {18,"leaves",8,1,0,0,0.2,0},
            {21,"lapis_ore",9,1,1,0,3,0}, {31,"tall_grass",10,0,0,1,0,0},
            {32,"dead_bush",11,0,0,1,0,0}, {37,"dandelion",12,0,0,1,0,0},
            {38,"poppy",13,0,0,1,0,0}, {39,"brown_mushroom",14,0,0,1,0,0},
            {40,"red_mushroom",15,0,0,1,0,0}, {48,"mossy_cobblestone",16,1,1,0,2,0},
            {49,"obsidian",17,1,1,0,50,0}, {50,"torch",18,0,0,1,0,14},
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
            block.emitted_light = content[entry].emission;
            block.mesh_shape = content[entry].cross ? MAAGE_BLOCK_MESH_CROSS : MAAGE_BLOCK_MESH_CUBE;
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
                if (block.id == MAAGE_BLOCK_TORCH) {
                    block.selection_shape.minimum[0] = block.selection_shape.minimum[2] = 0.4;
                    block.selection_shape.maximum[0] = block.selection_shape.maximum[2] = 0.6;
                    block.selection_shape.maximum[1] = 0.625;
                } else if (block.id != MAAGE_BLOCK_TALL_GRASS && block.id != MAAGE_BLOCK_DEAD_BUSH) {
                    block.selection_shape.minimum[0] = block.selection_shape.minimum[2] = 0.3;
                    block.selection_shape.maximum[0] = block.selection_shape.maximum[2] = 0.7;
                    block.selection_shape.maximum[1] = block.id == MAAGE_BLOCK_BROWN_MUSHROOM ||
                        block.id == MAAGE_BLOCK_RED_MUSHROOM ? 0.4 : 0.6;
                }
            }
            if (block.id == MAAGE_BLOCK_SNOW) {
                block.collision_shape.maximum[1] = block.selection_shape.maximum[1] = 0.125;
                block.replaceable = 1;
            } else if (block.id == MAAGE_BLOCK_CACTUS) {
                block.collision_shape.minimum[0] = block.collision_shape.minimum[2] = 0.0625;
                block.collision_shape.maximum[0] = block.collision_shape.maximum[2] = 0.9375;
                block.selection_shape = block.collision_shape;
            }
            if (block.id == MAAGE_BLOCK_LOG || block.id == MAAGE_BLOCK_PLANKS) block.required_tool = MAAGE_TOOL_AXE;
            else if (block.solid && block.id != MAAGE_BLOCK_LEAVES && block.id != MAAGE_BLOCK_SNOW &&
                block.id != MAAGE_BLOCK_ICE && block.id != MAAGE_BLOCK_CACTUS) {
                block.required_tool = MAAGE_TOOL_PICKAXE; block.required_tool_level = block.id == MAAGE_BLOCK_OBSIDIAN ? 3U : 1U;
            }
            strcpy(name, "minecraft:"); strcat(name, content[entry].name);
            if (maage_status_failed(result = maage_block_registry_register_block(registry, name, &block)) ||
                maage_status_failed(result = maage_content_register_state(registry, config, block.id, block.id,
                    block.id == MAAGE_BLOCK_TALL_GRASS ? 1U : 0U, MAAGE_CONTENT_TEXTURE_EXTRA + content[entry].texture, -1))) goto failed;
        }
        if (maage_status_failed(result = maage_content_register_state(registry, config, MAAGE_STATE_SPRUCE_LOG, MAAGE_BLOCK_LOG, 1, 0, -1)) ||
            maage_status_failed(result = maage_content_register_state(registry, config, MAAGE_STATE_BIRCH_LOG, MAAGE_BLOCK_LOG, 2, 0, -1)) ||
            maage_status_failed(result = maage_content_register_state(registry, config, MAAGE_STATE_SPRUCE_LEAVES, MAAGE_BLOCK_LEAVES, 1, 0, -1)) ||
            maage_status_failed(result = maage_content_register_state(registry, config, MAAGE_STATE_BIRCH_LEAVES, MAAGE_BLOCK_LEAVES, 2, 0, -1))) goto failed;
        for (i = 1; i < 16; ++i) {
            if (maage_status_failed(result = maage_content_register_state(registry, config, MAAGE_STATE_WATER_FLOW_BASE + i - 1,
                    MAAGE_BLOCK_WATER, (unsigned int)i, MAAGE_CONTENT_TEXTURE_WATER, -1)) ||
                maage_status_failed(result = maage_content_register_state(registry, config, MAAGE_STATE_LAVA_FLOW_BASE + i - 1,
                    MAAGE_BLOCK_LAVA, (unsigned int)i, MAAGE_CONTENT_TEXTURE_EXTRA + 2, -1))) goto failed;
        }
    }
    for (i = 0; i < MAAGE_BIOME_COUNT; ++i) {
        if (!biomes->builtin_names[i] || maage_status_failed(result = maage_content_register_state(registry, config,
                MAAGE_STATE_BIOME_GRASS(i), MAAGE_BLOCK_GRASS,
                (unsigned int)i + 1, MAAGE_CONTENT_TEXTURE_GRASS_TOP, i))) goto failed;
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
            memcpy(content->biomes.definitions[id].grass_color,
                source->grass_color, sizeof(source->grass_color));
            memcpy(content->biomes.definitions[id].foliage_color,
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

maage_status maage_content_destroy(maage_content *content)
{
    if (!content) return maage_status_make(MAAGE_STATUS_NOOP, 0,
        MAAGE_OPERATION_DESTROY, 0);
    /* No borrowed World/job/gameplay readers may remain at this boundary. */
    maage_item_registry_destroy(content->items);
    maage_block_registry_destroy(content->blocks);
    maage_biome_registry_destroy(&content->biomes);
    free(content);
    return maage_status_make(MAAGE_STATUS_SUCCESS, 0, MAAGE_OPERATION_DESTROY, 0);
}
