#ifndef MAAGE_BASE_IDS_H
#define MAAGE_BASE_IDS_H
#include "maage/content.h"
/* Generation probabilities, independent of Client color blending. Trees are
 * oak/spruce/birch probabilities per jittered eight-block candidate; their sum
 * is at most one. Other densities are per eligible decoration/feature attempt.
 * Zero disables a feature. Finalization validates every probability. */
typedef struct maage_base_biome_decoration {
    double trees[3];
    double grass, flowers, dead_bushes, cacti, mushrooms;
    double emeralds, lakes;
} maage_base_biome_decoration;

#define MAAGE_BASE_ITEM_TEXTURE_COUNT 64U
#define MAAGE_BASE_BIOME_COUNT 6
enum {
    MAAGE_BASE_MOB_PIG,
    MAAGE_BASE_MOB_COW,
    MAAGE_BASE_MOB_SHEEP,
    MAAGE_BASE_MOB_CHICKEN,
    MAAGE_BASE_MOB_ZOMBIE,
    MAAGE_BASE_MOB_SKELETON,
    MAAGE_BASE_MOB_CREEPER,
    MAAGE_BASE_MOB_SPIDER,
    MAAGE_BASE_MOB_COUNT
};

/* Stable numeric presentation tile slots, not runtime resource paths.
 * The Client resolves these into atlas UVs once, before publication. */
#define MAAGE_BASE_TEXTURE_STONE 0
#define MAAGE_BASE_TEXTURE_DIRT 1
#define MAAGE_BASE_TEXTURE_GRASS_TOP 2
#define MAAGE_BASE_TEXTURE_GRASS_SIDE 3
#define MAAGE_BASE_TEXTURE_BEDROCK 4
#define MAAGE_BASE_TEXTURE_WATER 5
#define MAAGE_BASE_TEXTURE_SAND 6
#define MAAGE_BASE_TEXTURE_GRAVEL 7
#define MAAGE_BASE_TEXTURE_SANDSTONE 8
#define MAAGE_BASE_TEXTURE_SANDSTONE_TOP 9
#define MAAGE_BASE_TEXTURE_SANDSTONE_BOTTOM 10
#define MAAGE_BASE_TEXTURE_EXTRA 11
#define MAAGE_BASE_TEXTURE_LEAVES_OPAQUE_OAK (MAAGE_BASE_TEXTURE_EXTRA + 33)
#define MAAGE_BASE_TEXTURE_LEAVES_OPAQUE_SPRUCE (MAAGE_BASE_TEXTURE_EXTRA + 34)
#define MAAGE_BASE_TEXTURE_LEAVES_OPAQUE_BIRCH (MAAGE_BASE_TEXTURE_EXTRA + 35)
#define MAAGE_BASE_TEXTURE_GRASS_SIDE_OVERLAY (MAAGE_BASE_TEXTURE_EXTRA + 36)
#define MAAGE_BASE_TEXTURE_GRASS_SIDE_SNOW (MAAGE_BASE_TEXTURE_EXTRA + 37)
#define MAAGE_BASE_TEXTURE_MISSING (MAAGE_BASE_TEXTURE_EXTRA + 38)
#define MAAGE_BASE_TEXTURE_CRAFTING_TOP (MAAGE_BASE_TEXTURE_MISSING + 1)
#define MAAGE_BASE_TEXTURE_CRAFTING_SIDE (MAAGE_BASE_TEXTURE_MISSING + 2)
#define MAAGE_BASE_TEXTURE_CRAFTING_FRONT (MAAGE_BASE_TEXTURE_MISSING + 3)
#define MAAGE_BASE_TEXTURE_SPRUCE_PLANKS (MAAGE_BASE_TEXTURE_MISSING + 4)
#define MAAGE_BASE_TEXTURE_BIRCH_PLANKS (MAAGE_BASE_TEXTURE_MISSING + 5)
#define MAAGE_BASE_TEXTURE_STORAGE (MAAGE_BASE_TEXTURE_MISSING + 6)
#define MAAGE_BASE_TEXTURE_FURNACE (MAAGE_BASE_TEXTURE_STORAGE + 8)
#define MAAGE_BASE_TEXTURE_FURNACE_EXTRA (MAAGE_BASE_TEXTURE_FURNACE + 4)
#define MAAGE_BASE_TEXTURE_FURNACE_VARIANTS (MAAGE_BASE_TEXTURE_FURNACE_EXTRA + 26)
#define MAAGE_BASE_TEXTURE_FARMLAND (MAAGE_BASE_TEXTURE_FURNACE_VARIANTS + 9)
#define MAAGE_BASE_TEXTURE_WHEAT (MAAGE_BASE_TEXTURE_FARMLAND + 2)
#define MAAGE_BASE_TEXTURE_ITEMS (MAAGE_BASE_TEXTURE_WHEAT + 8)
#define MAAGE_BASE_TEXTURE_COUNT (MAAGE_BASE_TEXTURE_ITEMS + MAAGE_BASE_ITEM_TEXTURE_COUNT)

