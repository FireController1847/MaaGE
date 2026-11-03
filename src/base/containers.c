#include "base_internal.h"
#include "maage/containers.h"
#include <string.h>

static int base_table_access(const void *context, const maage_world *world, const maage_crafting_container *container) {
    return maage_base_table_access((const maage_base_ids *) context, world, container);
}
static int base_furnace_access(const void *context, const maage_world *world, const maage_crafting_container *container) {
    (void) context;
    return maage_base_furnace_access(world, container);
}
static maage_status base_furnace_action(const void *context, maage_world *world, struct maage_gameplay *game, unsigned long revision, unsigned int group, unsigned int slot, unsigned int action) {
    (void) context;
    return maage_base_furnace_action(world, game, revision, group, slot, action);
}
static maage_status base_furnace_sample(const void *context, const maage_world *world, maage_crafting_container *container) {
    maage_status result;
    (void) context;
    result = maage_base_furnace_sample(world, container);
    if (maage_status_failed(result)) return result;
    if (container->progress_count > 0) {
        container->progress[0].value = container->active_remaining;
        container->progress[0].total = container->active_duration ? container->active_duration : container->process_duration;
        container->progress[0].active = container->active_remaining != 0;
    }
    if (container->progress_count > 1) {
        container->progress[1].value = container->process;
        container->progress[1].total = container->process_duration;
        container->progress[1].active = container->active_remaining != 0;
    }
    return result;
}
static maage_status base_container_texture(maage_content_context *context, const char *path, maage_texture_id *id) {
    maage_texture_definition definition;
    maage_status result;
    memset(&definition, 0, sizeof(definition));
    definition.fallback = MAAGE_CONTENT_ID_INVALID;
    definition.frame_ticks = 1;
    definition.frame_edge = 256;
    result = maage_presentation_source_register(context->presentation, path, &definition.source);
    if (!maage_status_failed(result)) result = maage_presentation_texture_register(context->presentation, &definition, id);
    return result;
}
static void base_container_slot(maage_container_slot_definition *definition, unsigned int group, unsigned int slot, unsigned int storage, int x, int y) {
    definition->group = group;
    definition->slot = slot;
    definition->storage = storage;
    definition->flags = storage == MAAGE_CONTAINER_SLOT_OUTPUT ? MAAGE_CONTAINER_SLOT_EXTRACT | MAAGE_CONTAINER_SLOT_REVISION : MAAGE_CONTAINER_SLOT_INSERT | MAAGE_CONTAINER_SLOT_EXTRACT;
    definition->x = x;
    definition->y = y;
}
static void base_container_presentation(maage_container_definition *definition) {
    memset(definition, 0, sizeof(*definition));
    definition->presentation.width = 176;
    definition->presentation.height = 166;
    definition->presentation.texture_edge = 256;
    definition->presentation.creative_tabs = MAAGE_CONTENT_ID_INVALID;
    definition->presentation.inventory_x = 8;
    definition->presentation.inventory_y = 72;
    definition->presentation.show_inventory = 1;
    definition->presentation.inventory_slot_x = 8;
    definition->presentation.inventory_slot_y = 84;
    definition->presentation.hotbar_y = 142;
}
maage_status maage_base_containers(maage_content_context *context, maage_base_ids *ids) {
    maage_container_definition definition;
    maage_container_slot_definition player_slots[5], table_slots[10], furnace_slots[3], chest_slots[54];
    maage_container_progress_definition progress[2];
    maage_texture_id inventory, table, furnace, tabs, chest;
    maage_status result;
    unsigned int i, large, rows;
    result = base_container_texture(context, "minecraft/textures/gui/container/inventory.png", &inventory);
    if (!maage_status_failed(result)) result = base_container_texture(context, "minecraft/textures/gui/container/crafting_table.png", &table);
    if (!maage_status_failed(result)) result = base_container_texture(context, "minecraft/textures/gui/container/furnace.png", &furnace);
    if (!maage_status_failed(result)) result = base_container_texture(context, "minecraft/textures/gui/container/creative_inventory/tabs.png", &tabs);
    if (!maage_status_failed(result)) result = base_container_texture(context, "minecraft/textures/gui/container/generic_54.png", &chest);
    if (maage_status_failed(result)) return result;

    base_container_presentation(&definition);
    for (i = 0; i < 4; ++i)
        base_container_slot(&player_slots[i], MAAGE_SLOT_CRAFT_INPUT, i, MAAGE_CONTAINER_SLOT_INPUT, 88 + (int) (i % 2) * 18, 26 + (int) (i / 2) * 18);
    base_container_slot(&player_slots[4], MAAGE_SLOT_CRAFT_RESULT, 0, MAAGE_CONTAINER_SLOT_OUTPUT, 144, 36);
    definition.slots = player_slots;
    definition.slot_count = 5;
    definition.grid_width = 2;
    definition.personal = 1;
    definition.creative_catalog = 1;
    definition.presentation.background = inventory;
    definition.presentation.creative_tabs = tabs;
    definition.presentation.creative_width = 195;
    definition.presentation.creative_height = 136;
    definition.presentation.title = "Crafting";
    definition.presentation.title_x = 86;
    definition.presentation.title_y = 16;
    definition.presentation.player_preview = 1;
    result = maage_container_register(context->containers, "minecraft:inventory", &definition);
    if (maage_status_failed(result)) return result;
    ids->container_player = definition.id;
    result = maage_container_bind_inventory(context->containers, definition.id);
    if (maage_status_failed(result)) return result;

    base_container_presentation(&definition);
    for (i = 0; i < 9; ++i)
        base_container_slot(&table_slots[i], MAAGE_SLOT_CRAFT_INPUT, i, MAAGE_CONTAINER_SLOT_INPUT, 30 + (int) (i % 3) * 18, 17 + (int) (i / 3) * 18);
    base_container_slot(&table_slots[9], MAAGE_SLOT_CRAFT_RESULT, 0, MAAGE_CONTAINER_SLOT_OUTPUT, 124, 35);
    definition.slots = table_slots;
    definition.slot_count = 10;
    definition.grid_width = 3;
    definition.context = ids;
    definition.accessible = base_table_access;
    definition.presentation.background = table;
    definition.presentation.title = "Crafting";
    definition.presentation.title_x = 28;
    definition.presentation.title_y = 6;
    definition.presentation.inventory_title = "Inventory";
    result = maage_container_register(context->containers, "minecraft:crafting_table", &definition);
    if (maage_status_failed(result)) return result;
    ids->container_table = definition.id;

    base_container_presentation(&definition);
    base_container_slot(&furnace_slots[0], MAAGE_SLOT_CRAFT_INPUT, 0, MAAGE_CONTAINER_SLOT_INPUT, 56, 17);
    base_container_slot(&furnace_slots[1], MAAGE_SLOT_CRAFT_INPUT, 1, MAAGE_CONTAINER_SLOT_INPUT, 56, 53);
    base_container_slot(&furnace_slots[2], MAAGE_SLOT_CRAFT_RESULT, 0, MAAGE_CONTAINER_SLOT_OUTPUT, 116, 35);
    memset(progress, 0, sizeof(progress));
    progress[0].texture = furnace;
    progress[0].uv[0] = 176.0f / 256;
    progress[0].uv[2] = 190.0f / 256;
    progress[0].uv[3] = 13.0f / 256;
    progress[0].x = 56;
    progress[0].y = 36;
    progress[0].width = 14;
    progress[0].height = 13;
    progress[0].steps = 13;
    progress[0].extra_pixel = 1;
    progress[0].vertical = progress[0].require_active = 1;
    progress[1].texture = furnace;
    progress[1].uv[0] = 176.0f / 256;
    progress[1].uv[1] = 14.0f / 256;
    progress[1].uv[2] = 201.0f / 256;
    progress[1].uv[3] = 30.0f / 256;
    progress[1].x = 79;
    progress[1].y = 34;
    progress[1].width = 25;
    progress[1].height = 16;
    progress[1].steps = 24;
    progress[1].extra_pixel = progress[1].require_active = 1;
    definition.slots = furnace_slots;
    definition.slot_count = 3;
    definition.progress = progress;
    definition.progress_count = 2;
    definition.accessible = base_furnace_access;
    definition.action = base_furnace_action;
    definition.sample = base_furnace_sample;
    definition.presentation.background = furnace;
    definition.presentation.title = "Furnace";
    definition.presentation.title_x = 88;
    definition.presentation.title_y = 6;
    definition.presentation.title_centered = 1;
    definition.presentation.inventory_title = "Inventory";
    result = maage_container_register(context->containers, "minecraft:furnace", &definition);
    if (maage_status_failed(result)) return result;
    ids->container_furnace = definition.id;
    for (large = 0; large < 2; ++large) {
        rows = large ? 6 : 3;
        base_container_presentation(&definition);
        memset(chest_slots, 0, sizeof(chest_slots));
        for (i = 0; i < rows * 9; ++i) {
            base_container_slot(&chest_slots[i], MAAGE_SLOT_CRAFT_INPUT, i, 0, 8 + (int) (i % 9) * 18, 18 + (int) (i / 9) * 18);
            chest_slots[i].flags = MAAGE_CONTAINER_SLOT_INSERT | MAAGE_CONTAINER_SLOT_EXTRACT;
        }
        definition.context = ids;
        definition.slots = chest_slots;
        definition.slot_count = rows * 9;
        definition.opened = maage_base_chest_opened;
        definition.closed = maage_base_chest_closed;
        definition.accessible = maage_base_chest_access;
        definition.action = maage_base_chest_action;
        definition.sample = maage_base_chest_sample;
        definition.presentation.background = chest;
        definition.presentation.height = rows * 18 + 114;
        definition.presentation.background_split = rows * 18 + 17;
        definition.presentation.background_bottom = 126;
        definition.presentation.title = large ? "Large Chest" : "Chest";
        definition.presentation.title_x = 8;
        definition.presentation.title_y = 6;
        definition.presentation.inventory_title = "Inventory";
        definition.presentation.inventory_y = (int) rows * 18 + 20;
        definition.presentation.inventory_slot_y = (int) rows * 18 + 31;
        definition.presentation.hotbar_y = (int) rows * 18 + 89;
        result = maage_container_register(context->containers, large ? "minecraft:large_chest" : "minecraft:chest", &definition);
        if (maage_status_failed(result)) return result;
        ids->container_chest[large] = definition.id;
    }
    return result;
}
