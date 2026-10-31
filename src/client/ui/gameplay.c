#include "internal.h"
#include "../resources.h"
#include "../item_models.h"
#include "maage/time.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int maage_client_ui_creative_catalog(const maage_client_ui *ui);

static void maage_client_ui_block_uv(const maage_client_ui *ui, const float source[4], float uv[4]) {
    uv[0] = source[0] * ui->resources.block_atlas_width / ui->resources.atlas_edge;
    uv[2] = source[2] * ui->resources.block_atlas_width / ui->resources.atlas_edge;
    uv[1] = (MAAGE_CLIENT_UI_BLOCK_ATLAS_Y + source[1] * ui->resources.block_atlas_height) / ui->resources.atlas_edge;
    uv[3] = (MAAGE_CLIENT_UI_BLOCK_ATLAS_Y + source[3] * ui->resources.block_atlas_height) / ui->resources.atlas_edge;
}

static void maage_client_ui_stack(maage_client_ui *ui, const maage_item_stack *stack, float x, float y) {
    maage_render_ui_vertex vertices[24];
    maage_client_ui_rect rectangle;
    const maage_item_definition *item;
    size_t quads, i;
    float uv[4], fraction;
    char count[16];
    if (!stack || !stack->count || !ui->items || !ui->blocks) return;
    item = maage_item_registry_get(ui->items, stack->item_id);
    if (!item) return;
    quads = maage_client_item_icon(ui->items, ui->blocks, stack, ui->inventory_grass_color, ui->item_uv, vertices);
    for (i = 0; i < quads * 4; ++i) {
        vertices[i].position[0] += x;
        vertices[i].position[1] += y;
        uv[0] = uv[2] = vertices[i].uv[0];
        uv[1] = uv[3] = vertices[i].uv[1];
        maage_client_ui_block_uv(ui, uv, uv);
        vertices[i].uv[0] = uv[0];
        vertices[i].uv[1] = uv[1];
    }
    maage_client_ui_append_quads(ui, vertices, quads);
    if (item->durability && stack->damage) {
        rectangle.x = x + 2;
        rectangle.y = y + 13;
        rectangle.width = 13;
        rectangle.height = 2;
        maage_client_ui_panel(ui, rectangle, 0, 0, 0, 1);
        fraction = 1.0f - (float) stack->damage / item->durability;
        rectangle.width = (float) (int) (13 * fraction + 0.5f);
        rectangle.height = 1;
        maage_client_ui_panel(ui, rectangle, 1 - fraction, fraction, 0, 1);
    }
    if (stack->count > 1) {
        sprintf(count, "%u", stack->count);
        maage_client_ui_text(ui, count, x + 18, y + 10, MAAGE_CLIENT_UI_RIGHT, 0.18f, 0.18f, 0.18f);
        maage_client_ui_text(ui, count, x + 17, y + 9, MAAGE_CLIENT_UI_RIGHT, 1, 1, 1);
    }
}

/* Cosmetic variation depends only on the displayed owner tick and icon. It
 * stays stable between snapshots and never consumes gameplay randomness. */
static unsigned int maage_client_ui_vital_jitter(unsigned long tick, unsigned int icon, unsigned int range) {
    unsigned long value = (tick ^ ((icon + 1UL) * 2654435761UL)) & 4294967295UL;
    value ^= value >> 16;
    value = (value * 1664525UL + 1013904223UL) & 4294967295UL;
    return (unsigned int) ((value >> 16) % range);
}

static void maage_client_ui_status(maage_client_ui *ui, float x, float y, unsigned int value, int hunger) {
    maage_client_ui_rect rectangle;
    unsigned int i, filled, previous, remaining;
    unsigned long age, tick = ui->snapshot.tick;
    int flashing = 0;
    float v, u;
    if (value > 20) value = 20;
    previous = ui->health_before_damage > 20 ? 20 : ui->health_before_damage;
    if (!hunger && ui->health_flashing && tick >= ui->health_flash_tick) {
        age = tick - ui->health_flash_tick;
        if (age <= 10) {
            remaining = 20U - (unsigned int) age;
            flashing = (remaining / 3U) % 2U != 0;
        }
    }
    rectangle.width = rectangle.height = 9;
    v = (float) (hunger ? 27 : 0) / ui->resources.atlas_edge;
    for (i = 0; i < 10; ++i) {
        rectangle.x = hunger ? x - (float) i * 8 : x + (float) i * 8;
        rectangle.y = y;
        if (hunger) {
            if (ui->snapshot.player.saturation <= 0 && tick % (value * 3UL + 1UL) == 0) rectangle.y += (int) maage_client_ui_vital_jitter(tick, i + 10, 3) - 1;
        } else if (value <= 4) rectangle.y += maage_client_ui_vital_jitter(tick, i, 2);
        u = (float) (flashing ? 25 : 16) / ui->resources.atlas_edge;
        maage_client_ui_rectangle(ui, rectangle, u, v, u + 9.0f / ui->resources.atlas_edge, v + 9.0f / ui->resources.atlas_edge, 1, 1, 1, 1);
        if (flashing && previous > i * 2) {
            filled = previous - i * 2;
            u = (float) (filled >= 2 ? 70 : 79) / ui->resources.atlas_edge;
            maage_client_ui_rectangle(ui, rectangle, u, v, u + 9.0f / ui->resources.atlas_edge, v + 9.0f / ui->resources.atlas_edge, 1, 1, 1, 1);
        }
        filled = value > i * 2 ? value - i * 2 : 0;
        if (!filled) continue;
        u = (float) (filled >= 2 ? 52 : 61) / ui->resources.atlas_edge;
        maage_client_ui_rectangle(ui, rectangle, u, v, u + 9.0f / ui->resources.atlas_edge, v + 9.0f / ui->resources.atlas_edge, 1, 1, 1, 1);
    }
}

