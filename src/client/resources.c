#include "resources.h"
#include "maage/minecraft_assets.h"
#include "maage/storage.h"
#include "maage/log.h"
#include "maage/items.h"
#include <limits.h>
#include <stdlib.h>
#include <string.h>

#define MAAGE_CLIENT_SOURCE_COUNT 57
static const char *const maage_client_texture_names[MAAGE_CLIENT_SOURCE_COUNT] = {
    "minecraft/textures/blocks/stone.png",
    "minecraft/textures/blocks/dirt.png",
    "minecraft/textures/blocks/grass_top.png",
    "minecraft/textures/blocks/grass_side.png",
    "minecraft/textures/blocks/grass_side_overlay.png",
    "minecraft/textures/colormap/grass.png",
    "minecraft/textures/blocks/bedrock.png",
    "minecraft/textures/blocks/water_still.png",
    "minecraft/textures/blocks/sand.png",
    "minecraft/textures/blocks/gravel.png",
    "minecraft/textures/blocks/sandstone_normal.png",
    "minecraft/textures/colormap/foliage.png",
    "minecraft/textures/blocks/sandstone_top.png",
    "minecraft/textures/blocks/sandstone_bottom.png",
    "minecraft/textures/blocks/destroy_stage_0.png",
    "minecraft/textures/blocks/destroy_stage_1.png",
    "minecraft/textures/blocks/destroy_stage_2.png",
    "minecraft/textures/blocks/destroy_stage_3.png",
    "minecraft/textures/blocks/destroy_stage_4.png",
    "minecraft/textures/blocks/destroy_stage_5.png",
    "minecraft/textures/blocks/destroy_stage_6.png",
    "minecraft/textures/blocks/destroy_stage_7.png",
    "minecraft/textures/blocks/destroy_stage_8.png",
    "minecraft/textures/blocks/destroy_stage_9.png",
    "minecraft/textures/blocks/cobblestone.png",
    "minecraft/textures/blocks/planks_oak.png",
    "minecraft/textures/blocks/lava_still.png",
    "minecraft/textures/blocks/gold_ore.png",
    "minecraft/textures/blocks/iron_ore.png",
    "minecraft/textures/blocks/coal_ore.png",
    "minecraft/textures/blocks/log_oak.png",
    "minecraft/textures/blocks/log_oak_top.png",
    "minecraft/textures/blocks/leaves_oak.png",
    "minecraft/textures/blocks/lapis_ore.png",
    "minecraft/textures/blocks/tallgrass.png",
    "minecraft/textures/blocks/deadbush.png",
    "minecraft/textures/blocks/flower_dandelion.png",
    "minecraft/textures/blocks/flower_rose.png",
    "minecraft/textures/blocks/mushroom_brown.png",
    "minecraft/textures/blocks/mushroom_red.png",
    "minecraft/textures/blocks/cobblestone_mossy.png",
    "minecraft/textures/blocks/obsidian.png",
    "minecraft/textures/blocks/torch_on.png",
    "minecraft/textures/blocks/diamond_ore.png",
    "minecraft/textures/blocks/redstone_ore.png",
    "minecraft/textures/blocks/snow.png",
    "minecraft/textures/blocks/cactus_side.png",
    "minecraft/textures/blocks/cactus_top.png",
    "minecraft/textures/blocks/cactus_bottom.png",
    "minecraft/textures/blocks/emerald_ore.png",
    "minecraft/textures/blocks/log_spruce.png",
    "minecraft/textures/blocks/log_spruce_top.png",
    "minecraft/textures/blocks/leaves_spruce.png",
    "minecraft/textures/blocks/log_birch.png",
    "minecraft/textures/blocks/log_birch_top.png",
    "minecraft/textures/blocks/leaves_birch.png",
    "minecraft/textures/blocks/ice.png"
};

