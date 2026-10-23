#include "internal.h"
#include "../resources.h"
#include "maage/time.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

static void maage_client_ui_block_uv(const maage_client_ui *ui,
    const float source[4], float uv[4])
{
    uv[0] = source[0] * ui->resources.block_atlas_width / ui->resources.atlas_edge;
    uv[2] = source[2] * ui->resources.block_atlas_width / ui->resources.atlas_edge;
    uv[1] = (MAAGE_CLIENT_UI_BLOCK_ATLAS_Y + source[1] * ui->resources.block_atlas_height) /
        ui->resources.atlas_edge;
    uv[3] = (MAAGE_CLIENT_UI_BLOCK_ATLAS_Y + source[3] * ui->resources.block_atlas_height) /
        ui->resources.atlas_edge;
}

static const maage_block_state_definition *maage_client_ui_item_state(
    const maage_client_ui *ui, const maage_item_stack *stack)
{
    const maage_item_definition *item;
    if (!stack || !stack->count || !ui->items || !ui->blocks) return NULL;
    item = maage_item_registry_get(ui->items, stack->item_id);
    return item ? maage_client_item_icon_state(ui->blocks, item, stack->metadata) : NULL;
}

static void maage_client_ui_stack(maage_client_ui *ui,
    const maage_item_stack *stack, float x, float y)
{
    const maage_block_state_definition *state;
    const maage_block_face_definition *face;
    maage_client_ui_rect rectangle;
    float uv[4], color[4];
    char count[16];
    state = maage_client_ui_item_state(ui, stack);
    if (!state) return;
    face = &state->faces[MAAGE_FACE_POSITIVE_Z];
    maage_client_ui_block_uv(ui, face->uv, uv);
    maage_client_item_face_tint(ui->inventory_grass_color, face, face->tint_mode, color);
    rectangle.x = x; rectangle.y = y; rectangle.width = rectangle.height = 16;
    maage_client_ui_rectangle(ui, rectangle, uv[0], uv[1], uv[2], uv[3],
        color[0], color[1], color[2], face->tint[3]);
    if (face->overlay_uv[2] > face->overlay_uv[0]) {
        maage_client_ui_block_uv(ui, face->overlay_uv, uv);
        maage_client_item_face_tint(ui->inventory_grass_color, face, face->overlay_tint_mode, color);
        maage_client_ui_rectangle(ui, rectangle, uv[0], uv[1], uv[2], uv[3],
            color[0], color[1], color[2], 1);
    }
    if (stack->count > 1) {
        sprintf(count, "%u", stack->count);
        maage_client_ui_text(ui, count, x + 17, y + 10, MAAGE_CLIENT_UI_RIGHT, 0.18f, 0.18f, 0.18f);
        maage_client_ui_text(ui, count, x + 16, y + 9, MAAGE_CLIENT_UI_RIGHT, 1, 1, 1);
    }
}

static void maage_client_ui_status(maage_client_ui *ui, float x, float y,
    unsigned int value, int hunger)
{
    maage_client_ui_rect rectangle;
    unsigned int i, filled;
    float v, u;
    if (value > 20) value = 20;
    rectangle.width = rectangle.height = 9;
    v = (float)(hunger ? 27 : 0) / ui->resources.atlas_edge;
    for (i = 0; i < 10; ++i) {
        rectangle.x = hunger ? x - (float)i * 8 : x + (float)i * 8;
        rectangle.y = y;
        maage_client_ui_rectangle(ui, rectangle, 16.0f / ui->resources.atlas_edge, v,
            25.0f / ui->resources.atlas_edge, v + 9.0f / ui->resources.atlas_edge, 1, 1, 1, 1);
        filled = value > i * 2 ? value - i * 2 : 0;
        if (!filled) continue;
        u = (float)(filled >= 2 ? 52 : 61) / ui->resources.atlas_edge;
        maage_client_ui_rectangle(ui, rectangle, u, v, u + 9.0f / ui->resources.atlas_edge,
            v + 9.0f / ui->resources.atlas_edge, 1, 1, 1, 1);
    }
}