static void maage_client_ui_air(maage_client_ui *ui, float x, float y) {
    maage_client_ui_rect rectangle;
    int filled, count, i;
    float u, edge = (float) ui->resources.atlas_edge;
    if (!ui->snapshot.player.submerged) return;
    filled = (int) ceil((ui->snapshot.player.air - 2) / 30.0);
    count = (int) ceil(ui->snapshot.player.air / 30.0);
    if (filled < 0) filled = 0;
    if (count > 10) count = 10;
    rectangle.width = rectangle.height = 9;
    rectangle.y = y;
    for (i = 0; i < count; ++i) {
        rectangle.x = x - i * 8;
        u = (float) (i < filled ? 16 : 25) / edge;
        maage_client_ui_rectangle(ui, rectangle, u, 18.0f / edge, u + 9.0f / edge, 27.0f / edge, 1, 1, 1, 1);
    }
}

void maage_client_ui_gameplay_draw(maage_client_ui *ui) {
    maage_client_ui_rect rectangle;
    const maage_item_stack *selected;
    const maage_item_definition *item;
    float x, y;
    unsigned int i;
    if (!ui->has_snapshot || !ui->items) return;
    x = (float) (int) (ui->content.x + (ui->content.width - 182) * 0.5f);
    y = ui->content.y + ui->content.height - 22;
    rectangle.x = x;
    rectangle.y = y;
    rectangle.width = 182;
    rectangle.height = 22;
    maage_client_ui_rectangle(ui, rectangle, 256.0f / ui->resources.atlas_edge, 0, 438.0f / ui->resources.atlas_edge, 22.0f / ui->resources.atlas_edge, 1, 1, 1, 1);
    rectangle.x = x - 1 + (float) ui->snapshot.player.inventory.selected * 20;
    rectangle.y = y - 1;
    rectangle.width = 24;
    rectangle.height = 22;
    maage_client_ui_rectangle(ui, rectangle, 256.0f / ui->resources.atlas_edge, 22.0f / ui->resources.atlas_edge, 280.0f / ui->resources.atlas_edge, 44.0f / ui->resources.atlas_edge, 1, 1, 1, 1);
    for (i = 0; i < MAAGE_HOTBAR_SLOTS; ++i)
        maage_client_ui_stack(ui, &ui->snapshot.player.inventory.slots[i], x + 3 + (float) i * 20, y + 3);
    if (ui->snapshot.player.game_mode == MAAGE_GAME_MODE_SURVIVAL) {
        /* Vitals sit directly above the hotbar; no experience row is rendered. */
        maage_client_ui_status(ui, x, y - 11, ui->snapshot.player.health, 0);
        maage_client_ui_status(ui, x + 173, y - 11, ui->snapshot.player.hunger, 1);
        maage_client_ui_air(ui, x + 173, y - 21);
    }
    selected = maage_inventory_selected(&ui->snapshot.player.inventory);
    item = selected && selected->count ? maage_item_registry_get(ui->items, selected->item_id) : NULL;
    if (item && !maage_client_ui_modal(ui) && ui->highlight_until > ui->notice_now) {
        size_t first = ui->batch.vertex_count, n;
        float alpha = (float) ((ui->highlight_until - ui->notice_now) * 2.0);
        float name_y = ui->content.y + ui->content.height - 59 + (ui->snapshot.player.game_mode == MAAGE_GAME_MODE_CREATIVE ? 14 : 0);
        if (alpha > 1.0f) alpha = 1.0f;
        maage_client_ui_text(ui, maage_client_item_name(ui->items, selected), x + 92, name_y + 1, MAAGE_CLIENT_UI_CENTER, 0.25f, 0.25f, 0.25f);
        maage_client_ui_text(ui, maage_client_item_name(ui->items, selected), x + 91, name_y, MAAGE_CLIENT_UI_CENTER, 1, 1, 1);
        for (n = first; n < ui->batch.vertex_count; ++n)
            ui->vertices[n].color[3] *= alpha;
    }
}