static void maage_client_colormap(const maage_image *map,
    double temperature, double rainfall, float color[3])
{
    const unsigned char *pixel;
    int x, y, channel;
    if (temperature < 0) temperature = 0;
    if (temperature > 1) temperature = 1;
    if (rainfall < 0) rainfall = 0;
    if (rainfall > 1) rainfall = 1;
    rainfall *= temperature;
    x = (int)((1.0 - temperature) * 255.0);
    y = (int)((1.0 - rainfall) * 255.0);
    pixel = map->pixels + (size_t)y * map->stride + (size_t)x * 4;
    for (channel = 0; channel < 3; ++channel)
        color[channel] = (float)pixel[channel] / 255.0f;
}

int maage_client_resources_load(const char *application_data,
    maage_client_resources *resources, maage_biome_registry *biomes)
{
    static const int atlas_sources[11] = {0, 1, 2, 3, 6, 7, 8, 9, 10, 12, 13};
    maage_image sources[MAAGE_CLIENT_SOURCE_COUNT];
    maage_biome_definition *biome;
    char path[MAAGE_PATH_CAPACITY];
    const float *tint;
    unsigned char *pixel, *source, *overlay;
    unsigned int channel, alpha, value;
    int i, j, x, y, sx, sy, edge, tile, source_index, biome_id, stage, ok = 0;
    if (!resources || !biomes || biomes->finalized) return 0;
    memset(sources, 0, sizeof(sources));
    memset(resources, 0, sizeof(*resources));
    /* External names are consumed here only; mesh faces retain numeric UVs. */
    for (i = 0; i < MAAGE_CLIENT_SOURCE_COUNT; ++i) {
        if (!maage_minecraft_asset_path(application_data,
                maage_client_texture_names[i], path, sizeof(path)) ||
            !maage_image_load_png(path, &sources[i])) {
            maage_log_error("maage.client.resources", "Missing or invalid Minecraft 1.7.10 PNG: %s. Prepare/repair resources with the Launcher.", maage_client_texture_names[i]);
            goto done;
        }
        maage_log_info("maage.client.resources", "Loaded %s (%dx%d).", maage_client_texture_names[i], sources[i].width, sources[i].height);
    }
    edge = sources[0].width;
    for (i = 0; i < MAAGE_CLIENT_SOURCE_COUNT; ++i) {
        if (i == 5 || i == 11) {
            if (sources[i].width == 256 && sources[i].height == 256) continue;
        } else if (sources[i].width == edge &&
            (sources[i].height == edge || ((i == 7 || i == 26) && sources[i].height >= edge && sources[i].height % edge == 0))) continue;
        maage_log_error("maage.client.resources", "Invalid block texture or biome colormap dimensions: %s.", maage_client_texture_names[i]);
        goto done;
    }
    for (i = 0; i < MAAGE_BIOME_COUNT; ++i) {
        if (!biomes->occupied[i]) goto done;
        biome = &biomes->definitions[i];
        maage_client_colormap(&sources[5], biome->temperature, biome->rainfall, biome->grass_color);
        maage_client_colormap(&sources[11], biome->temperature, biome->rainfall, biome->foliage_color);
    }
    if (edge > INT_MAX - 2 || edge + 2 > INT_MAX / MAAGE_CLIENT_TEXTURE_COUNT ||
        edge + 2 > INT_MAX / (1 + MAAGE_CLIENT_BREAK_STAGES)) goto done;
    tile = edge + 2;
    resources->image.width = tile * MAAGE_CLIENT_TEXTURE_COUNT;
    resources->image.height = tile * (1 + MAAGE_CLIENT_BREAK_STAGES);
    if ((size_t)resources->image.width > (size_t)-1 / 4) goto done;
    resources->image.stride = (size_t)resources->image.width * 4;
    if ((size_t)resources->image.height > (size_t)-1 / resources->image.stride) goto done;
    resources->image.pixels = (unsigned char *)malloc(resources->image.stride * (size_t)resources->image.height);
    if (!resources->image.pixels) goto done;
    for (i = 0; i < MAAGE_CLIENT_TEXTURE_COUNT; ++i) {
        biome_id = i >= 11 && i < MAAGE_CLIENT_TEXTURE_EXTRA ? (i - 11) / 2 : MAAGE_BIOME_PLAINS;
        source_index = i >= MAAGE_CLIENT_TEXTURE_EXTRA ? 24 + i - MAAGE_CLIENT_TEXTURE_EXTRA :
            (i >= 11 ? ((i - 11) % 2 ? 3 : 2) : atlas_sources[i]);
        if (i == MAAGE_CLIENT_TEXTURE_EXTRA + 28) biome_id = MAAGE_BIOME_TAIGA;
        else if (i == MAAGE_CLIENT_TEXTURE_EXTRA + 31) biome_id = MAAGE_BIOME_FOREST;
        tint = source_index == 32 || source_index == 52 || source_index == 55 ?
            biomes->definitions[biome_id].foliage_color : biomes->definitions[biome_id].grass_color;
        for (y = 0; y < tile; ++y) {
            sy = y == 0 ? 0 : (y > edge ? edge - 1 : y - 1);
            for (x = 0; x < tile; ++x) {
                sx = x == 0 ? 0 : (x > edge ? edge - 1 : x - 1);
                pixel = resources->image.pixels + (size_t)y * resources->image.stride + (size_t)(i * tile + x) * 4;
                source = sources[source_index].pixels + (size_t)sy * sources[source_index].stride + (size_t)sx * 4;
                memcpy(pixel, source, 4);
                if (source_index == 2 || source_index == 32 || source_index == 34 || source_index == 52 || source_index == 55)
                    for (channel = 0; channel < 3; ++channel)
                        pixel[channel] = (unsigned char)((float)pixel[channel] * tint[channel]);
                if (source_index == 3) {
                    overlay = sources[4].pixels + (size_t)sy * sources[4].stride + (size_t)sx * 4;
                    alpha = overlay[3];
                    for (channel = 0; channel < 3; ++channel) {
                        value = (unsigned int)((float)overlay[channel] * tint[channel]);
                        pixel[channel] = (unsigned char)((value * alpha + (unsigned int)pixel[channel] * (255 - alpha) + 127) / 255);
                    }
                }
                if (source_index == 7) {
                    /* Static first animation frame; opaque water avoids ordering
                     * artifacts until transparent materials are implemented. */
                    pixel[0] = (unsigned char)((unsigned int)pixel[0] * 63 / 255);
                    pixel[1] = (unsigned char)((unsigned int)pixel[1] * 118 / 255);
                    pixel[2] = (unsigned char)((unsigned int)pixel[2] * 228 / 255);
                    pixel[3] = 255;
                } else if (source_index == 26) pixel[3] = 255;
            }
        }
        resources->uv[i][0] = (float)(i * tile + 1) / resources->image.width;
        resources->uv[i][1] = 1.0f / resources->image.height;
        resources->uv[i][2] = (float)(i * tile + 1 + edge) / resources->image.width;
        resources->uv[i][3] = (float)(1 + edge) / resources->image.height;
        /* Composite cracks at initialization so the existing opaque mesh path
         * can draw damage without acquiring resources during interaction. */
        for (stage = 0; stage < MAAGE_CLIENT_BREAK_STAGES; ++stage) {
            for (y = 0; y < tile; ++y) {
                sy = y == 0 ? 0 : (y > edge ? edge - 1 : y - 1);
                for (x = 0; x < tile; ++x) {
                    sx = x == 0 ? 0 : (x > edge ? edge - 1 : x - 1);
                    source = resources->image.pixels + (size_t)y * resources->image.stride + (size_t)(i * tile + x) * 4;
                    pixel = source + (size_t)(stage + 1) * tile * resources->image.stride;
                    overlay = sources[14 + stage].pixels + (size_t)sy * sources[14 + stage].stride + (size_t)sx * 4;
                    for (channel = 0; channel < 3; ++channel)
                        pixel[channel] = (unsigned char)((unsigned int)source[channel] *
                            (255U - overlay[3] + (unsigned int)overlay[channel] * overlay[3] / 255U) / 255U);
                    pixel[3] = source[3];
                }
            }
            memcpy(resources->breaking_uv[stage][i], resources->uv[i], sizeof(resources->uv[i]));
            resources->breaking_uv[stage][i][1] += (float)(stage + 1) / (1 + MAAGE_CLIENT_BREAK_STAGES);
            resources->breaking_uv[stage][i][3] += (float)(stage + 1) / (1 + MAAGE_CLIENT_BREAK_STAGES);
        }
    }
    resources->atlas.pixels = resources->image.pixels;
    resources->atlas.width = resources->image.width;
    resources->atlas.height = resources->image.height;
    resources->atlas.stride = resources->image.stride;
    ok = 1;
done:
    for (j = 0; j < MAAGE_CLIENT_SOURCE_COUNT; ++j) maage_image_destroy(&sources[j]);
    if (!ok) maage_client_resources_destroy(resources);
    return ok;
}

