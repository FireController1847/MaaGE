#include "base_internal.h"
#include "maage/block_geometry.h"
#include <string.h>

maage_status maage_base_geometry(maage_content_context *context, maage_base_ids *ids) {
    maage_block_geometry_definition definition;
    maage_status result;
    memset(&definition, 0, sizeof(definition));
    definition.primitive = MAAGE_BLOCK_MESH_CUBE;
    result = maage_block_geometry_register(context->geometry, "minecraft:cube", &definition, &ids->geometry_cube);
    if (maage_status_failed(result)) return result;
    definition.primitive = MAAGE_BLOCK_MESH_CROSS;
    result = maage_block_geometry_register(context->geometry, "minecraft:cross", &definition, &ids->geometry_cross);
    if (maage_status_failed(result)) return result;
    definition.primitive = MAAGE_BLOCK_MESH_TORCH;
    return maage_block_geometry_register(context->geometry, "minecraft:torch", &definition, &ids->geometry_torch);
}

maage_block_state_id maage_base_item_icon(const maage_item_definition *item, unsigned int metadata) {
    const maage_base_ids *ids = (const maage_base_ids *) item->behavior_context;
    unsigned int species = metadata & 3U;
    if (item->block_id == ids->block_log && species <= 3) return ids->logs[species];
    if (item->block_id == ids->block_leaves && species <= 2) return ids->leaves[species];
    if (item->block_id == ids->block_planks && metadata < 6) return ids->planks[metadata];
    if (item->block_id == ids->block_log2 && metadata <= 1) return ids->logs2[metadata];
    if (item->block_id == ids->block_sand && metadata == 1) return ids->state_red_sand;
    return item->icon_state_id;
}
maage_texture_id maage_base_item_texture(const maage_item_definition *item, unsigned int metadata) {
    const maage_base_ids *ids = item ? (const maage_base_ids *) item->behavior_context : NULL;
    if (!item) return MAAGE_BASE_TEXTURE_MISSING;
    if (item->id == ids->item_sapling && (metadata == 1 || metadata == 2)) return ids->textures[MAAGE_BASE_TEXTURE_ITEMS + 25 + metadata];
    if (item->id == ids->item_fish && metadata) return ids->textures[MAAGE_BASE_TEXTURE_ITEMS + 55 + metadata - 1];
    if (item->id == ids->item_cooked_fish && metadata == 1) return ids->textures[MAAGE_BASE_TEXTURE_ITEMS + 58];
    if (item->id == ids->item_coal && metadata == 1) return ids->textures[MAAGE_BASE_TEXTURE_ITEMS + 34];
    return item->texture_id;
}
const char *maage_base_item_name(const maage_item_definition *item, unsigned int metadata) {
    const maage_base_ids *ids = (const maage_base_ids *) item->behavior_context;
    static const char *const logs[] = {"Oak Log", "Spruce Log", "Birch Log", "Jungle Log"};
    static const char *const leaves[] = {"Oak Leaves", "Spruce Leaves", "Birch Leaves"};
    static const char *const planks[] = {"Oak Planks", "Spruce Planks", "Birch Planks", "Jungle Planks", "Acacia Planks", "Dark Oak Planks"};
    static const char *const saplings[] = {"Oak Sapling", "Spruce Sapling", "Birch Sapling"};
    if (!item) return "";
    if (item->has_block && item->block_id == ids->block_sand && metadata == 1) return "Red Sand";
    if (item->has_block && item->block_id == ids->block_log2) return metadata ? "Dark Oak Log" : "Acacia Log";
    if (item->has_block && item->block_id == ids->block_log && metadata < 4) return logs[metadata];
    if (item->has_block && item->block_id == ids->block_planks && metadata < 6) return planks[metadata];
    if (item->id == ids->item_dye && metadata == 2) return "Cactus Green";
    if (item->id == ids->item_fish && metadata == 1) return "Raw Salmon";
    if (item->id == ids->item_fish && metadata == 2) return "Clownfish";
    if (item->id == ids->item_fish && metadata == 3) return "Pufferfish";
    if (item->id == ids->item_cooked_fish && metadata == 1) return "Cooked Salmon";
    if (metadata <= 2) {
        if (item->has_block && item->block_id == ids->block_log) return logs[metadata];
        if (item->has_block && item->block_id == ids->block_leaves) return leaves[metadata];
        if (item->has_block && item->block_id == ids->block_planks) return planks[metadata];
        if (item->id == ids->item_coal && metadata == 1) return "Charcoal";
        if (item->id == ids->item_sapling) return saplings[metadata];
    }
    return item->display_name;
}

maage_texture_id maage_base_logical_texture(const maage_base_ids *ids, maage_texture_id texture) {
    unsigned int slot;
    for (slot = 0; slot < MAAGE_BASE_TEXTURE_COUNT; ++slot)
        if (ids->textures[slot] == texture) return slot;
    return MAAGE_BASE_TEXTURE_MISSING;
}