static maage_client_ui_rect maage_client_ui_inventory_frame(const maage_client_ui *ui) {
    maage_client_ui_rect rectangle;
    rectangle.width = maage_client_ui_creative_catalog(ui) ? 195 : 176;
    rectangle.height = maage_client_ui_creative_catalog(ui) ? 136 : 166;
    rectangle.x = (float) (int) (ui->content.x + (ui->content.width - rectangle.width) * 0.5f);
    rectangle.y = (float) (int) (ui->content.y + (ui->content.height - rectangle.height) * 0.5f);
    return rectangle;
}

static maage_client_ui_rect maage_client_ui_inventory_slot(maage_client_ui_rect frame, unsigned int slot) {
    maage_client_ui_rect rectangle;
    rectangle.width = rectangle.height = 16;
    rectangle.x = frame.x + (frame.width == 195 ? 9 : 8) + (float) (slot % 9) * 18;
    rectangle.y = frame.y + (frame.width == 195 ? 112 : (slot < 9 ? 142 : 84 + (float) ((slot - 9) / 9) * 18));
    return rectangle;
}

static int maage_client_ui_slot_contains(const maage_client_ui *ui, maage_client_ui_rect rectangle) {
    return ui->mouse_x >= rectangle.x && ui->mouse_y >= rectangle.y && ui->mouse_x < rectangle.x + rectangle.width && ui->mouse_y < rectangle.y + rectangle.height;
}

static int maage_client_ui_creative_catalog(const maage_client_ui *ui) {
    return ui->snapshot.player.game_mode == MAAGE_GAME_MODE_CREATIVE && ui->snapshot.player.crafting.kind != MAAGE_CONTAINER_TABLE;
}
static maage_client_ui_rect maage_client_ui_crafting_slot(maage_client_ui_rect frame, int table, unsigned int slot, int result) {
    maage_client_ui_rect rectangle;
    rectangle.width = rectangle.height = 16;
    rectangle.x = frame.x + (result ? (table ? 124 : 144) : (table ? 30 : 88) + (float) (slot % (table ? 3U : 2U)) * 18);
    rectangle.y = frame.y + (result ? (table ? 35 : 36) : (table ? 17 : 26) + (float) (slot / (table ? 3U : 2U)) * 18);
    return rectangle;
}
static void maage_client_ui_container_slot(maage_client_ui *ui, const maage_client_ui_input *input, unsigned int group, unsigned int slot) {
    maage_session_command command;
    memset(&command, 0, sizeof(command));
    command.kind = MAAGE_CMD_CONTAINER_SLOT;
    command.container_identity = ui->snapshot.player.crafting.identity;
    command.container_revision = ui->snapshot.player.crafting.revision;
    command.slot_group = group;
    command.container_slot = slot;
    command.slot_action = input->shift ? MAAGE_SLOT_QUICK_MOVE : (input->secondary_pressed ? MAAGE_SLOT_SECONDARY : MAAGE_SLOT_PRIMARY);
    ui->route_result = maage_client_ui_queue_command(ui, &command);
}

