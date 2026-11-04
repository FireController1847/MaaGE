#include "base_internal.h"
#include "maage/block_geometry.h"
#include <string.h>

static maage_status base_entity_geometry(const void *context, const maage_block_geometry_query *query, const maage_block_geometry_sink *sink) {
    (void) context;
    (void) query;
    (void) sink;
    return maage_status_make(MAAGE_STATUS_NOOP, 0, MAAGE_OPERATION_MESH, 0);
}
/* Cactus faces retain full texture extents; only each side's normal plane
 * is inset. Collision bounds do not describe its visual silhouette. */
static maage_status base_cactus_geometry(const void *context, const maage_block_geometry_query *query, const maage_block_geometry_sink *sink) {
    maage_block_shape bounds;
    maage_status result;
    unsigned int face, axis;
    (void) context;
    for (face = 0; face < MAAGE_FACE_COUNT; ++face) {
        for (axis = 0; axis < 3; ++axis) {
            bounds.minimum[axis] = 0;
            bounds.maximum[axis] = 1;
        }
        axis = face / 2;
        if (axis != 1) {
            bounds.minimum[axis] = 1.0 / 16;
            bounds.maximum[axis] = 15.0 / 16;
        }
        result = maage_block_geometry_cuboid(sink, &bounds, query->state->faces, 1U << face, query->block->mesh_material, 0);
        if (maage_status_failed(result)) return result;
    }
    return maage_status_make(MAAGE_STATUS_SUCCESS, 0, MAAGE_OPERATION_MESH, 0);
}
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
    result = maage_block_geometry_register(context->geometry, "minecraft:torch", &definition, &ids->geometry_torch);
    if (maage_status_failed(result)) return result;
    definition.primitive = MAAGE_BLOCK_GEOMETRY_CUSTOM;
    definition.emit = base_cactus_geometry;
    result = maage_block_geometry_register(context->geometry, "minecraft:cactus", &definition, &ids->geometry_cactus);
    if (maage_status_failed(result)) return result;
    definition.emit = base_entity_geometry;
    return maage_block_geometry_register(context->geometry, "minecraft:block_entity", &definition, &ids->geometry_entity);
}

maage_block_state_id maage_base_item_icon(const maage_item_definition *item, maage_item_variant_id variant) {
    const maage_base_ids *ids = (const maage_base_ids *) item->behavior_context;
    maage_item_variant_id species = variant;
    if (item->block_id == ids->block_log && species <= 3) return ids->logs[species];
    if (item->block_id == ids->block_leaves && species <= 2) return ids->leaves[species];
    if (item->block_id == ids->block_planks && variant < 6) return ids->planks[variant];
    if (item->block_id == ids->block_log2 && variant <= 1) return ids->logs2[variant];
    if (item->block_id == ids->block_sand && variant == 1) return ids->state_red_sand;
    return item->icon_state_id;
}
maage_texture_id maage_base_item_texture(const maage_item_definition *item, maage_item_variant_id variant) {
    const maage_base_ids *ids = item ? (const maage_base_ids *) item->behavior_context : NULL;
    if (!item) return MAAGE_BASE_TEXTURE_MISSING;
    if (item->id == ids->item_sapling && (variant == 1 || variant == 2)) return ids->textures[MAAGE_BASE_TEXTURE_ITEMS + 25 + variant];
    if (item->id == ids->item_fish && variant) return ids->textures[MAAGE_BASE_TEXTURE_ITEMS + 55 + variant - 1];
    if (item->id == ids->item_cooked_fish && variant == 1) return ids->textures[MAAGE_BASE_TEXTURE_ITEMS + 58];
    if (item->id == ids->item_coal && variant == 1) return ids->textures[MAAGE_BASE_TEXTURE_ITEMS + 34];
    return item->texture_id;
}
const char *maage_base_item_name(const maage_item_definition *item, maage_item_variant_id variant) {
    const maage_base_ids *ids;
    static const char *const logs[] = {"Oak Log", "Spruce Log", "Birch Log", "Jungle Log"};
    static const char *const leaves[] = {"Oak Leaves", "Spruce Leaves", "Birch Leaves"};
    static const char *const planks[] = {"Oak Planks", "Spruce Planks", "Birch Planks", "Jungle Planks", "Acacia Planks", "Dark Oak Planks"};
    static const char *const saplings[] = {"Oak Sapling", "Spruce Sapling", "Birch Sapling"};
    if (!item) return "";
    ids = (const maage_base_ids *) item->behavior_context;
    if (item->has_block && item->block_id == ids->block_sand && variant == 1) return "Red Sand";
    if (item->has_block && item->block_id == ids->block_log2) return variant ? "Dark Oak Log" : "Acacia Log";
    if (item->has_block && item->block_id == ids->block_log && variant < 4) return logs[variant];
    if (item->has_block && item->block_id == ids->block_planks && variant < 6) return planks[variant];
    if (item->id == ids->item_dye && variant == 2) return "Cactus Green";
    if (item->id == ids->item_fish && variant == 1) return "Raw Salmon";
    if (item->id == ids->item_fish && variant == 2) return "Clownfish";
    if (item->id == ids->item_fish && variant == 3) return "Pufferfish";
    if (item->id == ids->item_cooked_fish && variant == 1) return "Cooked Salmon";
    if (variant <= 2) {
        if (item->has_block && item->block_id == ids->block_log) return logs[variant];
        if (item->has_block && item->block_id == ids->block_leaves) return leaves[variant];
        if (item->has_block && item->block_id == ids->block_planks) return planks[variant];
        if (item->id == ids->item_coal && variant == 1) return "Charcoal";
        if (item->id == ids->item_sapling) return saplings[variant];
    }
    return item->display_name;
}

maage_texture_id maage_base_logical_texture(const maage_base_ids *ids, maage_texture_id texture) {
    unsigned int slot;
    for (slot = 0; slot < MAAGE_BASE_TEXTURE_COUNT; ++slot)
        if (ids->textures[slot] == texture) return slot;
    return MAAGE_BASE_TEXTURE_MISSING;
}
