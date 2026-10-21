#include "internal.h"
#include <math.h>
#include <stdio.h>

static void maage_client_ui_block_uv(const maage_client_ui *ui,
    const maage_block_face_definition *face, float uv[4])
{
    uv[0] = face->uv[0] * ui->resources.block_atlas_width / ui->resources.atlas_edge;
    uv[2] = face->uv[2] * ui->resources.block_atlas_width / ui->resources.atlas_edge;
    uv[1] = (MAAGE_CLIENT_UI_BLOCK_ATLAS_Y + face->uv[1] * ui->resources.block_atlas_height) /
        ui->resources.atlas_edge;
    uv[3] = (MAAGE_CLIENT_UI_BLOCK_ATLAS_Y + face->uv[3] * ui->resources.block_atlas_height) /
        ui->resources.atlas_edge;
}

static const maage_block_state_definition *maage_client_ui_item_state(
    const maage_client_ui *ui, const maage_item_stack *stack)
{
    const maage_item_definition *item;
    if (!stack || !stack->count || !ui->items || !ui->blocks) return NULL;
    item = maage_item_registry_get(ui->items, stack->item_id);
    return item ? maage_block_registry_state(ui->blocks, item->icon_state_id) : NULL;
}

static void maage_client_ui_stack(maage_client_ui *ui,
    const maage_item_stack *stack, float x, float y)
{
    const maage_block_state_definition *state;
    maage_client_ui_rect rectangle;
    float uv[4];
    char count[16];
    state = maage_client_ui_item_state(ui, stack);
    if (!state) return;
    maage_client_ui_block_uv(ui, &state->faces[MAAGE_FACE_POSITIVE_Z], uv);
    rectangle.x = x; rectangle.y = y; rectangle.width = rectangle.height = 16;
    maage_client_ui_rectangle(ui, rectangle, uv[0], uv[1], uv[2], uv[3], 1, 1, 1, 1);
    if (stack->count > 1) {
        sprintf(count, "%u", stack->count);
        maage_client_ui_text(ui, count, x + 17, y + 10, MAAGE_CLIENT_UI_RIGHT, 0.18f, 0.18f, 0.18f);
        maage_client_ui_text(ui, count, x + 16, y + 9, MAAGE_CLIENT_UI_RIGHT, 1, 1, 1);
    }
}