static int maage_client_register_state(maage_block_registry *registry,
    const maage_client_resources *resources, maage_block_state_id id,
    maage_block_id block, unsigned int metadata, int texture, int grass_biome)
{
    maage_block_state_definition state;
    int face, channel, selected, level;
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
            selected = grass_biome >= 0 ? MAAGE_CLIENT_TEXTURE_BIOME_SIDE(grass_biome) : MAAGE_CLIENT_TEXTURE_GRASS_SIDE;
            if (face == MAAGE_FACE_NEGATIVE_Y) selected = MAAGE_CLIENT_TEXTURE_DIRT;
            if (face == MAAGE_FACE_POSITIVE_Y)
                selected = grass_biome >= 0 ? MAAGE_CLIENT_TEXTURE_BIOME_TOP(grass_biome) : MAAGE_CLIENT_TEXTURE_GRASS_TOP;
        }
        if (block == MAAGE_BLOCK_SANDSTONE) {
            if (face == MAAGE_FACE_POSITIVE_Y) selected = MAAGE_CLIENT_TEXTURE_SANDSTONE_TOP;
            if (face == MAAGE_FACE_NEGATIVE_Y) selected = MAAGE_CLIENT_TEXTURE_SANDSTONE_BOTTOM;
        }
        if (block == MAAGE_BLOCK_LOG) {
            selected = MAAGE_CLIENT_TEXTURE_EXTRA + (metadata == 1 ? 26 : (metadata == 2 ? 29 : 6));
            if (face == MAAGE_FACE_NEGATIVE_Y || face == MAAGE_FACE_POSITIVE_Y) ++selected;
        } else if (block == MAAGE_BLOCK_LEAVES)
            selected = MAAGE_CLIENT_TEXTURE_EXTRA + (metadata == 1 ? 28 : (metadata == 2 ? 31 : 8));
        else if (block == MAAGE_BLOCK_CACTUS) {
            if (face == MAAGE_FACE_POSITIVE_Y) selected = MAAGE_CLIENT_TEXTURE_EXTRA + 23;
            if (face == MAAGE_FACE_NEGATIVE_Y) selected = MAAGE_CLIENT_TEXTURE_EXTRA + 24;
        }
        state.faces[face].texture_id = (maage_texture_id)selected;
        memcpy(state.faces[face].uv, resources->uv[selected], sizeof(state.faces[face].uv));
        for (channel = 0; channel < 4; ++channel) state.faces[face].tint[channel] = 1.0f;
    }
    return maage_block_registry_register_state(registry, &state) == MAAGE_REGISTRY_OK;
}

