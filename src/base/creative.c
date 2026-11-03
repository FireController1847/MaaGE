#include "base_internal.h"
#include "maage/creative.h"
#include <stddef.h>
#include <string.h>

static maage_status base_creative_texture(maage_content_context *context, const char *path, unsigned int edge, maage_texture_id *id) {
    maage_texture_definition texture;
    maage_status result;
    memset(&texture, 0, sizeof(texture));
    texture.fallback = MAAGE_CONTENT_ID_INVALID;
    texture.frame_edge = edge;
    texture.frame_ticks = 1;
    result = maage_presentation_source_register(context->presentation, path, &texture.source);
    if (!maage_status_failed(result)) result = maage_presentation_texture_register(context->presentation, &texture, id);
    return result;
}
static maage_status base_creative_sprite(maage_content_context *context, const char *path, maage_creative_icon_quad *quad) {
    unsigned int i;
    maage_status result;
    memset(quad, 0, sizeof(*quad));
    result = base_creative_texture(context, path, 16, &quad->texture);
    for (i = 0; i < 4; ++i) {
        quad->position[i][0] = i == 1 || i == 2 ? 16.0f : 0;
        quad->position[i][1] = i < 2 ? 0 : 16.0f;
        quad->uv[i][0] = quad->position[i][0] / 16;
        quad->uv[i][1] = quad->position[i][1] / 16;
        quad->color[i] = 1;
    }
    return result;
}
static maage_status base_creative_bricks(maage_content_context *context, maage_creative_icon_quad quads[3]) {
    static const float points[3][4][2] = {
        {{0.93f, 3.67f}, {8, 7.21f}, {8, 15.87f}, {0.93f, 12.33f}},
        {{8, 7.21f}, {15.07f, 3.67f}, {15.07f, 12.33f}, {8, 15.87f}},
        {{8, 0.13f}, {15.07f, 3.67f}, {8, 7.21f}, {0.93f, 3.67f}}};
    static const float shade[3] = {0.637f, 0.435f, 1};
    maage_status result;
    unsigned int face, i;
    result = base_creative_sprite(context, "minecraft/textures/blocks/brick.png", &quads[0]);
    if (maage_status_failed(result)) return result;
    quads[1] = quads[2] = quads[0];
    for (face = 0; face < 3; ++face) {
        memcpy(quads[face].position, points[face], sizeof(points[face]));
        for (i = 0; i < 3; ++i)
            quads[face].color[i] = shade[face];
    }
    return result;
}
typedef struct base_creative_assignment {
    size_t item_offset;
    unsigned int tab, first_variant, last_variant;
    long order;
} base_creative_assignment;
#define BASE_CREATIVE_ITEM(item, tab, first, last, order) {offsetof(maage_base_ids, item), tab, first, last, order}
maage_status maage_base_creative(maage_content_context *context, const maage_base_ids *ids) {
    static const char *const names[] = {"building_blocks", "decoration_blocks", "redstone", "transportation", "miscellaneous", "search", "foodstuffs", "tools", "combat", "brewing", "materials", "survival_inventory"};
    static const char *const titles[] = {"Building Blocks", "Decoration Blocks", "Redstone", "Transportation", "Miscellaneous", "Search Items", "Foodstuffs", "Tools", "Combat", "Brewing", "Materials", "Survival Inventory"};
    /* Explicit display order and visible variants are independent of item IDs
     * and variant_count. Internal huge-mushroom blocks have no catalog entry. */
    static const base_creative_assignment assignments[] = {
        BASE_CREATIVE_ITEM(item_stone, 0, 0, 0, 1), BASE_CREATIVE_ITEM(item_grass, 0, 0, 0, 2), BASE_CREATIVE_ITEM(item_dirt, 0, 0, 0, 3),
        BASE_CREATIVE_ITEM(item_cobblestone, 0, 0, 0, 4), BASE_CREATIVE_ITEM(item_planks, 0, 0, 5, 5), BASE_CREATIVE_ITEM(item_sapling, 1, 0, 2, 6),
        BASE_CREATIVE_ITEM(item_bedrock, 0, 0, 0, 7), BASE_CREATIVE_ITEM(item_sand, 0, 0, 1, 12), BASE_CREATIVE_ITEM(item_gravel, 0, 0, 0, 13),
        BASE_CREATIVE_ITEM(item_gold_ore, 0, 0, 0, 14), BASE_CREATIVE_ITEM(item_iron_ore, 0, 0, 0, 15), BASE_CREATIVE_ITEM(item_coal_ore, 0, 0, 0, 16),
        BASE_CREATIVE_ITEM(item_log, 0, 0, 3, 17), BASE_CREATIVE_ITEM(item_leaves, 1, 0, 2, 18), BASE_CREATIVE_ITEM(item_glass, 0, 0, 0, 20),
        BASE_CREATIVE_ITEM(item_lapis_ore, 0, 0, 0, 21), BASE_CREATIVE_ITEM(item_lapis_block, 0, 0, 0, 22), BASE_CREATIVE_ITEM(item_sandstone, 0, 0, 0, 24),
        BASE_CREATIVE_ITEM(item_noteblock, 2, 0, 0, 25), BASE_CREATIVE_ITEM(item_tall_grass, 1, 0, 0, 31), BASE_CREATIVE_ITEM(item_dead_bush, 1, 0, 0, 32),
        BASE_CREATIVE_ITEM(item_dandelion, 1, 0, 0, 37), BASE_CREATIVE_ITEM(item_poppy, 1, 0, 0, 38), BASE_CREATIVE_ITEM(item_brown_mushroom, 1, 0, 0, 39),
        BASE_CREATIVE_ITEM(item_red_mushroom, 1, 0, 0, 40), BASE_CREATIVE_ITEM(item_gold_block, 0, 0, 0, 41), BASE_CREATIVE_ITEM(item_iron_block, 0, 0, 0, 42),
        BASE_CREATIVE_ITEM(item_bookshelf, 0, 0, 0, 47), BASE_CREATIVE_ITEM(item_mossy_cobblestone, 0, 0, 0, 48), BASE_CREATIVE_ITEM(item_obsidian, 0, 0, 0, 49),
        BASE_CREATIVE_ITEM(item_torch, 1, 0, 0, 50), BASE_CREATIVE_ITEM(item_chest, 1, 0, 0, 54), BASE_CREATIVE_ITEM(item_diamond_ore, 0, 0, 0, 56),
        BASE_CREATIVE_ITEM(item_diamond_block, 0, 0, 0, 57), BASE_CREATIVE_ITEM(item_crafting_table, 1, 0, 0, 58), BASE_CREATIVE_ITEM(item_furnace, 1, 0, 0, 61),
        BASE_CREATIVE_ITEM(item_oak_stairs, 0, 0, 0, 53), BASE_CREATIVE_ITEM(item_wooden_pressure_plate, 2, 0, 0, 72), BASE_CREATIVE_ITEM(item_redstone_ore, 0, 0, 0, 73),
        BASE_CREATIVE_ITEM(item_snow, 1, 0, 0, 78), BASE_CREATIVE_ITEM(item_ice, 0, 0, 0, 79), BASE_CREATIVE_ITEM(item_snow_block, 0, 0, 0, 80),
        BASE_CREATIVE_ITEM(item_cactus, 1, 0, 0, 81), BASE_CREATIVE_ITEM(item_clay, 0, 0, 0, 82), BASE_CREATIVE_ITEM(item_jukebox, 1, 0, 0, 84),
        BASE_CREATIVE_ITEM(item_fence, 1, 0, 0, 85), BASE_CREATIVE_ITEM(item_netherrack, 0, 0, 0, 87), BASE_CREATIVE_ITEM(item_trapdoor, 2, 0, 0, 96),
        BASE_CREATIVE_ITEM(item_fence_gate, 2, 0, 0, 107), BASE_CREATIVE_ITEM(item_nether_brick, 0, 0, 0, 112), BASE_CREATIVE_ITEM(item_wooden_slab, 0, 0, 0, 126),
        BASE_CREATIVE_ITEM(item_emerald_ore, 0, 0, 0, 129), BASE_CREATIVE_ITEM(item_emerald_block, 0, 0, 0, 133), BASE_CREATIVE_ITEM(item_spruce_stairs, 0, 0, 0, 134),
        BASE_CREATIVE_ITEM(item_birch_stairs, 0, 0, 0, 135), BASE_CREATIVE_ITEM(item_jungle_stairs, 0, 0, 0, 136), BASE_CREATIVE_ITEM(item_trapped_chest, 2, 0, 0, 146),
        BASE_CREATIVE_ITEM(item_daylight_detector, 2, 0, 0, 151), BASE_CREATIVE_ITEM(item_redstone_block, 2, 0, 0, 152), BASE_CREATIVE_ITEM(item_quartz_ore, 0, 0, 0, 153),
        BASE_CREATIVE_ITEM(item_log2, 0, 0, 1, 162), BASE_CREATIVE_ITEM(item_acacia_stairs, 0, 0, 0, 163), BASE_CREATIVE_ITEM(item_dark_oak_stairs, 0, 0, 0, 164),
        BASE_CREATIVE_ITEM(item_hardened_clay, 0, 0, 0, 172), BASE_CREATIVE_ITEM(item_coal_block, 0, 0, 0, 173),
        BASE_CREATIVE_ITEM(item_apple, 6, 0, 0, 260), BASE_CREATIVE_ITEM(item_coal, 10, 0, 1, 263), BASE_CREATIVE_ITEM(item_diamond, 10, 0, 0, 264),
        BASE_CREATIVE_ITEM(item_iron_ingot, 10, 0, 0, 265), BASE_CREATIVE_ITEM(item_gold_ingot, 10, 0, 0, 266), BASE_CREATIVE_ITEM(item_wooden_sword, 8, 0, 0, 268),
        BASE_CREATIVE_ITEM(item_stick, 10, 0, 0, 280), BASE_CREATIVE_ITEM(item_bowl, 10, 0, 0, 281), BASE_CREATIVE_ITEM(item_mushroom_stew, 6, 0, 0, 282),
        BASE_CREATIVE_ITEM(item_seeds, 10, 0, 0, 295), BASE_CREATIVE_ITEM(item_wheat, 10, 0, 0, 296), BASE_CREATIVE_ITEM(item_flint, 10, 0, 0, 318),
        BASE_CREATIVE_ITEM(item_porkchop, 6, 0, 0, 319), BASE_CREATIVE_ITEM(item_cooked_porkchop, 6, 0, 0, 320), BASE_CREATIVE_ITEM(item_bucket, 4, 0, 0, 325),
        BASE_CREATIVE_ITEM(item_lava_bucket, 4, 0, 0, 327), BASE_CREATIVE_ITEM(item_redstone, 2, 0, 0, 331), BASE_CREATIVE_ITEM(item_snowball, 4, 0, 0, 332),
        BASE_CREATIVE_ITEM(item_brick, 10, 0, 0, 336), BASE_CREATIVE_ITEM(item_clay_ball, 10, 0, 0, 337), BASE_CREATIVE_ITEM(item_fish, 6, 0, 3, 349),
        BASE_CREATIVE_ITEM(item_cooked_fish, 6, 0, 1, 350), BASE_CREATIVE_ITEM(item_lapis, 10, 0, 0, 351), BASE_CREATIVE_ITEM(item_dye, 10, 2, 2, 351),
        BASE_CREATIVE_ITEM(item_shears, 7, 0, 0, 359), BASE_CREATIVE_ITEM(item_beef, 6, 0, 0, 363), BASE_CREATIVE_ITEM(item_cooked_beef, 6, 0, 0, 364),
        BASE_CREATIVE_ITEM(item_chicken, 6, 0, 0, 365), BASE_CREATIVE_ITEM(item_cooked_chicken, 6, 0, 0, 366), BASE_CREATIVE_ITEM(item_blaze_rod, 10, 0, 0, 369),
        BASE_CREATIVE_ITEM(item_gold_nugget, 10, 0, 0, 371), BASE_CREATIVE_ITEM(item_emerald, 10, 0, 0, 388), BASE_CREATIVE_ITEM(item_potato, 6, 0, 0, 392),
        BASE_CREATIVE_ITEM(item_baked_potato, 6, 0, 0, 393), BASE_CREATIVE_ITEM(item_netherbrick, 10, 0, 0, 405), BASE_CREATIVE_ITEM(item_quartz, 10, 0, 0, 406)};
    static const long tool_order[5][4] = {{270, 269, 271, 290}, {274, 273, 275, 291}, {257, 256, 258, 292}, {278, 277, 279, 293}, {285, 284, 286, 294}};
    maage_creative_tab_definition tab;
    maage_creative_entry entry;
    maage_creative_tab_id tabs[12];
    maage_creative_icon_quad quads[3];
    maage_texture_id backgrounds[3];
    maage_status result;
    size_t i;
    unsigned int variant, material, tool;
    result = base_creative_texture(context, "minecraft/textures/gui/container/creative_inventory/tab_items.png", 256, &backgrounds[0]);
    if (!maage_status_failed(result)) result = base_creative_texture(context, "minecraft/textures/gui/container/creative_inventory/tab_item_search.png", 256, &backgrounds[1]);
    if (!maage_status_failed(result)) result = base_creative_texture(context, "minecraft/textures/gui/container/creative_inventory/tab_inventory.png", 256, &backgrounds[2]);
    if (maage_status_failed(result)) return result;
    for (i = 0; i < 12; ++i) {
        char name[64];
        memset(&tab, 0, sizeof(tab));
        tab.title = titles[i];
        tab.order = (long) i;
        tab.kind = i == 5 ? MAAGE_CREATIVE_SEARCH : (i == 11 ? MAAGE_CREATIVE_INVENTORY : MAAGE_CREATIVE_CATALOG);
        tab.background = backgrounds[tab.kind];
        tab.icon.count = 1;
        if (i == 2) tab.icon.item_id = ids->item_redstone;
        else if (i == 4) tab.icon.item_id = ids->item_lava_bucket;
        else if (i == 6) tab.icon.item_id = ids->item_apple;
        else if (i == 7) tab.icon.item_id = ids->tools[2][2];
        else if (i == 10) tab.icon.item_id = ids->item_stick;
        else if (i == 11) tab.icon.item_id = ids->item_chest;
        else {
            memset(&tab.icon, 0, sizeof(tab.icon));
            tab.icon_quads = quads;
            tab.icon_quad_count = 1;
            if (i == 0) {
                result = base_creative_bricks(context, quads);
                tab.icon_quad_count = 3;
            } else result = base_creative_sprite(context, i == 1 ? "minecraft/textures/blocks/double_plant_paeonia_top.png" : (i == 3 ? "minecraft/textures/blocks/rail_golden.png" : (i == 5 ? "minecraft/textures/items/compass.png" : (i == 8 ? "minecraft/textures/items/gold_sword.png" : "minecraft/textures/items/potion_bottle_drinkable.png"))), quads);
            if (maage_status_failed(result)) return result;
            if (i == 9) {
                quads[1] = quads[0];
                result = base_creative_sprite(context, "minecraft/textures/items/potion_overlay.png", &quads[0]);
                if (maage_status_failed(result)) return result;
                quads[0].color[0] = 56.0f / 255;
                quads[0].color[1] = 93.0f / 255;
                quads[0].color[2] = 198.0f / 255;
                tab.icon_quad_count = 2;
            }
        }
        strcpy(name, "minecraft:");
        strcat(name, names[i]);
        result = maage_creative_tab_register(context->creative, name, &tab);
        if (maage_status_failed(result)) return result;
        tabs[i] = tab.id;
    }
    memset(&entry, 0, sizeof(entry));
    entry.searchable = 1;
    entry.stack.count = 1;
    for (i = 0; i < sizeof(assignments) / sizeof(assignments[0]); ++i) {
        entry.tab = tabs[assignments[i].tab];
        entry.stack.item_id = MAAGE_BASE_ID(ids, assignments[i].item_offset);
        for (variant = assignments[i].first_variant; variant <= assignments[i].last_variant; ++variant) {
            entry.stack.variant = variant;
            entry.order = entry.search_order = assignments[i].order * 32 + (entry.stack.item_id == ids->item_lapis ? 4 : variant);
            result = maage_creative_entry_register(context->creative, &entry);
            if (maage_status_failed(result)) return result;
        }
    }
    for (material = 0; material < 5; ++material)
        for (tool = 0; tool < 4; ++tool) {
            entry.tab = tabs[7];
            entry.stack.item_id = ids->tools[material][tool];
            entry.stack.variant = 0;
            entry.order = entry.search_order = tool_order[material][tool] * 32;
            result = maage_creative_entry_register(context->creative, &entry);
            if (maage_status_failed(result)) return result;
        }
    return result;
}
#undef BASE_CREATIVE_ITEM