void maage_client_ui_gameplay_draw(maage_client_ui *ui)
{
    maage_client_ui_rect rectangle;
    const maage_item_stack *selected;
    const maage_item_definition *item;
    float x, y;
    unsigned int i;
    if (!ui->has_snapshot || !ui->items) return;
    x = (float)(int)(ui->content.x + (ui->content.width - 182) * 0.5f);
    y = ui->content.y + ui->content.height - 22;
    rectangle.x = x; rectangle.y = y; rectangle.width = 182; rectangle.height = 22;
    maage_client_ui_rectangle(ui, rectangle, 256.0f / ui->resources.atlas_edge, 0,
        438.0f / ui->resources.atlas_edge, 22.0f / ui->resources.atlas_edge, 1, 1, 1, 1);
    rectangle.x = x - 1 + (float)ui->snapshot.player.inventory.selected * 20;
    rectangle.y = y - 1; rectangle.width = rectangle.height = 24;
    maage_client_ui_rectangle(ui, rectangle, 256.0f / ui->resources.atlas_edge,
        22.0f / ui->resources.atlas_edge, 280.0f / ui->resources.atlas_edge,
        46.0f / ui->resources.atlas_edge, 1, 1, 1, 1);
    for (i = 0; i < MAAGE_HOTBAR_SLOTS; ++i)
        maage_client_ui_stack(ui, &ui->snapshot.player.inventory.slots[i], x + 3 + (float)i * 20, y + 3);
    if (ui->snapshot.player.game_mode == MAAGE_GAME_MODE_SURVIVAL) {
        maage_client_ui_status(ui, x, y - 13, ui->snapshot.player.health, 0);
        maage_client_ui_status(ui, x + 173, y - 13, ui->snapshot.player.hunger, 1);
    }
    selected = maage_inventory_selected(&ui->snapshot.player.inventory);
    item = selected && selected->count ? maage_item_registry_get(ui->items, selected->item_id) : NULL;
    if (item && !maage_client_ui_modal(ui))
        maage_client_ui_text(ui, item->display_name, x + 91, y - 26,
            MAAGE_CLIENT_UI_CENTER, 1, 1, 1);
}

static maage_client_ui_rect maage_client_ui_inventory_frame(const maage_client_ui *ui)
{
    maage_client_ui_rect rectangle;
    rectangle.width = 176; rectangle.height = 166;
    rectangle.x = (float)(int)(ui->content.x + (ui->content.width - 176) * 0.5f);
    rectangle.y = (float)(int)(ui->content.y + (ui->content.height - 166) * 0.5f);
    return rectangle;
}

static maage_client_ui_rect maage_client_ui_inventory_slot(maage_client_ui_rect frame,
    unsigned int slot)
{
    maage_client_ui_rect rectangle;
    rectangle.width = rectangle.height = 16;
    rectangle.x = frame.x + 8 + (float)(slot % 9) * 18;
    rectangle.y = frame.y + (slot < 9 ? 142 : 84 + (float)((slot - 9) / 9) * 18);
    return rectangle;
}

static int maage_client_ui_slot_contains(const maage_client_ui *ui, maage_client_ui_rect rectangle)
{
    return ui->mouse_x >= rectangle.x && ui->mouse_y >= rectangle.y &&
        ui->mouse_x < rectangle.x + rectangle.width && ui->mouse_y < rectangle.y + rectangle.height;
}