/* Registration-assigned references, owned by each registry set. Readers borrow
 * this table through const accessors; publication/finalization ends mutation.
 * Array indices below describe properties, never content identities. */
typedef struct maage_base_ids {
    const maage_traits_registry *traits;
    const maage_block_registry *blocks;
    const maage_item_registry *items;
    maage_property_id property_wood, property_axis, property_persistent, property_check_decay, property_stage, property_age, property_crop_age, property_moisture, property_facing, property_lit, property_color, property_sand, property_attachment, property_fluid_level, property_grass_variant, property_plant_variant;
    maage_property_value wood_values[6];
    maage_tag_id tool_tags[6]; /* Private constructor selectors index registered item capabilities. */
    maage_tag_id tag_wood, tag_log, tag_planks, tag_leaves, tag_soil, tag_sand, tag_farmland, tag_crop, tag_axial, tag_soil_plant, tag_mushroom, tag_cactus, tag_wood_drops;
    unsigned long mob_types[MAAGE_BASE_MOB_COUNT], mob_models[MAAGE_BASE_MOB_COUNT], mob_textures[MAAGE_BASE_MOB_COUNT];
    unsigned long spawn_eggs[MAAGE_BASE_MOB_COUNT], arrow_type, arrow_model, arrow_texture, sheep_fleece;
    unsigned long item_feather, item_leather, item_bone, item_rotten_flesh, item_string, item_gunpowder, item_egg, item_arrow, item_spider_eye, item_milk_bucket;
    unsigned long block_wool, item_wool, wool_states[16], wool_textures[16];
    unsigned long material_default, material_stone, material_grass, material_dirt, material_wood, material_sand, material_gravel, material_snow;
    unsigned long material_metal, material_glass, material_water, material_lava, material_foliage, material_cloth, material_ladder, material_anvil;
    unsigned long container_player, container_table, container_furnace, container_chest[2];
    unsigned long chest_models[2][2], chest_states[2][4], chest_open_sound, chest_close_sound;
    unsigned long fluid_water, fluid_lava;
    unsigned long geometry_cube, geometry_cactus, geometry_cross, geometry_torch, geometry_entity;
    unsigned long texture_water_flow, texture_lava_flow, texture_water_overlay;
    unsigned long biome_ids[MAAGE_BASE_BIOME_COUNT];
    unsigned long textures[MAAGE_BASE_TEXTURE_COUNT];
    unsigned long sources[190];
    unsigned long state_red_sand;
    unsigned long block_furnace, item_furnace, furnaces[2][4];
    unsigned long block_glass, state_glass, item_glass;
    unsigned long block_clay, state_clay, item_clay;
    unsigned long block_hardened_clay, state_hardened_clay, item_hardened_clay;
    unsigned long block_netherrack, state_netherrack, item_netherrack;
    unsigned long block_nether_brick, state_nether_brick, item_nether_brick;
    unsigned long block_quartz_ore, state_quartz_ore, item_quartz_ore;
    unsigned long block_log2, state_log2, item_log2;
    unsigned long block_chest, state_chest, item_chest;
    unsigned long block_trapped_chest, state_trapped_chest, item_trapped_chest;
    unsigned long block_bookshelf, state_bookshelf, item_bookshelf;
    unsigned long block_fence, state_fence, item_fence;
    unsigned long block_fence_gate, state_fence_gate, item_fence_gate;
    unsigned long block_wooden_pressure_plate, state_wooden_pressure_plate, item_wooden_pressure_plate;
    unsigned long block_trapdoor, state_trapdoor, item_trapdoor;
    unsigned long block_wooden_slab, state_wooden_slab, item_wooden_slab;
    unsigned long block_oak_stairs, state_oak_stairs, item_oak_stairs;
    unsigned long block_spruce_stairs, state_spruce_stairs, item_spruce_stairs;
    unsigned long block_birch_stairs, state_birch_stairs, item_birch_stairs;
    unsigned long block_jungle_stairs, state_jungle_stairs, item_jungle_stairs;
    unsigned long block_acacia_stairs, state_acacia_stairs, item_acacia_stairs;
    unsigned long block_dark_oak_stairs, state_dark_oak_stairs, item_dark_oak_stairs;
    unsigned long block_jukebox, state_jukebox, item_jukebox;
    unsigned long block_noteblock, state_noteblock, item_noteblock;
    unsigned long block_daylight_detector, state_daylight_detector, item_daylight_detector;
    unsigned long block_brown_mushroom_block, state_brown_mushroom_block, item_brown_mushroom_block;
    unsigned long block_red_mushroom_block, state_red_mushroom_block, item_red_mushroom_block;
    unsigned long item_clay_ball;
    unsigned long item_brick;
    unsigned long item_netherbrick;
    unsigned long item_dye;
    unsigned long item_quartz;
    unsigned long item_porkchop;
    unsigned long item_cooked_porkchop;
    unsigned long item_beef;
    unsigned long item_cooked_beef;
    unsigned long item_chicken;
    unsigned long item_cooked_chicken;
    unsigned long item_fish;
    unsigned long item_cooked_fish;
    unsigned long item_potato;
    unsigned long item_baked_potato;
    unsigned long item_blaze_rod;
    unsigned long item_bucket;
    unsigned long item_lava_bucket;
    unsigned long item_wooden_sword;
    unsigned long item_wooden_hoe;
    unsigned long block_farmland, block_wheat, item_wheat;
    unsigned long farmland[8], wheat[8];
    unsigned long block_air;
    unsigned long block_stone;
    unsigned long block_grass;
    unsigned long block_dirt;
    unsigned long block_cobblestone;
    unsigned long block_planks;
    unsigned long block_sapling;
    unsigned long block_bedrock;
    unsigned long block_water;
    unsigned long block_lava;
    unsigned long block_sand;
    unsigned long block_gravel;
    unsigned long block_gold_ore;
    unsigned long block_iron_ore;
    unsigned long block_coal_ore;
    unsigned long block_log;
    unsigned long block_leaves;
    unsigned long block_lapis_ore;
    unsigned long block_sandstone;
    unsigned long block_tall_grass;
    unsigned long block_dead_bush;
    unsigned long block_dandelion;
    unsigned long block_poppy;
    unsigned long block_brown_mushroom;
    unsigned long block_red_mushroom;
    unsigned long block_mossy_cobblestone;
    unsigned long block_obsidian;
    unsigned long block_torch;
    unsigned long block_crafting_table;
    unsigned long block_gold_block;
    unsigned long block_iron_block;
    unsigned long block_diamond_block;
    unsigned long block_lapis_block;
    unsigned long block_emerald_block;
    unsigned long block_redstone_block;
    unsigned long block_coal_block;
    unsigned long block_snow_block;
    unsigned long block_diamond_ore;
    unsigned long block_redstone_ore;
    unsigned long block_snow;
    unsigned long block_ice;
    unsigned long block_cactus;
    unsigned long block_emerald_ore;
    unsigned long state_air;
    unsigned long state_stone;
    unsigned long state_grass;
    unsigned long state_dirt;
    unsigned long state_cobblestone;
    unsigned long state_planks;
    unsigned long state_sapling;
    unsigned long state_bedrock;
    unsigned long state_water;
    unsigned long state_lava;
    unsigned long state_sand;
    unsigned long state_gravel;
    unsigned long state_gold_ore;
    unsigned long state_iron_ore;
    unsigned long state_coal_ore;
    unsigned long state_log;
    unsigned long state_leaves;
    unsigned long state_lapis_ore;
    unsigned long state_sandstone;
    unsigned long state_tall_grass;
    unsigned long state_dead_bush;
    unsigned long state_dandelion;
    unsigned long state_poppy;
    unsigned long state_brown_mushroom;
    unsigned long state_red_mushroom;
    unsigned long state_mossy_cobblestone;
    unsigned long state_obsidian;
    unsigned long state_torch;
    unsigned long state_crafting_table;
    unsigned long state_diamond_ore;
    unsigned long state_redstone_ore;
    unsigned long state_snow;
    unsigned long state_ice;
    unsigned long state_cactus;
    unsigned long state_emerald_ore;
    unsigned long state_spruce_log;
    unsigned long state_birch_log;
    unsigned long state_spruce_leaves;
    unsigned long state_birch_leaves;
    unsigned long item_stone;
    unsigned long item_grass;
    unsigned long item_dirt;
    unsigned long item_cobblestone;
    unsigned long item_planks;
    unsigned long item_sapling;
    unsigned long item_bedrock;
    unsigned long item_sand;
    unsigned long item_gravel;
    unsigned long item_gold_ore;
    unsigned long item_iron_ore;
    unsigned long item_coal_ore;
    unsigned long item_log;
    unsigned long item_leaves;
    unsigned long item_lapis_ore;
    unsigned long item_sandstone;
    unsigned long item_tall_grass;
    unsigned long item_dead_bush;
    unsigned long item_dandelion;
    unsigned long item_poppy;
    unsigned long item_brown_mushroom;
    unsigned long item_red_mushroom;
    unsigned long item_mossy_cobblestone;
    unsigned long item_obsidian;
    unsigned long item_torch;
    unsigned long item_crafting_table;
    unsigned long item_gold_block;
    unsigned long item_iron_block;
    unsigned long item_diamond_block;
    unsigned long item_lapis_block;
    unsigned long item_emerald_block;
    unsigned long item_redstone_block;
    unsigned long item_coal_block;
    unsigned long item_snow_block;
    unsigned long item_diamond_ore;
    unsigned long item_redstone_ore;
    unsigned long item_snow;
    unsigned long item_ice;
    unsigned long item_cactus;
    unsigned long item_emerald_ore;
    unsigned long item_shears;
    unsigned long item_coal;
    unsigned long item_diamond;
    unsigned long item_redstone;
    unsigned long item_lapis;
    unsigned long item_emerald;
    unsigned long item_flint;
    unsigned long item_apple;
    unsigned long item_snowball;
    unsigned long item_seeds;
    unsigned long item_stick;
    unsigned long item_iron_ingot;
    unsigned long item_gold_ingot;
    unsigned long item_gold_nugget;
    unsigned long item_bowl;
    unsigned long item_mushroom_stew;
    unsigned long biome_ocean;
    unsigned long biome_plains;
    unsigned long biome_desert;
    unsigned long biome_hills;
    unsigned long biome_forest;
    unsigned long biome_taiga;
    unsigned long state_gold_block;
    unsigned long state_iron_block;
    unsigned long state_diamond_block;
    unsigned long state_lapis_block;
    unsigned long state_emerald_block;
    unsigned long state_redstone_block;
    unsigned long state_coal_block;
    unsigned long state_snow_block;
    unsigned long leaves[16], saplings[16], planks[6], torches[6], logs[12], logs2[12];
    unsigned long water[16], lava[16], cactus[16], biome_grass[6], tools[5][4];
} maage_base_ids;

/* Composition-local immutable Base references; generation and registered behavior
 * providers share this lifetime without depending on an adopting block context. */
const maage_base_ids *maage_base_generation_references(const maage_block_registry *registry);
const maage_base_ids *maage_base_state_references(const maage_block_registry *registry, const maage_block_state_definition *state);
#endif