maage_block_registry *maage_client_register_blocks(
    const maage_client_resources *resources, const maage_biome_registry *biomes)
{
    static const char *const names[] = {"minecraft:air", "minecraft:stone", "minecraft:grass", "minecraft:dirt", "minecraft:bedrock", "minecraft:water", "minecraft:sand", "minecraft:gravel", "minecraft:sandstone"};
    static const maage_block_id ids[] = {0, 1, 2, 3, 7, 9, 12, 13, 24};
    static const int textures[] = {0, 0, 2, 1, 4, 5, 6, 7, 8};
    static const double hardness[] = {0, 1.5, 0.6, 0.5, -1, 100, 0.5, 0.6, 0.8};
    static const unsigned long drops[] = {0, 1, 3, 3, 0, 0, 12, 13, 24};
    maage_block_registry *registry;
    maage_block_definition block;
    int i;
    if (!resources || !biomes) return NULL;
    registry = maage_block_registry_create(0, 0);
    if (!registry) return NULL;
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
        if (maage_block_registry_register_block(registry, names[i], &block) != MAAGE_REGISTRY_OK ||
            !maage_client_register_state(registry, resources, ids[i], ids[i], 0, textures[i], -1)) goto failed;
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
            if (maage_block_registry_register_block(registry, name, &block) != MAAGE_REGISTRY_OK ||
                !maage_client_register_state(registry, resources, block.id, block.id,
                    block.id == MAAGE_BLOCK_TALL_GRASS ? 1U : 0U, MAAGE_CLIENT_TEXTURE_EXTRA + content[entry].texture, -1)) goto failed;
        }
        if (!maage_client_register_state(registry, resources, MAAGE_STATE_SPRUCE_LOG, MAAGE_BLOCK_LOG, 1, 0, -1) ||
            !maage_client_register_state(registry, resources, MAAGE_STATE_BIRCH_LOG, MAAGE_BLOCK_LOG, 2, 0, -1) ||
            !maage_client_register_state(registry, resources, MAAGE_STATE_SPRUCE_LEAVES, MAAGE_BLOCK_LEAVES, 1, 0, -1) ||
            !maage_client_register_state(registry, resources, MAAGE_STATE_BIRCH_LEAVES, MAAGE_BLOCK_LEAVES, 2, 0, -1)) goto failed;
        for (i = 1; i < 16; ++i) {
            if (!maage_client_register_state(registry, resources, MAAGE_STATE_WATER_FLOW_BASE + i - 1,
                    MAAGE_BLOCK_WATER, (unsigned int)i, MAAGE_CLIENT_TEXTURE_WATER, -1) ||
                !maage_client_register_state(registry, resources, MAAGE_STATE_LAVA_FLOW_BASE + i - 1,
                    MAAGE_BLOCK_LAVA, (unsigned int)i, MAAGE_CLIENT_TEXTURE_EXTRA + 2, -1)) goto failed;
        }
    }
    for (i = 0; i < MAAGE_BIOME_COUNT; ++i) {
        if (!biomes->occupied[i] || !maage_client_register_state(registry, resources,
                MAAGE_STATE_BIOME_GRASS(i), MAAGE_BLOCK_GRASS,
                (unsigned int)i + 1, MAAGE_CLIENT_TEXTURE_GRASS_TOP, i)) goto failed;
    }
    if (maage_block_registry_finalize(registry) != MAAGE_REGISTRY_OK) goto failed;
    maage_log_info("maage.client.world", "Finalized %lu block types / %lu states; direct numeric runtime indexing.",
        (unsigned long)maage_block_registry_block_count(registry), (unsigned long)maage_block_registry_state_count(registry));
    return registry;
failed:
    maage_block_registry_destroy(registry);
    return NULL;
}

void maage_client_resources_destroy(maage_client_resources *resources)
{
    if (!resources) return;
    maage_image_destroy(&resources->image);
    memset(resources, 0, sizeof(*resources));
}
