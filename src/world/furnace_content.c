#include "world_internal.h"
#include <string.h>
#include <stddef.h>
#include <math.h>
#define ID_OFFSET(name) offsetof(maage_builtin_ids, name)
static unsigned long *furnace_id(maage_builtin_ids *ids, size_t offset) {
    return (unsigned long *) ((unsigned char *) ids + offset);
}
maage_status maage_furnace_content_blocks(maage_block_registry *registry, const maage_content_config *config) {
    static const struct {
        size_t block, state;
        const char *name;
        double hardness;
        int opaque;
    } definitions[] = {
        {ID_OFFSET(block_glass), ID_OFFSET(state_glass), "minecraft:glass", 0.3, 0},
        {ID_OFFSET(block_clay), ID_OFFSET(state_clay), "minecraft:clay", 0.6, 1},
        {ID_OFFSET(block_hardened_clay), ID_OFFSET(state_hardened_clay), "minecraft:hardened_clay", 1.25, 1},
        {ID_OFFSET(block_netherrack), ID_OFFSET(state_netherrack), "minecraft:netherrack", 0.4, 1},
        {ID_OFFSET(block_nether_brick), ID_OFFSET(state_nether_brick), "minecraft:nether_brick", 2, 1},
        {ID_OFFSET(block_quartz_ore), ID_OFFSET(state_quartz_ore), "minecraft:quartz_ore", 3, 1},
        {ID_OFFSET(block_log2), ID_OFFSET(state_log2), "minecraft:log2", 2, 1},
        {ID_OFFSET(block_chest), ID_OFFSET(state_chest), "minecraft:chest", 2, 1},
        {ID_OFFSET(block_trapped_chest), ID_OFFSET(state_trapped_chest), "minecraft:trapped_chest", 2, 1},
        {ID_OFFSET(block_bookshelf), ID_OFFSET(state_bookshelf), "minecraft:bookshelf", 2, 1},
        {ID_OFFSET(block_fence), ID_OFFSET(state_fence), "minecraft:fence", 2, 1},
        {ID_OFFSET(block_fence_gate), ID_OFFSET(state_fence_gate), "minecraft:fence_gate", 2, 1},
        {ID_OFFSET(block_wooden_pressure_plate), ID_OFFSET(state_wooden_pressure_plate), "minecraft:wooden_pressure_plate", 2, 1},
        {ID_OFFSET(block_trapdoor), ID_OFFSET(state_trapdoor), "minecraft:trapdoor", 2, 1},
        {ID_OFFSET(block_wooden_slab), ID_OFFSET(state_wooden_slab), "minecraft:wooden_slab", 2, 1},
        {ID_OFFSET(block_oak_stairs), ID_OFFSET(state_oak_stairs), "minecraft:oak_stairs", 2, 1},
        {ID_OFFSET(block_spruce_stairs), ID_OFFSET(state_spruce_stairs), "minecraft:spruce_stairs", 2, 1},
        {ID_OFFSET(block_birch_stairs), ID_OFFSET(state_birch_stairs), "minecraft:birch_stairs", 2, 1},
        {ID_OFFSET(block_jungle_stairs), ID_OFFSET(state_jungle_stairs), "minecraft:jungle_stairs", 2, 1},
        {ID_OFFSET(block_acacia_stairs), ID_OFFSET(state_acacia_stairs), "minecraft:acacia_stairs", 2, 1},
        {ID_OFFSET(block_dark_oak_stairs), ID_OFFSET(state_dark_oak_stairs), "minecraft:dark_oak_stairs", 2, 1},
        {ID_OFFSET(block_jukebox), ID_OFFSET(state_jukebox), "minecraft:jukebox", 2, 1},
        {ID_OFFSET(block_noteblock), ID_OFFSET(state_noteblock), "minecraft:noteblock", 2, 1},
        {ID_OFFSET(block_daylight_detector), ID_OFFSET(state_daylight_detector), "minecraft:daylight_detector", 2, 1},
        {ID_OFFSET(block_brown_mushroom_block), ID_OFFSET(state_brown_mushroom_block), "minecraft:brown_mushroom_block", 2, 1},
        {ID_OFFSET(block_red_mushroom_block), ID_OFFSET(state_red_mushroom_block), "minecraft:red_mushroom_block", 2, 1}};
    maage_builtin_ids *ids = maage_block_registry_initialize_ids(registry);
    maage_block_definition block;
    maage_status result;
    size_t i;
    unsigned int lit, facing;
    result = maage_content_register_state(registry, config, &ids->state_red_sand, ids->block_sand, 1, MAAGE_CONTENT_TEXTURE_FURNACE_VARIANTS);
    if (maage_status_failed(result)) return result;
    for (facing = 3; facing < 6; ++facing) {
        result = maage_content_register_state(registry, config, &ids->planks[facing], ids->block_planks, facing, 0);
        if (maage_status_failed(result)) return result;
    }
    for (facing = 3; facing < 12; facing += 4) {
        result = maage_content_register_state(registry, config, &ids->logs[facing], ids->block_log, facing, 0);
        if (maage_status_failed(result)) return result;
    }
    for (lit = 0; lit < 2; ++lit) {
        memset(&block, 0, sizeof(block));
        block.material_solid = block.opaque = block.solid = block.selectable = 1;
        block.light_opacity = 15;
        block.emitted_light = lit ? 13 : 0;
        block.hardness = 3.5;
        block.required_tool = MAAGE_TOOL_PICKAXE;
        block.harvest_requires_tool = 1;
        block.drop_count = 1;
        block.collision_shape.maximum[0] = block.collision_shape.maximum[1] = block.collision_shape.maximum[2] = 1;
        block.selection_shape = block.collision_shape;
        result = maage_block_registry_register_block(registry, lit ? "minecraft:lit_furnace" : "minecraft:furnace", &block);
        if (maage_status_failed(result)) return result;
        if (lit) ids->block_lit_furnace = block.id;
        else ids->block_furnace = block.id;
        for (facing = 0; facing < 4; ++facing) {
            result = maage_content_register_state(registry, config, &ids->furnaces[lit][facing], block.id, facing, MAAGE_CONTENT_TEXTURE_FURNACE);
            if (maage_status_failed(result)) return result;
        }
    }
    for (i = 0; i < sizeof(definitions) / sizeof(definitions[0]); ++i) {
        memset(&block, 0, sizeof(block));
        block.material_solid = block.solid = block.selectable = 1;
        block.opaque = definitions[i].opaque;
        block.light_opacity = block.opaque ? 15 : 0;
        block.hardness = definitions[i].hardness;
        block.drop_count = 1;
        block.required_tool = i == 1 ? MAAGE_TOOL_SHOVEL : (i >= 6 ? MAAGE_TOOL_AXE : MAAGE_TOOL_PICKAXE);
        block.harvest_requires_tool = i >= 2 && i <= 5;
        block.collision_shape.maximum[0] = block.collision_shape.maximum[1] = block.collision_shape.maximum[2] = 1;
        block.selection_shape = block.collision_shape;
        result = maage_block_registry_register_block(registry, definitions[i].name, &block);
        if (maage_status_failed(result)) return result;
        *furnace_id(ids, definitions[i].block) = block.id;
        result = maage_content_register_state(registry, config, furnace_id(ids, definitions[i].state), block.id, 0, MAAGE_CONTENT_TEXTURE_FURNACE_EXTRA + (int) i);
        if (maage_status_failed(result)) return result;
    }
    ids->logs2[0] = ids->state_log2;
    for (lit = 0; lit < 3; ++lit)
        for (facing = 0; facing < 2; ++facing) {
            unsigned int metadata = lit * 4U + facing;
            if (!metadata) continue;
            result = maage_content_register_state(registry, config, &ids->logs2[metadata], ids->block_log2, metadata, 0);
            if (maage_status_failed(result)) return result;
        }
    return maage_status_make(MAAGE_STATUS_SUCCESS, 0, MAAGE_OPERATION_REGISTER, 0);
}
static maage_status furnace_placement(const maage_item_definition *item, unsigned int metadata, maage_block_face face, double yaw, maage_block_state_id *state);
maage_status maage_furnace_content_items(maage_item_registry *registry, const maage_block_registry *blocks, maage_builtin_ids *ids) {
    static const struct {
        size_t block, state, item;
        const char *name, *label;
        unsigned int fuel;
    } definitions[] = {
        {ID_OFFSET(block_glass), ID_OFFSET(state_glass), ID_OFFSET(item_glass), "minecraft:glass", "Glass", 0},
        {ID_OFFSET(block_clay), ID_OFFSET(state_clay), ID_OFFSET(item_clay), "minecraft:clay", "Clay", 0},
        {ID_OFFSET(block_hardened_clay), ID_OFFSET(state_hardened_clay), ID_OFFSET(item_hardened_clay), "minecraft:hardened_clay", "Hardened Clay", 0},
        {ID_OFFSET(block_netherrack), ID_OFFSET(state_netherrack), ID_OFFSET(item_netherrack), "minecraft:netherrack", "Netherrack", 0},
        {ID_OFFSET(block_nether_brick), ID_OFFSET(state_nether_brick), ID_OFFSET(item_nether_brick), "minecraft:nether_brick", "Nether Bricks", 0},
        {ID_OFFSET(block_quartz_ore), ID_OFFSET(state_quartz_ore), ID_OFFSET(item_quartz_ore), "minecraft:quartz_ore", "Nether Quartz Ore", 0},
        {ID_OFFSET(block_log2), ID_OFFSET(state_log2), ID_OFFSET(item_log2), "minecraft:log2", "Acacia Log", 300},
        {ID_OFFSET(block_chest), ID_OFFSET(state_chest), ID_OFFSET(item_chest), "minecraft:chest", "Chest", 300},
        {ID_OFFSET(block_trapped_chest), ID_OFFSET(state_trapped_chest), ID_OFFSET(item_trapped_chest), "minecraft:trapped_chest", "Trapped Chest", 300},
        {ID_OFFSET(block_bookshelf), ID_OFFSET(state_bookshelf), ID_OFFSET(item_bookshelf), "minecraft:bookshelf", "Bookshelf", 300},
        {ID_OFFSET(block_fence), ID_OFFSET(state_fence), ID_OFFSET(item_fence), "minecraft:fence", "Fence", 300},
        {ID_OFFSET(block_fence_gate), ID_OFFSET(state_fence_gate), ID_OFFSET(item_fence_gate), "minecraft:fence_gate", "Fence Gate", 300},
        {ID_OFFSET(block_wooden_pressure_plate), ID_OFFSET(state_wooden_pressure_plate), ID_OFFSET(item_wooden_pressure_plate), "minecraft:wooden_pressure_plate", "Wooden Pressure Plate", 300},
        {ID_OFFSET(block_trapdoor), ID_OFFSET(state_trapdoor), ID_OFFSET(item_trapdoor), "minecraft:trapdoor", "Trapdoor", 300},
        {ID_OFFSET(block_wooden_slab), ID_OFFSET(state_wooden_slab), ID_OFFSET(item_wooden_slab), "minecraft:wooden_slab", "Wooden Slab", 150},
        {ID_OFFSET(block_oak_stairs), ID_OFFSET(state_oak_stairs), ID_OFFSET(item_oak_stairs), "minecraft:oak_stairs", "Oak Wood Stairs", 300},
        {ID_OFFSET(block_spruce_stairs), ID_OFFSET(state_spruce_stairs), ID_OFFSET(item_spruce_stairs), "minecraft:spruce_stairs", "Spruce Wood Stairs", 300},
        {ID_OFFSET(block_birch_stairs), ID_OFFSET(state_birch_stairs), ID_OFFSET(item_birch_stairs), "minecraft:birch_stairs", "Birch Wood Stairs", 300},
        {ID_OFFSET(block_jungle_stairs), ID_OFFSET(state_jungle_stairs), ID_OFFSET(item_jungle_stairs), "minecraft:jungle_stairs", "Jungle Wood Stairs", 300},
        {ID_OFFSET(block_acacia_stairs), ID_OFFSET(state_acacia_stairs), ID_OFFSET(item_acacia_stairs), "minecraft:acacia_stairs", "Acacia Wood Stairs", 300},
        {ID_OFFSET(block_dark_oak_stairs), ID_OFFSET(state_dark_oak_stairs), ID_OFFSET(item_dark_oak_stairs), "minecraft:dark_oak_stairs", "Dark Oak Wood Stairs", 300},
        {ID_OFFSET(block_jukebox), ID_OFFSET(state_jukebox), ID_OFFSET(item_jukebox), "minecraft:jukebox", "Jukebox", 300},
        {ID_OFFSET(block_noteblock), ID_OFFSET(state_noteblock), ID_OFFSET(item_noteblock), "minecraft:noteblock", "Note Block", 300},
        {ID_OFFSET(block_daylight_detector), ID_OFFSET(state_daylight_detector), ID_OFFSET(item_daylight_detector), "minecraft:daylight_detector", "Daylight Sensor", 300},
        {ID_OFFSET(block_brown_mushroom_block), ID_OFFSET(state_brown_mushroom_block), ID_OFFSET(item_brown_mushroom_block), "minecraft:brown_mushroom_block", "Brown Mushroom Block", 300},
        {ID_OFFSET(block_red_mushroom_block), ID_OFFSET(state_red_mushroom_block), ID_OFFSET(item_red_mushroom_block), "minecraft:red_mushroom_block", "Red Mushroom Block", 300}};
    static const struct {
        size_t item;
        const char *name, *label;
        unsigned int stack, hunger;
        double saturation;
        unsigned int fuel;
    } resources[] = {
        {ID_OFFSET(item_clay_ball), "minecraft:clay_ball", "Clay", 64, 0, 0, 0},
        {ID_OFFSET(item_brick), "minecraft:brick", "Brick", 64, 0, 0, 0},
        {ID_OFFSET(item_netherbrick), "minecraft:netherbrick", "Nether Brick", 64, 0, 0, 0},
        {ID_OFFSET(item_dye), "minecraft:dye", "Dye", 64, 0, 0, 0},
        {ID_OFFSET(item_quartz), "minecraft:quartz", "Nether Quartz", 64, 0, 0, 0},
        {ID_OFFSET(item_porkchop), "minecraft:porkchop", "Raw Porkchop", 64, 3, 0.3, 0},
        {ID_OFFSET(item_cooked_porkchop), "minecraft:cooked_porkchop", "Cooked Porkchop", 64, 8, 0.8, 0},
        {ID_OFFSET(item_beef), "minecraft:beef", "Raw Beef", 64, 3, 0.3, 0},
        {ID_OFFSET(item_cooked_beef), "minecraft:cooked_beef", "Steak", 64, 8, 0.8, 0},
        {ID_OFFSET(item_chicken), "minecraft:chicken", "Raw Chicken", 64, 2, 0.3, 0},
        {ID_OFFSET(item_cooked_chicken), "minecraft:cooked_chicken", "Cooked Chicken", 64, 6, 0.6, 0},
        {ID_OFFSET(item_fish), "minecraft:fish", "Raw Fish", 64, 2, 0.1, 0},
        {ID_OFFSET(item_cooked_fish), "minecraft:cooked_fish", "Cooked Fish", 64, 5, 0.6, 0},
        {ID_OFFSET(item_potato), "minecraft:potato", "Potato", 64, 1, 0.3, 0},
        {ID_OFFSET(item_baked_potato), "minecraft:baked_potato", "Baked Potato", 64, 5, 0.6, 0},
        {ID_OFFSET(item_blaze_rod), "minecraft:blaze_rod", "Blaze Rod", 64, 0, 0, 2400},
        {ID_OFFSET(item_bucket), "minecraft:bucket", "Bucket", 16, 0, 0, 0},
        {ID_OFFSET(item_lava_bucket), "minecraft:lava_bucket", "Lava Bucket", 1, 0, 0, 20000},
        {ID_OFFSET(item_wooden_sword), "minecraft:wooden_sword", "Wooden Sword", 1, 0, 0, 200},
        {ID_OFFSET(item_wooden_hoe), "minecraft:wooden_hoe", "Wooden Hoe", 1, 0, 0, 200}};
    maage_item_definition item;
    const maage_block_state_definition *state;
    maage_status result;
    size_t i;
    memset(&item, 0, sizeof(item));
    item.has_block = 1;
    item.block_id = ids->block_furnace;
    item.placement_state_id = item.icon_state_id = ids->furnaces[0][2];
    item.max_stack = 64;
    item.mining_speed = 1;
    item.display_name = "Furnace";
    item.placement = furnace_placement;
    item.texture_id = MAAGE_CONTENT_TEXTURE_FURNACE + 2;
    result = maage_item_registry_register(registry, "minecraft:furnace", &item);
    if (maage_status_failed(result)) return result;
    ids->item_furnace = item.id;
    for (i = 0; i < sizeof(definitions) / sizeof(definitions[0]); ++i) {
        memset(&item, 0, sizeof(item));
        item.has_block = 1;
        item.block_id = MAAGE_CONTENT_ID(ids, definitions[i].block);
        item.placement_state_id = item.icon_state_id = MAAGE_CONTENT_ID(ids, definitions[i].state);
        state = maage_block_registry_state(blocks, item.icon_state_id);
        item.texture_id = state->faces[MAAGE_FACE_POSITIVE_Z].texture_id;
        item.max_stack = 64;
        item.mining_speed = 1;
        item.display_name = definitions[i].label;
        item.fuel_ticks = definitions[i].fuel;
        if (item.block_id == ids->block_log2) {
            item.max_metadata = 1;
            item.placement = maage_item_builtin_placement;
        }
        result = maage_item_registry_register(registry, definitions[i].name, &item);
        if (maage_status_failed(result)) return result;
        *furnace_id(ids, definitions[i].item) = item.id;
    }
    for (i = 0; i < sizeof(resources) / sizeof(resources[0]); ++i) {
        memset(&item, 0, sizeof(item));
        item.max_stack = resources[i].stack;
        item.display_name = resources[i].label;
        item.model = MAAGE_ITEM_MODEL_SPRITE;
        item.texture_index = 35 + (unsigned int) i;
        item.texture_id = MAAGE_CONTENT_TEXTURE_ITEMS + item.texture_index;
        item.mining_speed = 1;
        item.fuel_ticks = resources[i].fuel;
        item.food_hunger = resources[i].hunger;
        item.food_saturation = resources[i].saturation;
        if (item.food_hunger) item.food_ticks = 32;
        if (resources[i].item == ID_OFFSET(item_dye)) item.max_metadata = 15;
        if (resources[i].item == ID_OFFSET(item_fish)) item.max_metadata = 3;
        if (resources[i].item == ID_OFFSET(item_cooked_fish)) item.max_metadata = 1;
        if (resources[i].item == ID_OFFSET(item_lava_bucket)) item.container_item = ids->item_bucket;
        if (resources[i].item == ID_OFFSET(item_wooden_sword) || resources[i].item == ID_OFFSET(item_wooden_hoe)) item.durability = 59;
        result = maage_item_registry_register(registry, resources[i].name, &item);
        if (maage_status_failed(result)) return result;
        *furnace_id(ids, resources[i].item) = item.id;
    }
    return maage_status_make(MAAGE_STATUS_SUCCESS, 0, MAAGE_OPERATION_REGISTER, 0);
}
static maage_status furnace_placement(const maage_item_definition *item, unsigned int metadata, maage_block_face face, double yaw, maage_block_state_id *state) {
    double turn = fmod(yaw / 1.5707963267948966 + 0.5, 4.0);
    (void) face;
    if (metadata) return maage_status_make(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_CONTEXT_WORLD, MAAGE_OPERATION_RESOLVE, 0);
    if (turn < 0) turn += 4;
    *state = item->builtins->furnaces[0][((unsigned int) turn + 2U) % 4U];
    return maage_status_make(MAAGE_STATUS_SUCCESS, 0, MAAGE_OPERATION_RESOLVE, 0);
}
maage_status maage_smelting_create_builtin(const maage_item_registry *items, maage_smelting_registry **recipes, maage_fuel_registry **fuels) {
    static const struct {
        size_t input, output;
        unsigned int metadata, output_metadata;
        double xp;
    } definitions[] = {
        {ID_OFFSET(item_iron_ore), ID_OFFSET(item_iron_ingot), 0U, 0U, 0.7},
        {ID_OFFSET(item_gold_ore), ID_OFFSET(item_gold_ingot), 0U, 0U, 1},
        {ID_OFFSET(item_diamond_ore), ID_OFFSET(item_diamond), 0U, 0U, 1},
        {ID_OFFSET(item_emerald_ore), ID_OFFSET(item_emerald), 0U, 0U, 1},
        {ID_OFFSET(item_coal_ore), ID_OFFSET(item_coal), 0U, 0U, 0.1},
        {ID_OFFSET(item_redstone_ore), ID_OFFSET(item_redstone), 0U, 0U, 0.7},
        {ID_OFFSET(item_lapis_ore), ID_OFFSET(item_lapis), 0U, 0U, 0.2},
        {ID_OFFSET(item_quartz_ore), ID_OFFSET(item_quartz), 0U, 0U, 0.2},
        {ID_OFFSET(item_cobblestone), ID_OFFSET(item_stone), 0U, 0U, 0.1},
        {ID_OFFSET(item_sand), ID_OFFSET(item_glass), MAAGE_SMELTING_METADATA_ANY, 0U, 0.1},
        {ID_OFFSET(item_log), ID_OFFSET(item_coal), 65535U, 1U, 0.15},
        {ID_OFFSET(item_log2), ID_OFFSET(item_coal), 65535U, 1U, 0.15},
        {ID_OFFSET(item_clay_ball), ID_OFFSET(item_brick), 0U, 0U, 0.3},
        {ID_OFFSET(item_clay), ID_OFFSET(item_hardened_clay), 0U, 0U, 0.35},
        {ID_OFFSET(item_cactus), ID_OFFSET(item_dye), 0U, 2U, 0.2},
        {ID_OFFSET(item_netherrack), ID_OFFSET(item_netherbrick), 0U, 0U, 0.1},
        {ID_OFFSET(item_porkchop), ID_OFFSET(item_cooked_porkchop), 0U, 0U, 0.35},
        {ID_OFFSET(item_beef), ID_OFFSET(item_cooked_beef), 0U, 0U, 0.35},
        {ID_OFFSET(item_chicken), ID_OFFSET(item_cooked_chicken), 0U, 0U, 0.35},
        {ID_OFFSET(item_fish), ID_OFFSET(item_cooked_fish), 0U, 0U, 0.35},
        {ID_OFFSET(item_fish), ID_OFFSET(item_cooked_fish), 1U, 1U, 0.35},
        {ID_OFFSET(item_potato), ID_OFFSET(item_baked_potato), 0U, 0U, 0.35}};
    const maage_builtin_ids *ids = maage_item_registry_builtin_ids(items);
    maage_smelting_recipe recipe;
    maage_fuel_definition fuel;
    const maage_item_definition *item;
    maage_status result;
    size_t i;
    if (recipes) *recipes = NULL;
    if (fuels) *fuels = NULL;
    if (!recipes || !fuels || !ids) return maage_status_make(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_CONTEXT_REGISTRY, MAAGE_OPERATION_CREATE, 0);
    result = maage_smelting_registry_create(items, recipes);
    if (maage_status_failed(result)) return result;
    result = maage_fuel_registry_create(items, fuels);
    if (maage_status_failed(result)) return result;
    for (i = 0; i < sizeof(definitions) / sizeof(definitions[0]); ++i) {
        memset(&recipe, 0, sizeof(recipe));
        recipe.input = MAAGE_CONTENT_ID(ids, definitions[i].input);
        recipe.metadata = definitions[i].metadata;
        recipe.output.item_id = MAAGE_CONTENT_ID(ids, definitions[i].output);
        recipe.output.metadata = definitions[i].output_metadata;
        recipe.output.count = 1;
        recipe.experience = definitions[i].xp;
        result = maage_smelting_register(*recipes, &recipe);
        if (maage_status_failed(result)) return result;
    }
    for (i = 0; i < maage_item_registry_count(items); ++i) {
        item = maage_item_registry_at(items, i);
        if (!item->fuel_ticks) continue;
        memset(&fuel, 0, sizeof(fuel));
        fuel.item = item->id;
        fuel.metadata = MAAGE_SMELTING_METADATA_ANY;
        fuel.ticks = item->fuel_ticks;
        if (item->container_item) {
            fuel.remainder.item_id = item->container_item;
            fuel.remainder.count = 1;
        }
        result = maage_fuel_register(*fuels, &fuel);
        if (maage_status_failed(result)) return result;
    }
    result = maage_smelting_finalize(*recipes);
    if (!maage_status_failed(result)) result = maage_fuel_finalize(*fuels);
    return result;
}