static unsigned int maage_client_ui_inventory_input(maage_client_ui *ui, const maage_client_ui_input *input, void *context) {
    maage_client_ui_rect frame, rectangle;
    const maage_item_definition *item;
    unsigned int slot;
    int table;
    size_t count, first;
    (void) context;
    /* Inventory slot operations are silent; an
     * inventory command is not a local menu-button activation. */
    if (ui->has_snapshot && maage_client_ui_creative_catalog(ui)) {
        unsigned int rows = (unsigned int) ((ui->creative_count + 8) / 9), maximum = rows > 5 ? rows - 5 : 0;
        maage_client_ui_rect r = maage_client_ui_inventory_frame(ui);
        if (input->scroll > 0 && ui->creative_page) --ui->creative_page;
        if (input->scroll < 0 && ui->creative_page < maximum) ++ui->creative_page;
        r.x += 175;
        r.y += 18;
        r.width = 12;
        r.height = 112;
        if (input->primary_pressed && maage_client_ui_slot_contains(ui, r)) ui->creative_dragging = 1;
        if (!input->primary_down) ui->creative_dragging = 0;
        if (ui->creative_dragging) {
            float fraction = (ui->mouse_y - r.y - 7.5f) / (r.height - 15);
            if (fraction < 0) fraction = 0;
            if (fraction > 1) fraction = 1;
            ui->creative_page = (unsigned int) (fraction * maximum + 0.5f);
        }
    }
    if (!ui->has_snapshot || !ui->items || ui->container_close_requested ||
        !ui->snapshot.player.crafting.kind || ui->snapshot.player.crafting.closing ||
        (!input->primary_pressed && !input->secondary_pressed)) return 0;
    frame = maage_client_ui_inventory_frame(ui);
    for (slot = 0; slot < (maage_client_ui_creative_catalog(ui) ? 9U : MAAGE_INVENTORY_SLOTS); ++slot) {
        rectangle = maage_client_ui_inventory_slot(frame, slot);
        if (maage_client_ui_slot_contains(ui, rectangle)) {
            maage_client_ui_container_slot(ui, input, MAAGE_SLOT_INVENTORY, slot);
            return 0;
        }
    }
    table = ui->snapshot.player.crafting.kind == MAAGE_CONTAINER_TABLE;
    if (!maage_client_ui_creative_catalog(ui)) {
        for (slot = 0; slot < (table ? 9U : 4U); ++slot) {
            rectangle = maage_client_ui_crafting_slot(frame, table, slot, 0);
            if (maage_client_ui_slot_contains(ui, rectangle)) {
                maage_client_ui_container_slot(ui, input, MAAGE_SLOT_CRAFT_INPUT, slot);
                return 0;
            }
        }
        rectangle = maage_client_ui_crafting_slot(frame, table, 0, 1);
        if (maage_client_ui_slot_contains(ui, rectangle)) {
            if (ui->container_command_pending) return 0;
            maage_client_ui_container_slot(ui, input, MAAGE_SLOT_CRAFT_RESULT, 0);
            return 0;
        }
    }
    if (ui->snapshot.player.inventory.cursor.count && !maage_client_ui_slot_contains(ui, frame)) {
        maage_session_command command;
        memset(&command, 0, sizeof(command));
        command.kind = MAAGE_CMD_DROP;
        command.inventory_slot = MAAGE_INVENTORY_SLOTS;
        command.container_identity = ui->snapshot.player.crafting.identity;
        command.secondary_click = !input->secondary_pressed;
        ui->route_result = maage_client_ui_queue_command(ui, &command);
        return 0;
    }
    if (maage_client_ui_creative_catalog(ui)) {
        count = ui->creative_count;
        first = (size_t) ui->creative_page * 9;
        for (slot = 0; slot < 45 && first + slot < count; ++slot) {
            rectangle.x = frame.x + 9 + (float) (slot % 9) * 18;
            rectangle.y = frame.y + 18 + (float) (slot / 9) * 18;
            rectangle.width = rectangle.height = 16;
            if (maage_client_ui_slot_contains(ui, rectangle)) {
                item = maage_item_registry_get(ui->items, ui->creative_items[first + slot].item_id);
                if (item) {
                    maage_session_command command;
                    memset(&command, 0, sizeof(command));
                    command.kind = MAAGE_CMD_CREATIVE_PICK;
                    command.container_identity = ui->snapshot.player.crafting.identity;
                    command.item_id = item->id;
                    command.item_metadata = ui->creative_items[first + slot].metadata;
                    ui->route_result = maage_client_ui_queue_command(ui, &command);
                }
                return 0;
            }
        }
    }
    return 0;
}