static unsigned int maage_client_ui_inventory_input(maage_client_ui *ui,
    const maage_client_ui_input *input, void *context)
{
    maage_client_ui_rect frame, rectangle;
    const maage_item_definition *item;
    unsigned int slot;
    size_t count, first;
    (void)context;
    if (!ui->has_snapshot || !ui->items || (!input->primary_pressed && !input->secondary_pressed)) return 0;
    frame = maage_client_ui_inventory_frame(ui);
    for (slot = 0; slot < MAAGE_INVENTORY_SLOTS; ++slot) {
        rectangle = maage_client_ui_inventory_slot(frame, slot);
        if (maage_client_ui_slot_contains(ui, rectangle)) {
            maage_session_command command;
            memset(&command, 0, sizeof(command));
            command.kind = MAAGE_CMD_INVENTORY;
            command.inventory_slot = slot;
            command.secondary_click = input->secondary_pressed != 0;
            ui->route_result = maage_client_ui_queue_command(ui, &command);
            return 0;
        }
    }
    if (ui->snapshot.player.game_mode == MAAGE_GAME_MODE_CREATIVE) {
        count = maage_item_registry_count(ui->items);
        first = (size_t)ui->creative_page * 27;
        for (slot = 0; slot < 27 && first + slot < count; ++slot) {
            rectangle.x = frame.x + 8 + (float)(slot % 9) * 18;
            rectangle.y = frame.y + 18 + (float)(slot / 9) * 18;
            rectangle.width = rectangle.height = 16;
            if (maage_client_ui_slot_contains(ui, rectangle)) {
                item = maage_item_registry_at(ui->items, first + slot);
                if (item) {
                    maage_session_command command;
                    memset(&command, 0, sizeof(command));
                    command.kind = MAAGE_CMD_CREATIVE_PICK;
                    command.item_id = item->id;
                    ui->route_result = maage_client_ui_queue_command(ui, &command);
                }
                return 0;
            }
        }
        rectangle.x = frame.x + 116; rectangle.y = frame.y + 5;
        rectangle.width = 22; rectangle.height = 10;
        if (maage_client_ui_slot_contains(ui, rectangle) && ui->creative_page) --ui->creative_page;
        rectangle.x += 26;
        if (maage_client_ui_slot_contains(ui, rectangle) && first + 27 < count) ++ui->creative_page;
    }
    return 0;
}

static void maage_client_ui_inventory_draw(maage_client_ui *ui, void *context)
{
    maage_client_ui_rect frame, rectangle;
    maage_item_stack stack;
    const maage_item_definition *item, *hover = NULL;
    size_t count, first;
    unsigned int slot;
    char label[48];
    (void)context;
    if (!ui->has_snapshot || !ui->items) return;
    frame = maage_client_ui_inventory_frame(ui);
    maage_client_ui_rectangle(ui, frame, 256.0f / ui->resources.atlas_edge,
        256.0f / ui->resources.atlas_edge, 432.0f / ui->resources.atlas_edge,
        422.0f / ui->resources.atlas_edge, 1, 1, 1, 1);
    rectangle = frame; rectangle.x += 5; rectangle.y += 5;
    rectangle.width = 166; rectangle.height = 75;
    maage_client_ui_panel(ui, rectangle, 0.78f, 0.78f, 0.78f, 1);
    maage_client_ui_text(ui, ui->snapshot.player.game_mode == MAAGE_GAME_MODE_CREATIVE ? "Creative" : "Inventory",
        frame.x + 8, frame.y + 6, MAAGE_CLIENT_UI_LEFT, 0.25f, 0.25f, 0.25f);
    if (ui->snapshot.player.game_mode == MAAGE_GAME_MODE_CREATIVE) {
        count = maage_item_registry_count(ui->items);
        first = (size_t)ui->creative_page * 27;
        for (slot = 0; slot < 27 && first + slot < count; ++slot) {
            item = maage_item_registry_at(ui->items, first + slot);
            rectangle.x = frame.x + 7 + (float)(slot % 9) * 18;
            rectangle.y = frame.y + 17 + (float)(slot / 9) * 18;
            rectangle.width = rectangle.height = 18;
            maage_client_ui_rectangle(ui, rectangle, 263.0f / ui->resources.atlas_edge,
                339.0f / ui->resources.atlas_edge, 281.0f / ui->resources.atlas_edge,
                357.0f / ui->resources.atlas_edge, 1, 1, 1, 1);
            if (!item) continue;
            stack.item_id = item->id; stack.count = 1; stack.metadata = stack.damage = 0;
            maage_client_ui_stack(ui, &stack, rectangle.x + 1, rectangle.y + 1);
            if (maage_client_ui_slot_contains(ui, rectangle)) hover = item;
        }
        if (count > 27) {
            sprintf(label, "<   %u   >", ui->creative_page + 1);
            maage_client_ui_text(ui, label, frame.x + 145, frame.y + 6,
                MAAGE_CLIENT_UI_CENTER, 0.25f, 0.25f, 0.25f);
        }
    } else {
        maage_client_ui_text(ui, "Left click: move / merge", frame.x + 8, frame.y + 25,
            MAAGE_CLIENT_UI_LEFT, 0.25f, 0.25f, 0.25f);
        maage_client_ui_text(ui, "Right click: split / place one", frame.x + 8, frame.y + 38,
            MAAGE_CLIENT_UI_LEFT, 0.25f, 0.25f, 0.25f);
        sprintf(label, "Health: %u  Hunger: %u", ui->snapshot.player.health, ui->snapshot.player.hunger);
        maage_client_ui_text(ui, label, frame.x + 8, frame.y + 56,
            MAAGE_CLIENT_UI_LEFT, 0.25f, 0.25f, 0.25f);
    }
    for (slot = 0; slot < MAAGE_INVENTORY_SLOTS; ++slot) {
        rectangle = maage_client_ui_inventory_slot(frame, slot);
        maage_client_ui_stack(ui, &ui->snapshot.player.inventory.slots[slot], rectangle.x, rectangle.y);
        if (maage_client_ui_slot_contains(ui, rectangle)) {
            maage_client_ui_panel(ui, rectangle, 1, 1, 1, 0.28f);
            if (ui->snapshot.player.inventory.slots[slot].count)
                hover = maage_item_registry_get(ui->items, ui->snapshot.player.inventory.slots[slot].item_id);
        }
    }
    if (!ui->snapshot.player.inventory.cursor.count && hover) {
        rectangle.x = ui->mouse_x + 10; rectangle.y = ui->mouse_y - 12;
        rectangle.width = maage_client_ui_text_width(ui, hover->display_name) + 8;
        rectangle.height = 16;
        if (rectangle.x + rectangle.width > ui->width) rectangle.x = ui->width - rectangle.width;
        maage_client_ui_panel(ui, rectangle, 0.08f, 0.02f, 0.12f, 0.96f);
        maage_client_ui_text(ui, hover->display_name, rectangle.x + 4, rectangle.y + 4,
            MAAGE_CLIENT_UI_LEFT, 1, 1, 1);
    }
    maage_client_ui_stack(ui, &ui->snapshot.player.inventory.cursor, ui->mouse_x - 8, ui->mouse_y - 8);
}

