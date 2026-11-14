#include "base_internal.h"
#include "maage/creative.h"
#include <stdio.h>
#include <string.h>

static const char *const flora_names[MAAGE_BASE_FLORA_COUNT] = {"fern", "azure_bluet", "oxeye_daisy", "red_tulip", "orange_tulip", "white_tulip", "pink_tulip", "reeds", "double_tall_grass", "large_fern", "lilac", "rose_bush", "peony", "pumpkin"};
static const char *const flora_labels[MAAGE_BASE_FLORA_COUNT] = {"Fern", "Azure Bluet", "Oxeye Daisy", "Red Tulip", "Orange Tulip", "White Tulip", "Pink Tulip", "Sugar Canes", "Double Tall Grass", "Large Fern", "Lilac", "Rose Bush", "Peony", "Pumpkin"};
static const unsigned int flora_textures[MAAGE_BASE_FLORA_COUNT] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 10, 12, 14, 16, 18};

static maage_status flora_result(unsigned char code) {
    return maage_status_make(code, code == MAAGE_STATUS_SUCCESS || code == MAAGE_STATUS_NOOP ? 0 : MAAGE_CONTEXT_WORLD, MAAGE_OPERATION_UPDATE, 0);
}
int maage_base_flora_index(const maage_base_ids *ids, maage_block_id block) {
    unsigned int i;
    for (i = 0; i < MAAGE_BASE_FLORA_COUNT; ++i)
        if (ids->flora_blocks[i] == block) return (int) i;
    return -1;
}
maage_status maage_base_flora_textures(maage_content_context *context, maage_base_ids *ids) {
    static const char *const paths[] = {"fern", "flower_houstonia", "flower_oxeye_daisy", "flower_tulip_red", "flower_tulip_orange", "flower_tulip_white", "flower_tulip_pink", "reeds", "double_plant_grass_bottom", "double_plant_grass_top", "double_plant_fern_bottom", "double_plant_fern_top", "double_plant_syringa_bottom", "double_plant_syringa_top", "double_plant_rose_bottom", "double_plant_rose_top", "double_plant_paeonia_bottom", "double_plant_paeonia_top", "pumpkin_side", "pumpkin_top", "pumpkin_face_off"};
    maage_texture_definition texture;
    maage_status result;
    unsigned int i;
    char path[128];
    for (i = 0; i < sizeof(paths) / sizeof(paths[0]); ++i) {
        memset(&texture, 0, sizeof(texture));
        texture.fallback = MAAGE_CONTENT_ID_INVALID;
        sprintf(path, "minecraft/textures/blocks/%s.png", paths[i]);
        result = maage_presentation_source_register(context->presentation, path, &texture.source);
        if (!maage_status_failed(result)) result = maage_presentation_texture_register(context->presentation, &texture, &ids->textures[MAAGE_BASE_TEXTURE_FLORA + i]);
        if (maage_status_failed(result)) return result;
    }
    return flora_result(MAAGE_STATUS_SUCCESS);
}
static maage_status flora_reed_ground(const maage_base_ids *ids, const maage_world *world, int x, int y, int z, int *valid) {
    static const int sides[4][2] = {{-1, 0}, {1, 0}, {0, -1}, {0, 1}};
    const maage_block_registry *blocks = maage_world_registry(world);
    const maage_block_state_definition *state;
    unsigned int i;
    int nx, nz, missing = 0;
    *valid = 0;
    if (y <= 0) return flora_result(MAAGE_STATUS_SUCCESS);
    state = maage_block_registry_state(blocks, maage_world_get(world, x, y - 1, z));
    if (!state) return flora_result(MAAGE_STATUS_INVARIANT_VIOLATION);
    if (state->block_id == ids->flora_blocks[MAAGE_BASE_REEDS]) {
        *valid = 1;
        return flora_result(MAAGE_STATUS_SUCCESS);
    }
    if (!maage_base_state_tag(ids, state, ids->tag_soil) && !maage_base_state_tag(ids, state, ids->tag_sand)) return flora_result(MAAGE_STATUS_SUCCESS);
    for (i = 0; i < 4; ++i) {
        nx = x + sides[i][0];
        nz = z + sides[i][1];
        if (!maage_world_chunk_loaded(world, maage_world_chunk_coordinate(nx), maage_world_chunk_coordinate(nz))) {
            missing = 1;
            continue;
        }
        state = maage_block_registry_state(blocks, maage_world_get(world, nx, y - 1, nz));
        if (!state) return flora_result(MAAGE_STATUS_INVARIANT_VIOLATION);
        if (state->fluid_kind != ids->fluid_water) continue;
        *valid = 1;
        return flora_result(MAAGE_STATUS_SUCCESS);
    }
    return flora_result(missing ? MAAGE_STATUS_NOOP : MAAGE_STATUS_SUCCESS);
}
static maage_status flora_support_test(const void *context, const maage_world *world, int x, int y, int z, const maage_block_state_definition *state, unsigned int face, const maage_block_state_definition *neighbor, int *valid) {
    const maage_base_ids *ids = (const maage_base_ids *) context;
    const maage_block_registry *blocks = maage_world_registry(world);
    int upper = (int) maage_base_state_value(blocks, state, ids->property_upper);
    if (state->block_id == ids->flora_blocks[MAAGE_BASE_REEDS]) return flora_reed_ground(ids, world, x, y, z, valid);
    if (!upper && face == MAAGE_FACE_NEGATIVE_Y) *valid = maage_base_state_tag(ids, neighbor, ids->tag_soil);
    else *valid = neighbor->block_id == state->block_id && maage_base_state_value(blocks, neighbor, ids->property_upper) == (upper ? 0U : 1U);
    return flora_result(MAAGE_STATUS_SUCCESS);
}
static maage_status flora_support(const maage_world *world, int x, int y, int z, const maage_block_state_definition *state, maage_block_support_rule *rule) {
    const maage_base_ids *ids = maage_base_state_references(maage_world_registry(world), state);
    (void) x;
    (void) y;
    (void) z;
    rule->faces = 1U << MAAGE_FACE_NEGATIVE_Y;
    if (state->block_id != ids->flora_blocks[MAAGE_BASE_REEDS] && !maage_base_state_value(maage_world_registry(world), state, ids->property_upper)) rule->faces |= 1U << MAAGE_FACE_POSITIVE_Y;
    rule->mode = MAAGE_SUPPORT_ALL;
    rule->context = ids;
    rule->test = flora_support_test;
    return flora_result(MAAGE_STATUS_SUCCESS);
}
static maage_status flora_can_place(const maage_world *world, int x, int y, int z, const maage_block_state_definition *state, int *valid) {
    const maage_block_registry *blocks = maage_world_registry(world);
    const maage_base_ids *ids = maage_base_state_references(blocks, state);
    const maage_block_state_definition *ground;
    *valid = 0;
    if (state->block_id == ids->flora_blocks[MAAGE_BASE_REEDS]) return flora_reed_ground(ids, world, x, y, z, valid);
    if (y <= 0 || y >= MAAGE_WORLD_HEIGHT - 1 || maage_base_state_value(blocks, state, ids->property_upper)) return flora_result(MAAGE_STATUS_SUCCESS);
    ground = maage_block_registry_state(blocks, maage_world_get(world, x, y - 1, z));
    if (!ground) return flora_result(MAAGE_STATUS_INVARIANT_VIOLATION);
    *valid = maage_base_state_tag(ids, ground, ids->tag_soil) && maage_world_get(world, x, y + 1, z) == ids->state_air;
    return flora_result(MAAGE_STATUS_SUCCESS);
}
static maage_status flora_changed(maage_world *world, int x, int y, int z, const maage_block_state_definition *before, const maage_block_state_definition *after) {
    const maage_block_registry *blocks = maage_world_registry(world);
    const maage_base_ids *ids = maage_base_state_references(blocks, after);
    int index = maage_base_flora_index(ids, after->block_id);
    if (index < MAAGE_BASE_DOUBLE_GRASS || index > MAAGE_BASE_PEONY || before->block_id == after->block_id || maage_base_state_value(blocks, after, ids->property_upper) || maage_world_get(world, x, y, z) != after->id) return flora_result(MAAGE_STATUS_NOOP);
    /* Plans publish both halves before notifications. Ordinary placement
     * preflights the upper air cell; a failed mutation leaves this event pending. */
    if (y >= MAAGE_WORLD_HEIGHT - 1 || maage_world_get(world, x, y + 1, z) != ids->state_air) return flora_result(MAAGE_STATUS_NOOP);
    return maage_world_set(world, x, y + 1, z, ids->state_air, ids->flora_states[index][1]);
}
static maage_status flora_drops(const maage_block_registry *blocks, const maage_item_registry *items, const maage_block_state_definition *state, const maage_item_definition *tool, unsigned long *random, maage_item_drops *drops) {
    const maage_base_ids *ids = maage_base_state_references(blocks, state);
    int index = maage_base_flora_index(ids, state->block_id);
    int shears = tool && maage_traits_has(ids->traits, MAAGE_TRAIT_ITEM, tool->id, ids->tool_tags[MAAGE_TOOL_SHEARS]);
    (void) items;
    if (index < 0) return flora_result(MAAGE_STATUS_INVARIANT_VIOLATION);
    if (maage_base_state_value(blocks, state, ids->property_upper)) return flora_result(MAAGE_STATUS_SUCCESS);
    drops->stacks[0].item_id = ids->flora_items[index];
    drops->stacks[0].count = 1;
    if (index == MAAGE_BASE_FERN || index == MAAGE_BASE_DOUBLE_GRASS || index == MAAGE_BASE_LARGE_FERN) {
        drops->stacks[0].item_id = shears ? (index == MAAGE_BASE_DOUBLE_GRASS ? ids->item_tall_grass : ids->flora_items[MAAGE_BASE_FERN]) : ids->item_seeds;
        *random = (*random * 1664525UL + 1013904223UL) & 4294967295UL;
        drops->stacks[0].count = shears ? (index == MAAGE_BASE_FERN ? 1U : 2U) : (((*random >> 8) % 8UL) == 0 ? 1U : 0U);
    }
    return flora_result(MAAGE_STATUS_SUCCESS);
}
static maage_status flora_reed_tick(maage_world *world, int x, int y, int z, const maage_block_state_definition *state, maage_random_tick_context *context) {
    const maage_block_registry *blocks = maage_world_registry(world);
    const maage_base_ids *ids = maage_base_state_references(blocks, state);
    const maage_block_state_definition *below;
    maage_property_value age;
    maage_block_state_id cells[2];
    maage_block_plan plan;
    maage_status result;
    int valid, height;
    (void) context;
    result = flora_reed_ground(ids, world, x, y, z, &valid);
    if (maage_status_code(result) != MAAGE_STATUS_SUCCESS) return result;
    if (!valid) return maage_base_support_lost(world, x, y, z, state);
    if (y >= MAAGE_WORLD_HEIGHT - 1 || maage_world_get(world, x, y + 1, z) != ids->state_air) return flora_result(MAAGE_STATUS_NOOP);
    for (height = 1; height < 3 && y - height >= 0; ++height) {
        below = maage_block_registry_state(blocks, maage_world_get(world, x, y - height, z));
        if (!below) return flora_result(MAAGE_STATUS_INVARIANT_VIOLATION);
        if (below->block_id != state->block_id) break;
    }
    if (height >= 3) return flora_result(MAAGE_STATUS_NOOP);
    age = maage_base_state_value(blocks, state, ids->property_age);
    if (age < 15) return maage_world_set(world, x, y, z, state->id, ids->flora_states[MAAGE_BASE_REEDS][age + 1]);
    cells[0] = cells[1] = ids->flora_states[MAAGE_BASE_REEDS][0];
    plan.x = x;
    plan.y = y;
    plan.z = z;
    plan.edge = 1;
    plan.height = 2;
    plan.cells = cells;
    return maage_world_apply_plan(world, &plan);
}
maage_status maage_base_flora(maage_content_context *context, maage_base_ids *ids) {
    maage_block_definition block;
    maage_property_assignment property;
    maage_status result;
    char name[80];
    unsigned int i, state, count;
    int tall;
    for (i = 0; i < MAAGE_BASE_FLORA_COUNT; ++i) {
        tall = i >= MAAGE_BASE_DOUBLE_GRASS && i <= MAAGE_BASE_PEONY;
        block = *maage_block_registry_block(context->blocks, i == MAAGE_BASE_PUMPKIN ? ids->block_stone : ids->block_dandelion);
        block.id = MAAGE_CONTENT_ID_INVALID;
        block.required_tool = MAAGE_TOOL_AXE;
        block.special_tool_class = MAAGE_TOOL_NONE;
        block.tool_wear_class = MAAGE_TOOL_NONE;
        block.harvest_requires_tool = 0;
        block.drops = flora_drops;
        block.drop_count = 1;
        if (tall || i == MAAGE_BASE_REEDS) {
            block.survives = flora_support;
            block.can_place = flora_can_place;
            block.changed = tall ? flora_changed : NULL;
        }
        if (tall || i == MAAGE_BASE_REEDS) block.selection_shape.maximum[1] = 1;
        if (i == MAAGE_BASE_REEDS) block.random_tick = flora_reed_tick;
        if (i == MAAGE_BASE_FERN || i == MAAGE_BASE_DOUBLE_GRASS || i == MAAGE_BASE_LARGE_FERN) block.tool_wear_class = MAAGE_TOOL_SHEARS;
        if (i == MAAGE_BASE_PUMPKIN) {
            block.hardness = 1;
            block.material = ids->material_wood;
            block.required_tool = MAAGE_TOOL_AXE;
        }
        sprintf(name, "minecraft:%s", flora_names[i]);
        result = maage_base_register_block(context->blocks, name, &block);
        if (maage_status_failed(result)) return result;
        ids->flora_blocks[i] = block.id;
        count = tall ? 2U : (i == MAAGE_BASE_REEDS ? 16U : (i == MAAGE_BASE_PUMPKIN ? 4U : 1U));
        for (state = 0; state < count; ++state) {
            property.property = tall ? ids->property_upper : (i == MAAGE_BASE_REEDS ? ids->property_age : ids->property_facing);
            property.value = state;
            result = maage_content_register_state(context->blocks, ids, context->config, &ids->flora_states[i][state], block.id, count > 1 ? &property : NULL, count > 1 ? 1 : 0, MAAGE_BASE_TEXTURE_FLORA + flora_textures[i] + (tall ? state : 0));
            if (maage_status_failed(result)) return result;
        }
        result = base_block_item(context, name, flora_labels[i], block.id, ids->flora_states[i][0], 0, NULL, 0, &ids->flora_items[i]);
        if (maage_status_failed(result)) return result;
        block.drop_item_id = ids->flora_items[i];
        result = maage_block_registry_update_block(context->blocks, &block);
        if (maage_status_failed(result)) return result;
    }
    return flora_result(MAAGE_STATUS_SUCCESS);
}
maage_status maage_base_flora_creative(maage_content_context *context, const maage_base_ids *ids) {
    maage_creative_entry entry;
    maage_status result;
    unsigned int i;
    maage_creative_tab_id tab;
    result = maage_creative_tab_resolve(context->creative, "minecraft:decoration_blocks", &tab);
    if (maage_status_failed(result)) return result;
    for (i = 0; i < MAAGE_BASE_FLORA_COUNT; ++i) {
        memset(&entry, 0, sizeof(entry));
        entry.tab = tab;
        entry.stack.item_id = ids->flora_items[i];
        entry.stack.count = 1;
        entry.order = 1750 + (long) i;
        entry.search_order = entry.order;
        entry.searchable = 1;
        result = maage_creative_entry_register(context->creative, &entry);
        if (maage_status_failed(result)) return result;
    }
    return flora_result(MAAGE_STATUS_SUCCESS);
}