static void maage_client_ui_inventory_draw(maage_client_ui *ui, void *context) {
    maage_client_ui_rect frame, rectangle;
    maage_item_stack stack;
    const maage_item_definition *item;
    const char *hover = NULL;
    size_t count, first;
    unsigned int slot;
    (void) context;
    if (!ui->has_snapshot || !ui->items) return;
    frame = maage_client_ui_inventory_frame(ui);
    {
        float atlas_x = maage_client_ui_creative_catalog(ui) ? 1792.0f : (ui->snapshot.player.crafting.kind == MAAGE_CONTAINER_TABLE ? 1536.0f : 256.0f);
        maage_client_ui_rectangle(ui, frame, atlas_x / ui->resources.atlas_edge, 256.0f / ui->resources.atlas_edge, (atlas_x + frame.width) / ui->resources.atlas_edge, (256.0f + frame.height) / ui->resources.atlas_edge, 1, 1, 1, 1);
    }
    if (maage_client_ui_creative_catalog(ui)) {
        maage_client_ui_text(ui, "Items", frame.x + 8, frame.y + 6, MAAGE_CLIENT_UI_LEFT, 0.25f, 0.25f, 0.25f);
        count = ui->creative_count;
        first = (size_t) ui->creative_page * 9;
        for (slot = 0; slot < 45 && first + slot < count; ++slot) {
            item = maage_item_registry_get(ui->items, ui->creative_items[first + slot].item_id);
            rectangle.x = frame.x + 9 + (float) (slot % 9) * 18;
            rectangle.y = frame.y + 18 + (float) (slot / 9) * 18;
            rectangle.width = rectangle.height = 16;
            if (!item) continue;
            stack = ui->creative_items[first + slot];
            maage_client_ui_stack(ui, &stack, rectangle.x, rectangle.y);
            if (maage_client_ui_slot_contains(ui, rectangle)) {
                maage_client_ui_panel(ui, rectangle, 1, 1, 1, 0.5f);
                hover = maage_client_item_name(ui->items, &stack);
            }
        }
        {
            unsigned int rows = (unsigned int) ((count + 8) / 9), maximum = rows > 5 ? rows - 5 : 0;
            float u = 1792.0f + (maximum ? 232.0f : 244.0f);
            rectangle.x = frame.x + 175;
            rectangle.y = frame.y + 18 + (maximum ? (float) (int) (97.0f * ui->creative_page / maximum) : 0);
            rectangle.width = 12;
            rectangle.height = 15;
            maage_client_ui_rectangle(ui, rectangle, u / ui->resources.atlas_edge, 0, (u + 12) / ui->resources.atlas_edge, 15.0f / ui->resources.atlas_edge, 1, 1, 1, 1);
        }
    } else {
        int table = ui->snapshot.player.crafting.kind == MAAGE_CONTAINER_TABLE;
        const maage_item_stack *source;
        maage_client_ui_text(ui, "Crafting", frame.x + (table ? 28 : 86), frame.y + (table ? 6 : 16),
                             MAAGE_CLIENT_UI_LEFT, 0.25f, 0.25f, 0.25f);
        if (table) maage_client_ui_text(ui, "Inventory", frame.x + 8, frame.y + 72, MAAGE_CLIENT_UI_LEFT, 0.25f, 0.25f, 0.25f);
        else
            maage_client_ui_player_preview(ui, frame);
        for (slot = 0; slot <= (table ? 9U : 4U); ++slot) {
            int result = slot == (table ? 9U : 4U);
            rectangle = maage_client_ui_crafting_slot(frame, table, result ? 0 : slot, result);
            source = result ? &ui->snapshot.player.crafting.output : &ui->snapshot.player.crafting.inputs[slot];
            /* Inputs and result belong to one copied revision. Pending input
             * restricts result actions, never just one slot's presentation. */
            maage_client_ui_stack(ui, source, rectangle.x, rectangle.y);
            if (maage_client_ui_slot_contains(ui, rectangle)) {
                maage_client_ui_panel(ui, rectangle, 1, 1, 1, 0.5f);
                if (source->count) hover = maage_client_item_name(ui->items, source);
            }
        }
    }
    for (slot = 0; slot < (maage_client_ui_creative_catalog(ui) ? 9U : MAAGE_INVENTORY_SLOTS); ++slot) {
        rectangle = maage_client_ui_inventory_slot(frame, slot);
        maage_client_ui_stack(ui, &ui->snapshot.player.inventory.slots[slot], rectangle.x, rectangle.y);
        if (maage_client_ui_slot_contains(ui, rectangle)) {
            maage_client_ui_panel(ui, rectangle, 1, 1, 1, 0.5f);
            if (ui->snapshot.player.inventory.slots[slot].count) hover = maage_client_item_name(ui->items, &ui->snapshot.player.inventory.slots[slot]);
        }
    }
    if (!ui->snapshot.player.inventory.cursor.count && hover && ui->content.width > 8 && ui->content.height > 16) {
        rectangle.x = ui->mouse_x + 12;
        rectangle.y = ui->mouse_y - 12;
        rectangle.width = maage_client_ui_text_width(ui, hover);
        rectangle.height = 8;
        if (rectangle.width > ui->content.width - 8) rectangle.width = ui->content.width - 8;
        if (rectangle.x + rectangle.width + 4 > ui->content.x + ui->content.width) rectangle.x -= 28 + rectangle.width;
        if (rectangle.x < ui->content.x + 4) rectangle.x = ui->content.x + 4;
        if (rectangle.x + rectangle.width + 4 > ui->content.x + ui->content.width) rectangle.x = ui->content.x + ui->content.width - rectangle.width - 4;
        if (rectangle.y < ui->content.y + 4) rectangle.y = ui->content.y + 4;
        if (rectangle.y + rectangle.height + 4 > ui->content.y + ui->content.height) rectangle.y = ui->content.y + ui->content.height - rectangle.height - 4;
        {
            maage_client_ui_rect panel = rectangle;
            panel.x -= 4;
            panel.y -= 4;
            panel.width += 8;
            panel.height += 8;
            maage_client_ui_panel(ui, panel, 0.0627f, 0, 0.0627f, 0.94f);
            panel.x += 1;
            panel.y += 1;
            panel.width -= 2;
            panel.height = 1;
            maage_client_ui_gradient(ui, panel, 0x505000FFUL, 0x505000FFUL);
            panel.y += 13;
            maage_client_ui_gradient(ui, panel, 0x5028007FUL, 0x5028007FUL);
            panel.y -= 12;
            panel.width = 1;
            panel.height = 12;
            maage_client_ui_gradient(ui, panel, 0x505000FFUL, 0x5028007FUL);
            panel.x += rectangle.width + 5;
            maage_client_ui_gradient(ui, panel, 0x505000FFUL, 0x5028007FUL);
        }
        {
            maage_client_ui_rect previous_clip = ui->clip;
            ui->clip = rectangle;
            ui->clip.width += 1;
            ui->clip.height += 1;
            maage_client_ui_text(ui, hover, rectangle.x + 1, rectangle.y + 1, MAAGE_CLIENT_UI_LEFT, 0.25f, 0.25f, 0.25f);
            maage_client_ui_text(ui, hover, rectangle.x, rectangle.y, MAAGE_CLIENT_UI_LEFT, 1, 1, 1);
            ui->clip = previous_clip;
        }
    }
    maage_client_ui_stack(ui, &ui->snapshot.player.inventory.cursor, ui->mouse_x - 8, ui->mouse_y - 8);
}