static void maage_client_ui_held(maage_client_ui *ui)
{
    static const int faces[3] = {MAAGE_FACE_POSITIVE_Y, MAAGE_FACE_NEGATIVE_X, MAAGE_FACE_POSITIVE_Z};
    static const float offsets[3][4][2] = {
        {{-44, -68}, {0, -91}, {44, -68}, {0, -45}},
        {{-44, -68}, {0, -45}, {0, 20}, {-44, -3}},
        {{0, -45}, {44, -68}, {44, -3}, {0, 20}}
    };
    static const float shades[3] = {1, 0.68f, 0.85f};
    const maage_block_state_definition *state;
    float uv[4], positions[4][2], x, y, swing;
    int face, corner;
    state = maage_client_ui_item_state(ui, maage_inventory_selected(&ui->game->inventory));
    if (!state) return;
    swing = (float)sin(ui->game->swing * 3.141592653589793);
    x = ui->width - 40 - swing * 24;
    y = ui->height + 10 - swing * 17;
    for (face = 0; face < 3; ++face) {
        maage_client_ui_block_uv(ui, &state->faces[faces[face]], uv);
        for (corner = 0; corner < 4; ++corner) {
            positions[corner][0] = x + offsets[face][corner][0];
            positions[corner][1] = y + offsets[face][corner][1];
        }
        maage_client_ui_quad(ui, positions, uv, shades[face]);
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
    if (!ui->game || !ui->items) return;
    if (!maage_client_ui_modal(ui)) maage_client_ui_held(ui);
    x = (float)(int)(ui->content.x + (ui->content.width - 182) * 0.5f);
    y = ui->content.y + ui->content.height - 22;
    rectangle.x = x; rectangle.y = y; rectangle.width = 182; rectangle.height = 22;
    maage_client_ui_rectangle(ui, rectangle, 256.0f / ui->resources.atlas_edge, 0,
        438.0f / ui->resources.atlas_edge, 22.0f / ui->resources.atlas_edge, 1, 1, 1, 1);
    rectangle.x = x - 1 + (float)ui->game->inventory.selected * 20;
    rectangle.y = y - 1; rectangle.width = rectangle.height = 24;
    maage_client_ui_rectangle(ui, rectangle, 256.0f / ui->resources.atlas_edge,
        22.0f / ui->resources.atlas_edge, 280.0f / ui->resources.atlas_edge,
        46.0f / ui->resources.atlas_edge, 1, 1, 1, 1);
    for (i = 0; i < MAAGE_HOTBAR_SLOTS; ++i)
        maage_client_ui_stack(ui, &ui->game->inventory.slots[i], x + 3 + (float)i * 20, y + 3);
    if (ui->game->mode == MAAGE_GAME_MODE_SURVIVAL) {
        maage_client_ui_status(ui, x, y - 13, ui->game->health, 0);
        maage_client_ui_status(ui, x + 173, y - 13, ui->game->hunger, 1);
    }
    selected = maage_inventory_selected(&ui->game->inventory);
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
    if (!ui->game || !ui->items || (!input->primary_pressed && !input->secondary_pressed)) return 0;
    frame = maage_client_ui_inventory_frame(ui);
    for (slot = 0; slot < MAAGE_INVENTORY_SLOTS; ++slot) {
        rectangle = maage_client_ui_inventory_slot(frame, slot);
        if (maage_client_ui_slot_contains(ui, rectangle)) {
            maage_inventory_click(&ui->game->inventory, ui->items, slot, input->secondary_pressed);
            return 0;
        }
    }
    if (ui->game->mode == MAAGE_GAME_MODE_CREATIVE) {
        count = maage_item_registry_count(ui->items);
        first = (size_t)ui->creative_page * 27;
        for (slot = 0; slot < 27 && first + slot < count; ++slot) {
            rectangle.x = frame.x + 8 + (float)(slot % 9) * 18;
            rectangle.y = frame.y + 18 + (float)(slot / 9) * 18;
            rectangle.width = rectangle.height = 16;
            if (maage_client_ui_slot_contains(ui, rectangle)) {
                item = maage_item_registry_at(ui->items, first + slot);
                if (item) maage_inventory_creative(&ui->game->inventory, ui->items, item->id);
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
    if (!ui->game || !ui->items) return;
    frame = maage_client_ui_inventory_frame(ui);
    maage_client_ui_rectangle(ui, frame, 256.0f / ui->resources.atlas_edge,
        256.0f / ui->resources.atlas_edge, 432.0f / ui->resources.atlas_edge,
        422.0f / ui->resources.atlas_edge, 1, 1, 1, 1);
    rectangle = frame; rectangle.x += 5; rectangle.y += 5;
    rectangle.width = 166; rectangle.height = 75;
    maage_client_ui_panel(ui, rectangle, 0.78f, 0.78f, 0.78f, 1);
    maage_client_ui_text(ui, ui->game->mode == MAAGE_GAME_MODE_CREATIVE ? "Creative" : "Inventory",
        frame.x + 8, frame.y + 6, MAAGE_CLIENT_UI_LEFT, 0.25f, 0.25f, 0.25f);
    if (ui->game->mode == MAAGE_GAME_MODE_CREATIVE) {
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
        sprintf(label, "Health: %u  Hunger: %u", ui->game->health, ui->game->hunger);
        maage_client_ui_text(ui, label, frame.x + 8, frame.y + 56,
            MAAGE_CLIENT_UI_LEFT, 0.25f, 0.25f, 0.25f);
    }
    for (slot = 0; slot < MAAGE_INVENTORY_SLOTS; ++slot) {
        rectangle = maage_client_ui_inventory_slot(frame, slot);
        maage_client_ui_stack(ui, &ui->game->inventory.slots[slot], rectangle.x, rectangle.y);
        if (maage_client_ui_slot_contains(ui, rectangle)) {
            maage_client_ui_panel(ui, rectangle, 1, 1, 1, 0.28f);
            if (ui->game->inventory.slots[slot].count)
                hover = maage_item_registry_get(ui->items, ui->game->inventory.slots[slot].item_id);
        }
    }
    if (!ui->game->inventory.cursor.count && hover) {
        rectangle.x = ui->mouse_x + 10; rectangle.y = ui->mouse_y - 12;
        rectangle.width = maage_client_ui_text_width(ui, hover->display_name) + 8;
        rectangle.height = 16;
        if (rectangle.x + rectangle.width > ui->width) rectangle.x = ui->width - rectangle.width;
        maage_client_ui_panel(ui, rectangle, 0.08f, 0.02f, 0.12f, 0.96f);
        maage_client_ui_text(ui, hover->display_name, rectangle.x + 4, rectangle.y + 4,
            MAAGE_CLIENT_UI_LEFT, 1, 1, 1);
    }
    maage_client_ui_stack(ui, &ui->game->inventory.cursor, ui->mouse_x - 8, ui->mouse_y - 8);
}

int maage_client_ui_inventory_register(maage_client_ui *ui)
{
    return maage_client_ui_register_screen(ui, MAAGE_CLIENT_UI_INVENTORY,
        maage_client_ui_inventory_draw, maage_client_ui_inventory_input, NULL);
}

void maage_client_ui_set_gameplay(maage_client_ui *ui, const maage_item_registry *items,
    maage_gameplay *game, const maage_block_registry *blocks)
{
    if (!ui) return;
    ui->items = items; ui->game = game; ui->blocks = blocks;
}