maage_status maage_client_ui_inventory_register(maage_client_ui *ui)
{
    return maage_client_ui_register_screen(ui, MAAGE_CLIENT_UI_INVENTORY,
        maage_client_ui_inventory_draw, maage_client_ui_inventory_input, NULL);
}

maage_status maage_client_ui_set_content(maage_client_ui *ui,
    const maage_item_registry *items, const maage_block_registry *blocks,
    const float inventory_grass_color[3])
{
    if (!ui || !items || !blocks || !inventory_grass_color) return maage_status_make(MAAGE_STATUS_INVALID_ARGUMENT,
        MAAGE_CONTEXT_GUI, MAAGE_OPERATION_SET, 0);
    if (!maage_item_registry_is_finalized(items) || !maage_block_registry_is_finalized(blocks))
        return maage_status_make(MAAGE_STATUS_INVALID_STATE, MAAGE_CONTEXT_GUI, MAAGE_OPERATION_SET, 0);
    ui->items = items; ui->blocks = blocks;
    memcpy(ui->inventory_grass_color, inventory_grass_color, sizeof(ui->inventory_grass_color));
    return maage_status_make(MAAGE_STATUS_SUCCESS, 0, MAAGE_OPERATION_SET, 0);
}
maage_status maage_client_ui_set_snapshot(maage_client_ui *ui,
    const maage_session_snapshot *snapshot)
{
    int respawned;
    if (!ui || !snapshot) return maage_status_make(MAAGE_STATUS_INVALID_ARGUMENT,
        MAAGE_CONTEXT_GUI, MAAGE_OPERATION_SNAPSHOT, 0);
    if (snapshot->protocol_version != MAAGE_SESSION_PROTOCOL_VERSION)
        return maage_status_make(MAAGE_STATUS_MALFORMED_INPUT,
            MAAGE_CONTEXT_GUI, MAAGE_OPERATION_SNAPSHOT, 0);
    respawned = ui->has_snapshot && ui->snapshot.player.health == 0 &&
        snapshot->player.health != 0;
    ui->snapshot = *snapshot;
    ui->has_snapshot = 1;
    if (respawned && maage_client_ui_current(ui) == MAAGE_CLIENT_UI_PAUSE)
        maage_client_ui_back(ui);
    return maage_status_make(MAAGE_STATUS_SUCCESS, 0, MAAGE_OPERATION_SNAPSHOT, 0);
}
maage_status maage_client_ui_queue_command(maage_client_ui *ui,
    const maage_session_command *command)
{
    maage_session_command next;
    if (!ui || !command) return maage_status_make(MAAGE_STATUS_INVALID_ARGUMENT,
        MAAGE_CONTEXT_GUI, MAAGE_OPERATION_SUBMIT, 0);
    if (ui->command_count == MAAGE_CLIENT_UI_COMMAND_CAPACITY) {
        ui->route_result = maage_status_make(MAAGE_STATUS_BUFFER_OVERFLOW,
            MAAGE_CONTEXT_GUI, MAAGE_OPERATION_SUBMIT, 0);
        maage_client_ui_notify(ui, ui->route_result, maage_time_monotonic_seconds());
        return ui->route_result;
    }
    next = *command;
    next.protocol_version = MAAGE_SESSION_PROTOCOL_VERSION;
    /* Agent 11 assigns one global sequence across UI and gameplay input. */
    next.sequence = 0;
    ui->commands[ui->command_write] = next;
    ui->command_write = (ui->command_write + 1U) % MAAGE_CLIENT_UI_COMMAND_CAPACITY;
    ++ui->command_count;
    return maage_status_make(MAAGE_STATUS_SUCCESS, 0, MAAGE_OPERATION_SUBMIT, 0);
}
maage_status maage_client_ui_take_command(maage_client_ui *ui, maage_session_command *command)
{
    if (!ui || !command) return maage_status_make(MAAGE_STATUS_INVALID_ARGUMENT,
        MAAGE_CONTEXT_GUI, MAAGE_OPERATION_READ, 0);
    if (!ui->command_count) return maage_status_make(MAAGE_STATUS_NOOP, 0, MAAGE_OPERATION_READ, 0);
    *command = ui->commands[ui->command_read];
    ui->command_read = (ui->command_read + 1U) % MAAGE_CLIENT_UI_COMMAND_CAPACITY;
    --ui->command_count;
    return maage_status_make(MAAGE_STATUS_SUCCESS, 0, MAAGE_OPERATION_READ, 0);
}
void maage_client_ui_queue_game_mode(maage_client_ui *ui)
{
    maage_session_command command;
    if (!ui || !ui->has_snapshot) return;
    memset(&command, 0, sizeof(command));
    command.kind = MAAGE_CMD_GAME_MODE;
    command.game_mode = ui->snapshot.player.game_mode == MAAGE_GAME_MODE_CREATIVE ?
        MAAGE_GAME_MODE_SURVIVAL : MAAGE_GAME_MODE_CREATIVE;
    ui->route_result = maage_client_ui_queue_command(ui, &command);
}
void maage_client_ui_queue_respawn(maage_client_ui *ui)
{
    maage_session_command command;
    if (!ui) return;
    memset(&command, 0, sizeof(command));
    command.kind = MAAGE_CMD_RESPAWN;
    ui->route_result = maage_client_ui_queue_command(ui, &command);
}

maage_status maage_client_ui_retry_command(maage_client_ui *ui,
    const maage_session_command *command)
{
    if (!ui || !command || command->protocol_version != MAAGE_SESSION_PROTOCOL_VERSION)
        return maage_status_make(MAAGE_STATUS_INVALID_ARGUMENT,
            MAAGE_CONTEXT_GUI, MAAGE_OPERATION_SUBMIT, 0);
    if (ui->command_count == MAAGE_CLIENT_UI_COMMAND_CAPACITY) {
        ui->route_result = maage_status_make(MAAGE_STATUS_BUFFER_OVERFLOW,
            MAAGE_CONTEXT_GUI, MAAGE_OPERATION_SUBMIT, 0);
        maage_client_ui_notify(ui, ui->route_result, maage_time_monotonic_seconds());
        return ui->route_result;
    }
    ui->command_read = (ui->command_read + MAAGE_CLIENT_UI_COMMAND_CAPACITY - 1U) %
        MAAGE_CLIENT_UI_COMMAND_CAPACITY;
    ui->commands[ui->command_read] = *command;
    ++ui->command_count;
    return maage_status_make(MAAGE_STATUS_SUCCESS, 0, MAAGE_OPERATION_SUBMIT, 0);
}