maage_status maage_client_ui_inventory_register(maage_client_ui *ui) {
    maage_status result;
    result = maage_client_ui_register_screen(ui, MAAGE_CLIENT_UI_INVENTORY, maage_client_ui_inventory_draw, maage_client_ui_inventory_input, NULL);
    if (maage_status_failed(result)) return result;
    return maage_client_ui_register_screen(ui, MAAGE_CLIENT_UI_CRAFTING_TABLE, maage_client_ui_inventory_draw, maage_client_ui_inventory_input, NULL);
}

maage_status maage_client_ui_set_content(maage_client_ui *ui, const maage_item_registry *items, const maage_block_registry *blocks, const float inventory_grass_color[3], const float (*item_uv)[4]) {
    maage_item_stack *creative;
    const maage_item_definition *item;
    size_t i, count = 0, index = 0, variants, metadata;
    if (!ui || !items || !blocks || !inventory_grass_color || !item_uv) return maage_status_make(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_CONTEXT_GUI, MAAGE_OPERATION_SET, 0);
    if (!maage_item_registry_is_finalized(items) || !maage_block_registry_is_finalized(blocks)) return maage_status_make(MAAGE_STATUS_INVALID_STATE, MAAGE_CONTEXT_GUI, MAAGE_OPERATION_SET, 0);
    for (i = 0; i < maage_item_registry_count(items); ++i) {
        item = maage_item_registry_at(items, i);
        variants = (size_t) item->max_metadata + 1;
        if (!variants || variants > (size_t) - 1 / sizeof(*creative) - count) return maage_status_make(MAAGE_STATUS_BUFFER_OVERFLOW, MAAGE_CONTEXT_GUI, MAAGE_OPERATION_SET, 0);
        count += variants;
    }
    creative = (maage_item_stack *) calloc(count, sizeof(*creative));
    if (!creative) return maage_status_make(MAAGE_STATUS_ALLOCATION_FAILED, MAAGE_CONTEXT_GUI, MAAGE_OPERATION_SET, 0);
    for (i = 0; i < maage_item_registry_count(items); ++i) {
        item = maage_item_registry_at(items, i);
        for (metadata = 0; metadata <= item->max_metadata; ++metadata) {
            creative[index].item_id = item->id;
            creative[index].count = 1;
            creative[index++].metadata = (unsigned int) metadata;
        }
    }
    free(ui->creative_items);
    ui->creative_items = creative;
    ui->creative_count = count;
    ui->creative_page = 0;
    ui->items = items;
    ui->blocks = blocks;
    memcpy(ui->item_uv, item_uv, sizeof(ui->item_uv));
    memcpy(ui->inventory_grass_color, inventory_grass_color, sizeof(ui->inventory_grass_color));
    return maage_status_make(MAAGE_STATUS_SUCCESS, 0, MAAGE_OPERATION_SET, 0);
}
maage_status maage_client_ui_set_snapshot(maage_client_ui *ui, const maage_session_snapshot *snapshot) {
    int respawned;
    if (!ui || !snapshot) return maage_status_make(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_CONTEXT_GUI, MAAGE_OPERATION_SNAPSHOT, 0);
    if (snapshot->protocol_version != MAAGE_SESSION_PROTOCOL_VERSION) return maage_status_make(MAAGE_STATUS_MALFORMED_INPUT, MAAGE_CONTEXT_GUI, MAAGE_OPERATION_SNAPSHOT, 0);
    respawned = ui->has_snapshot && ui->snapshot.player.health == 0 && snapshot->player.health != 0;
    if (!ui->has_snapshot || snapshot->session_epoch != ui->snapshot.session_epoch || snapshot->world_epoch != ui->snapshot.world_epoch || snapshot->player.motion.life != ui->snapshot.player.motion.life || snapshot->player.game_mode != ui->snapshot.player.game_mode || snapshot->tick < ui->snapshot.tick) {
        ui->health_before_damage = snapshot->player.health;
        ui->health_flash_tick = 0;
        ui->health_flashing = 0;
    } else if (snapshot->player.damage_sequence != ui->snapshot.player.damage_sequence) {
        /* One occurrence starts one flash, even when snapshots repeat. Keep
         * the pre-hit value so lost full/half hearts can flash behind live HP. */
        ui->health_before_damage = ui->snapshot.player.health;
        ui->health_flash_tick = snapshot->player.damage_tick;
        ui->health_flashing = snapshot->player.damage_sequence != 0;
    } else if (ui->health_flashing && snapshot->tick >= ui->health_flash_tick && snapshot->tick - ui->health_flash_tick > 10) ui->health_flashing = 0;
    {
        const maage_item_stack *selected = maage_inventory_selected(&snapshot->player.inventory);
        const maage_item_definition *definition = selected && ui->items ? maage_item_registry_get(ui->items, selected->item_id) : NULL;
        if (!selected || !selected->count) ui->highlight_until = 0;
        else if (!ui->has_snapshot || selected->item_id != ui->highlighted_item.item_id || ((!definition || !definition->durability) && selected->metadata != ui->highlighted_item.metadata)) ui->highlight_until = ui->notice_now + 2.0;
        if (selected) ui->highlighted_item = *selected;
    }
    ui->snapshot = *snapshot;
    ui->has_snapshot = 1;
    if (ui->container_command_pending && ui->container_command_sequence && snapshot->applied_command_sequence >= ui->container_command_sequence) {
        ui->container_command_pending = 0;
        ui->container_command_sequence = 0;
    }
    if (ui->container_close_requested && !ui->container_command_pending) {
        if (!snapshot->player.crafting.kind) ui->container_close_requested = 0;
        else {
            maage_session_command command;
            memset(&command, 0, sizeof(command));
            command.kind = MAAGE_CMD_CONTAINER_CLOSE;
            command.container_identity = snapshot->player.crafting.identity;
            ui->route_result = maage_client_ui_queue_command(ui, &command);
            if (maage_status_failed(ui->route_result)) return ui->route_result;
        }
    }
    if (snapshot->player.crafting.kind) {
        maage_client_ui_screen_id desired = snapshot->player.crafting.kind == MAAGE_CONTAINER_TABLE ? MAAGE_CLIENT_UI_CRAFTING_TABLE : MAAGE_CLIENT_UI_INVENTORY;
        if (maage_client_ui_current(ui) != desired) {
            maage_status result;
            /* The Server opens containers only from gameplay; a late snapshot
             * may arrive just after Escape opened Pause. */
            if (maage_client_ui_current(ui) == MAAGE_CLIENT_UI_PAUSE) maage_client_ui_back(ui);
            result = maage_client_ui_open(ui, desired);
            if (maage_status_failed(result)) return result;
        }
    } else if (!ui->container_command_pending && (maage_client_ui_current(ui) == MAAGE_CLIENT_UI_INVENTORY || maage_client_ui_current(ui) == MAAGE_CLIENT_UI_CRAFTING_TABLE)) {
        maage_client_ui_back(ui);
    }
    if (respawned && maage_client_ui_current(ui) == MAAGE_CLIENT_UI_PAUSE) maage_client_ui_back(ui);
    return maage_status_make(MAAGE_STATUS_SUCCESS, 0, MAAGE_OPERATION_SNAPSHOT, 0);
}
maage_status maage_client_ui_queue_command(maage_client_ui *ui, const maage_session_command *command) {
    maage_session_command next;
    if (!ui || !command) return maage_status_make(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_CONTEXT_GUI, MAAGE_OPERATION_SUBMIT, 0);
    if (ui->command_count == MAAGE_CLIENT_UI_COMMAND_CAPACITY) {
        ui->route_result = maage_status_make(MAAGE_STATUS_BUFFER_OVERFLOW, MAAGE_CONTEXT_GUI, MAAGE_OPERATION_SUBMIT, 0);
        maage_client_ui_notify(ui, ui->route_result, maage_time_monotonic_seconds());
        return ui->route_result;
    }
    if (command->kind == MAAGE_CMD_CONTAINER_CLOSE) ui->container_close_requested = 1;
    next = *command;
    next.protocol_version = MAAGE_SESSION_PROTOCOL_VERSION;
    /* The controller assigns one global sequence across UI and gameplay input. */
    next.sequence = 0;
    ui->commands[ui->command_write] = next;
    ui->command_write = (ui->command_write + 1U) % MAAGE_CLIENT_UI_COMMAND_CAPACITY;
    ++ui->command_count;
    if (command->kind == MAAGE_CMD_CONTAINER_OPEN || command->kind == MAAGE_CMD_CONTAINER_CLOSE ||
        command->kind == MAAGE_CMD_CONTAINER_SLOT || command->kind == MAAGE_CMD_CREATIVE_PICK ||
        (command->kind == MAAGE_CMD_DROP && command->inventory_slot == MAAGE_INVENTORY_SLOTS)) {
        ui->container_command_pending = 1;
        ui->container_command_sequence = 0;
    }
    return maage_status_make(MAAGE_STATUS_SUCCESS, 0, MAAGE_OPERATION_SUBMIT, 0);
}
void maage_client_ui_command_submitted(maage_client_ui *ui, const maage_session_command *command, unsigned long sequence) {
    if (!ui || !command || !ui->container_command_pending) return;
    if (command->kind == MAAGE_CMD_CONTAINER_OPEN || command->kind == MAAGE_CMD_CONTAINER_CLOSE ||
        command->kind == MAAGE_CMD_CONTAINER_SLOT || command->kind == MAAGE_CMD_CREATIVE_PICK ||
        (command->kind == MAAGE_CMD_DROP && command->inventory_slot == MAAGE_INVENTORY_SLOTS))
        ui->container_command_sequence = sequence;
}

maage_status maage_client_ui_take_command(maage_client_ui *ui, maage_session_command *command) {
    if (!ui || !command) return maage_status_make(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_CONTEXT_GUI, MAAGE_OPERATION_READ, 0);
    if (!ui->command_count) return maage_status_make(MAAGE_STATUS_NOOP, 0, MAAGE_OPERATION_READ, 0);
    *command = ui->commands[ui->command_read];
    ui->command_read = (ui->command_read + 1U) % MAAGE_CLIENT_UI_COMMAND_CAPACITY;
    --ui->command_count;
    return maage_status_make(MAAGE_STATUS_SUCCESS, 0, MAAGE_OPERATION_READ, 0);
}
void maage_client_ui_queue_game_mode(maage_client_ui *ui) {
    maage_session_command command;
    if (!ui || !ui->has_snapshot) return;
    memset(&command, 0, sizeof(command));
    command.kind = MAAGE_CMD_GAME_MODE;
    command.game_mode = ui->snapshot.player.game_mode == MAAGE_GAME_MODE_CREATIVE ? MAAGE_GAME_MODE_SURVIVAL : MAAGE_GAME_MODE_CREATIVE;
    ui->route_result = maage_client_ui_queue_command(ui, &command);
}
void maage_client_ui_queue_respawn(maage_client_ui *ui) {
    maage_session_command command;
    if (!ui) return;
    memset(&command, 0, sizeof(command));
    command.kind = MAAGE_CMD_RESPAWN;
    ui->route_result = maage_client_ui_queue_command(ui, &command);
}

maage_status maage_client_ui_retry_command(maage_client_ui *ui, const maage_session_command *command) {
    if (!ui || !command || command->protocol_version != MAAGE_SESSION_PROTOCOL_VERSION) return maage_status_make(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_CONTEXT_GUI, MAAGE_OPERATION_SUBMIT, 0);
    if (ui->command_count == MAAGE_CLIENT_UI_COMMAND_CAPACITY) {
        ui->route_result = maage_status_make(MAAGE_STATUS_BUFFER_OVERFLOW, MAAGE_CONTEXT_GUI, MAAGE_OPERATION_SUBMIT, 0);
        maage_client_ui_notify(ui, ui->route_result, maage_time_monotonic_seconds());
        return ui->route_result;
    }
    ui->command_read = (ui->command_read + MAAGE_CLIENT_UI_COMMAND_CAPACITY - 1U) % MAAGE_CLIENT_UI_COMMAND_CAPACITY;
    ui->commands[ui->command_read] = *command;
    ++ui->command_count;
    return maage_status_make(MAAGE_STATUS_SUCCESS, 0, MAAGE_OPERATION_SUBMIT, 0);
}
