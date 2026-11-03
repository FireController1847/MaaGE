#include "internal.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const maage_container_definition *inventory_definition(const maage_client_ui *ui) {
    return ui->blocks ? maage_container_get(maage_block_registry_containers(ui->blocks), ui->snapshot.player.crafting.kind) : NULL;
}
static int inventory_creative(const maage_client_ui *ui) {
    const maage_container_definition *definition = inventory_definition(ui);
    return ui->has_snapshot && ui->snapshot.player.game_mode == MAAGE_GAME_MODE_CREATIVE && definition && definition->creative_catalog && maage_creative_tab_count(ui->creative);
}
static const maage_creative_tab_definition *inventory_tab(const maage_client_ui *ui) {
    return maage_creative_tab(ui->creative, ui->creative_tab);
}
int maage_client_ui_search_focused(const maage_client_ui *ui) {
    const maage_creative_tab_definition *tab = ui ? inventory_tab(ui) : NULL;
    return ui && maage_client_ui_current(ui) == MAAGE_CLIENT_UI_INVENTORY && inventory_creative(ui) && tab && tab->kind == MAAGE_CREATIVE_SEARCH;
}
void maage_client_ui_inventory_reset(maage_client_ui *ui) {
    ui->inventory_drag_active = 0;
    memset(&ui->inventory_drag, 0, sizeof(ui->inventory_drag));
    ui->creative_dragging = 0;
    ui->creative_pressed_tab = 0;
    ui->inventory_last_click = -1;
    ui->inventory_last_shift = 0;
    memset(&ui->inventory_last_stack, 0, sizeof(ui->inventory_last_stack));
}
static int inventory_contains(const maage_client_ui *ui, maage_client_ui_rect rectangle) {
    return ui->mouse_x >= rectangle.x && ui->mouse_y >= rectangle.y && ui->mouse_x < rectangle.x + rectangle.width && ui->mouse_y < rectangle.y + rectangle.height;
}
static int inventory_slot_contains(const maage_client_ui *ui, maage_client_ui_rect rectangle) {
    rectangle.x -= 1;
    rectangle.y -= 1;
    rectangle.width += 2;
    rectangle.height += 2;
    return inventory_contains(ui, rectangle);
}
static maage_client_ui_rect inventory_frame(const maage_client_ui *ui) {
    maage_client_ui_rect rectangle;
    const maage_container_definition *definition = inventory_definition(ui);
    int creative = inventory_creative(ui);
    rectangle.width = definition ? (float) (creative ? definition->presentation.creative_width : definition->presentation.width) : 176;
    rectangle.height = definition ? (float) (creative ? definition->presentation.creative_height : definition->presentation.height) : 166;
    rectangle.x = (float) (int) (ui->content.x + (ui->content.width - rectangle.width) * 0.5f);
    rectangle.y = (float) (int) (ui->content.y + (ui->content.height - rectangle.height) * 0.5f);
    return rectangle;
}
static maage_client_ui_rect inventory_slot(const maage_client_ui *ui, maage_client_ui_rect frame, unsigned int slot) {
    const maage_container_presentation *presentation = &inventory_definition(ui)->presentation;
    const maage_creative_tab_definition *tab = inventory_tab(ui);
    int creative = inventory_creative(ui), full = creative && tab && tab->kind == MAAGE_CREATIVE_INVENTORY;
    maage_client_ui_rect rectangle;
    rectangle.width = rectangle.height = 16;
    rectangle.x = frame.x + (creative ? 9 : presentation->inventory_slot_x) + (float) (slot % 9) * 18;
    rectangle.y = frame.y + (creative ? (slot < 9 || !full ? 112 : 54 + (float) ((slot - 9) / 9) * 18) : (slot < 9 ? presentation->hotbar_y : presentation->inventory_slot_y + (float) ((slot - 9) / 9) * 18));
    return rectangle;
}
static unsigned int inventory_slot_count(const maage_client_ui *ui) {
    const maage_creative_tab_definition *tab = inventory_tab(ui);
    if (inventory_creative(ui)) return tab && tab->kind == MAAGE_CREATIVE_INVENTORY ? MAAGE_INVENTORY_SLOTS : MAAGE_HOTBAR_SLOTS;
    return inventory_definition(ui)->presentation.show_inventory ? MAAGE_INVENTORY_SLOTS : 0;
}
static maage_client_ui_rect inventory_catalog_cell(maage_client_ui_rect frame, unsigned int slot) {
    maage_client_ui_rect rectangle;
    rectangle.x = frame.x + 9 + (float) (slot % 9) * 18;
    rectangle.y = frame.y + 18 + (float) (slot / 9) * 18;
    rectangle.width = rectangle.height = 16;
    return rectangle;
}
static maage_client_ui_rect inventory_destroy_slot(maage_client_ui_rect frame) {
    maage_client_ui_rect rectangle;
    rectangle.x = frame.x + 173;
    rectangle.y = frame.y + 112;
    rectangle.width = rectangle.height = 16;
    return rectangle;
}
static maage_client_ui_rect inventory_registered_slot(maage_client_ui_rect frame, const maage_container_slot_definition *slot) {
    maage_client_ui_rect rectangle;
    rectangle.x = frame.x + slot->x;
    rectangle.y = frame.y + slot->y;
    rectangle.width = rectangle.height = 16;
    return rectangle;
}
static void maage_client_ui_registered_texture(maage_client_ui *ui, maage_client_ui_rect rectangle, maage_texture_id texture, const float crop[4]) {
    float uv[4], source[4];
    if (texture >= ui->item_texture_count) {
        ui->draw_result = maage_status_make(MAAGE_STATUS_INVARIANT_VIOLATION, MAAGE_CONTEXT_GUI, MAAGE_OPERATION_ATLAS, 0);
        return;
    }
    memcpy(source, ui->item_uv[texture], sizeof(source));
    uv[0] = source[0] + (source[2] - source[0]) * crop[0];
    uv[1] = source[1] + (source[3] - source[1]) * crop[1];
    uv[2] = source[0] + (source[2] - source[0]) * crop[2];
    uv[3] = source[1] + (source[3] - source[1]) * crop[3];
    uv[0] *= (float) ui->resources.block_atlas_width / ui->resources.atlas_edge;
    uv[2] *= (float) ui->resources.block_atlas_width / ui->resources.atlas_edge;
    uv[1] = (MAAGE_CLIENT_UI_BLOCK_ATLAS_Y + uv[1] * ui->resources.block_atlas_height) / ui->resources.atlas_edge;
    uv[3] = (MAAGE_CLIENT_UI_BLOCK_ATLAS_Y + uv[3] * ui->resources.block_atlas_height) / ui->resources.atlas_edge;
    maage_client_ui_rectangle(ui, rectangle, uv[0], uv[1], uv[2], uv[3], 1, 1, 1, 1);
}
static void maage_client_ui_registered_progress(maage_client_ui *ui, maage_client_ui_rect frame, const maage_container_progress_definition *progress, const maage_container_progress_value *sample) {
    maage_client_ui_rect rectangle;
    unsigned long value, duration, quotient, remainder;
    unsigned int fill, lower, upper, middle;
    float uv[4], fraction;
    if (progress->require_active && !sample->active) return;
    value = sample->value;
    duration = sample->total ? sample->total : 1;
    if (value > duration) value = duration;
    /* Search the monotonic pixel thresholds in logarithmic work, preserving
     * quotient/remainder stepping across full-width tick counts. */
    quotient = duration / progress->steps;
    remainder = duration % progress->steps;
    lower = 0;
    upper = progress->steps;
    while (lower < upper) {
        middle = lower + (upper - lower) / 2U + (upper - lower) % 2U;
        if ((double) value >= (double) quotient * middle + ceil((double) remainder * middle / progress->steps)) lower = middle;
        else upper = middle - 1U;
    }
    fill = lower;
    if (!fill && !progress->extra_pixel) return;
    fraction = (float) (fill + progress->extra_pixel) / (progress->steps + progress->extra_pixel);
    if (progress->vertical && progress->extra_pixel) fraction = (float) (fill + progress->extra_pixel) / progress->steps;
    if (fraction > 1) fraction = 1;
    rectangle.x = frame.x + progress->x;
    rectangle.y = frame.y + progress->y;
    rectangle.width = (float) progress->width;
    rectangle.height = (float) progress->height;
    memcpy(uv, progress->uv, sizeof(uv));
    if (progress->vertical) {
        rectangle.y += (float) (int) (rectangle.height * (1 - fraction) + 0.5f);
        rectangle.height = (float) progress->height - (rectangle.y - frame.y - progress->y);
        uv[1] = uv[3] - (uv[3] - uv[1]) * rectangle.height / progress->height;
    } else {
        rectangle.width = (float) (int) (rectangle.width * fraction + 0.5f);
        uv[2] = uv[0] + (uv[2] - uv[0]) * rectangle.width / progress->width;
    }
    maage_client_ui_registered_texture(ui, rectangle, progress->texture, uv);
}
static void creative_tabs_page(maage_client_ui *ui) {
    const maage_creative_tab_definition *tab;
    size_t i, count = 0, index, first;
    memset(ui->creative_tabs, 0, sizeof(ui->creative_tabs));
    for (i = 0; i < maage_creative_tab_count(ui->creative); ++i) {
        tab = maage_creative_tab_at(ui->creative, i);
        if (tab->kind == MAAGE_CREATIVE_CATALOG) ++count;
        else ui->creative_tabs[tab->kind == MAAGE_CREATIVE_SEARCH ? 5 : 11] = tab->id;
    }
    ui->creative_page_count = count ? (count - 1) / 10 + 1 : 1;
    if (ui->creative_tab_page >= ui->creative_page_count) ui->creative_tab_page = ui->creative_page_count - 1;
    first = ui->creative_tab_page * 10;
    count = 0;
    for (i = 0; i < maage_creative_tab_count(ui->creative); ++i) {
        tab = maage_creative_tab_at(ui->creative, i);
        if (tab->kind != MAAGE_CREATIVE_CATALOG) continue;
        index = count++;
        if (index < first || index - first >= 10) continue;
        index -= first;
        ui->creative_tabs[index < 5 ? index : index + 1] = tab->id;
    }
}
static int creative_contains_text(const char *haystack, const char *needle) {
    const char *a, *b;
    unsigned char left, right;
    if (!*needle) return 1;
    if (!haystack) return 0;
    while (*haystack) {
        a = haystack++;
        b = needle;
        while (*a && *b) {
            left = (unsigned char) *a++;
            right = (unsigned char) *b++;
            if (left >= 'A' && left <= 'Z') left += 'a' - 'A';
            if (right >= 'A' && right <= 'Z') right += 'a' - 'A';
            if (left != right) break;
            if (!*b) return 1;
        }
    }
    return 0;
}
static const char *inventory_item_tooltip(const maage_client_ui *ui, const maage_item_stack *stack) {
    const maage_item_definition *item = stack && stack->count ? maage_item_registry_get(ui->items, stack->item_id) : NULL;
    return item && item->tooltip ? item->tooltip(item, stack->metadata) : NULL;
}
static void creative_filter(maage_client_ui *ui) {
    const maage_creative_tab_definition *tab = inventory_tab(ui);
    const maage_creative_entry_id *view;
    const maage_creative_entry *entry;
    size_t i, count;
    ui->creative_view = maage_creative_view(ui->creative, ui->creative_tab, &ui->creative_count);
    if (tab && tab->kind == MAAGE_CREATIVE_SEARCH) {
        view = ui->creative_view;
        count = ui->creative_count;
        ui->creative_count = 0;
        for (i = 0; i < count; ++i) {
            entry = maage_creative_get(ui->creative, view[i]);
            if (creative_contains_text(maage_client_item_name(ui->items, &entry->stack), ui->creative_query) || creative_contains_text(entry->tooltip, ui->creative_query) || creative_contains_text(inventory_item_tooltip(ui, &entry->stack), ui->creative_query)) ui->creative_filter[ui->creative_count++] = entry->id;
        }
        ui->creative_view = ui->creative_filter;
    }
    ui->creative_row = 0;
    ui->creative_scroll = 0;
    ui->creative_dragging = 0;
}
static void creative_select(maage_client_ui *ui, maage_creative_tab_id id) {
    if (!maage_creative_tab(ui->creative, id)) return;
    ui->creative_tab = id;
    ui->creative_query[0] = '\0';
    ui->creative_query_cursor = ui->creative_query_anchor = 0;
    ui->creative_search_reset = 0;
    maage_client_ui_inventory_reset(ui);
    creative_filter(ui);
}
static maage_client_ui_rect creative_tab_rectangle(maage_client_ui_rect frame, unsigned int index, int hitbox) {
    maage_client_ui_rect rectangle;
    unsigned int column = index % 6;
    rectangle.x = frame.x + (column == 5 ? frame.width - 28 + (hitbox ? 2 : 0) : column * 29.0f);
    rectangle.y = frame.y + (index < 6 ? (hitbox ? -32 : -28) : frame.height - (hitbox ? 0 : 4));
    rectangle.width = 28;
    rectangle.height = 32;
    return rectangle;
}
static maage_client_ui_rect creative_page_button(const maage_client_ui *ui, maage_client_ui_rect frame, int next) {
    maage_client_ui_rect rectangle;
    rectangle.width = rectangle.height = 20;
    rectangle.x = frame.x + (next ? frame.width - 20 : 0);
    rectangle.y = frame.y - 50;
    if (rectangle.y < ui->content.y) {
        rectangle.x = next ? frame.x + frame.width + 5 : frame.x - 25;
        rectangle.y = frame.y - 24;
    }
    return rectangle;
}
static size_t creative_scroll_rows(const maage_client_ui *ui) {
    /* The original normalized scroll has one trailing partial/empty row when
     * the entry count is a multiple of nine and exceeds the visible grid. */
    return ui->creative_count > 45 ? ui->creative_count / 9 - 4 : 0;
}
static maage_creative_entry_id creative_hover_entry(const maage_client_ui *ui, maage_client_ui_rect frame, int *cell) {
    unsigned int slot;
    size_t index;
    *cell = 0;
    for (slot = 0; slot < 45; ++slot)
        if (inventory_slot_contains(ui, inventory_catalog_cell(frame, slot))) {
            *cell = 1;
            index = ui->creative_row * 9 + slot;
            return index < ui->creative_count ? ui->creative_view[index] : 0;
        }
    return 0;
}
static void creative_command(maage_client_ui *ui, unsigned int kind, unsigned int secondary, maage_creative_entry_id entry, unsigned int hotbar, const maage_inventory_operation *operation) {
    maage_session_command command;
    memset(&command, 0, sizeof(command));
    command.kind = MAAGE_CMD_CREATIVE_ACTION;
    command.container_identity = ui->snapshot.player.crafting.identity;
    command.container_revision = ui->snapshot.player.crafting.revision;
    command.creative.tab = ui->creative_tab;
    command.creative.kind = kind;
    command.creative.secondary = secondary;
    command.creative.entry = entry;
    command.creative.hotbar = hotbar;
    if (operation) command.creative.inventory = *operation;
    ui->route_result = maage_client_ui_queue_command(ui, &command);
}
static void inventory_command(maage_client_ui *ui, const maage_client_ui_input *input, unsigned int group, unsigned int slot) {
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
static void creative_query_delete_selection(maage_client_ui *ui) {
    size_t first = ui->creative_query_cursor, last = ui->creative_query_anchor;
    if (first > last) {
        first = last;
        last = ui->creative_query_cursor;
    }
    memmove(ui->creative_query + first, ui->creative_query + last, strlen(ui->creative_query + last) + 1);
    ui->creative_query_anchor = ui->creative_query_cursor = first;
}
static int creative_search_input(maage_client_ui *ui, const maage_client_ui_input *input, int hotbar_key) {
    const maage_input_snapshot *raw = input->raw;
    const char *text = input->text;
    size_t length, i, cursor;
    int changed = 0, key = 0;
    if (!maage_client_ui_search_focused(ui)) {
        if (raw && raw->pressed[MAAGE_INPUT_KEY_T] && ui->creative_tabs[5]) {
            creative_select(ui, ui->creative_tabs[5]);
            return 1;
        }
        return 0;
    }
    if (raw) key = raw->first_pressed != 0 && raw->first_pressed < MAAGE_INPUT_MOUSE_BASE;
    if ((text && *text) || key) {
        if (ui->creative_search_reset) {
            ui->creative_query[0] = '\0';
            ui->creative_query_cursor = ui->creative_query_anchor = 0;
            ui->creative_search_reset = 0;
            changed = 1;
        }
    }
    if (hotbar_key >= 0 && !ui->snapshot.player.inventory.cursor.count) {
        if (changed) creative_filter(ui);
        return 0;
    }
    length = strlen(ui->creative_query);
    if (raw && raw->keys[MAAGE_INPUT_KEY_CONTROL] && raw->pressed[MAAGE_INPUT_KEY_A]) {
        ui->creative_query_anchor = 0;
        ui->creative_query_cursor = length;
    } else if (raw && (raw->pressed[MAAGE_INPUT_KEY_LEFT] || raw->pressed[MAAGE_INPUT_KEY_RIGHT] || raw->pressed[MAAGE_INPUT_KEY_HOME] || raw->pressed[MAAGE_INPUT_KEY_END])) {
        if (raw->pressed[MAAGE_INPUT_KEY_HOME]) ui->creative_query_cursor = 0;
        else if (raw->pressed[MAAGE_INPUT_KEY_END]) ui->creative_query_cursor = length;
        else if (raw->pressed[MAAGE_INPUT_KEY_LEFT] && ui->creative_query_cursor) {
            --ui->creative_query_cursor;
            if (raw->keys[MAAGE_INPUT_KEY_CONTROL]) {
                while (ui->creative_query_cursor && ui->creative_query[ui->creative_query_cursor] == ' ')
                    --ui->creative_query_cursor;
                while (ui->creative_query_cursor && ui->creative_query[ui->creative_query_cursor - 1] != ' ')
                    --ui->creative_query_cursor;
            }
        } else if (raw->pressed[MAAGE_INPUT_KEY_RIGHT] && ui->creative_query_cursor < length) {
            ++ui->creative_query_cursor;
            if (raw->keys[MAAGE_INPUT_KEY_CONTROL]) {
                while (ui->creative_query[ui->creative_query_cursor] && ui->creative_query[ui->creative_query_cursor] != ' ')
                    ++ui->creative_query_cursor;
                while (ui->creative_query[ui->creative_query_cursor] == ' ')
                    ++ui->creative_query_cursor;
            }
        }
        if (!input->shift) ui->creative_query_anchor = ui->creative_query_cursor;
    } else if (raw && raw->pressed[MAAGE_INPUT_KEY_DELETE]) {
        if (ui->creative_query_cursor != ui->creative_query_anchor) creative_query_delete_selection(ui);
        else if (ui->creative_query_cursor < length) {
            cursor = ui->creative_query_cursor + 1;
            if (raw->keys[MAAGE_INPUT_KEY_CONTROL]) {
                while (ui->creative_query[cursor] && ui->creative_query[cursor] != ' ')
                    ++cursor;
                while (ui->creative_query[cursor] == ' ')
                    ++cursor;
            }
            ui->creative_query_anchor = cursor;
            creative_query_delete_selection(ui);
        }
        changed = 1;
    }
    if (text) {
        for (i = 0; text[i]; ++i) {
            if (raw && raw->keys[MAAGE_INPUT_KEY_CONTROL] && text[i] != '\b') continue;
            if (text[i] == '\b') {
                if (ui->creative_query_cursor != ui->creative_query_anchor) creative_query_delete_selection(ui);
                else if (ui->creative_query_cursor) {
                    cursor = ui->creative_query_cursor - 1;
                    if (raw && raw->keys[MAAGE_INPUT_KEY_CONTROL]) {
                        while (cursor && ui->creative_query[cursor] == ' ')
                            --cursor;
                        while (cursor && ui->creative_query[cursor - 1] != ' ')
                            --cursor;
                    }
                    ui->creative_query_anchor = cursor;
                    creative_query_delete_selection(ui);
                }
                changed = 1;
            } else if ((unsigned char) text[i] >= 32 && (unsigned char) text[i] <= 126) {
                if (ui->creative_query_cursor != ui->creative_query_anchor) creative_query_delete_selection(ui);
                length = strlen(ui->creative_query);
                if (length < 15) {
                    memmove(ui->creative_query + ui->creative_query_cursor + 1, ui->creative_query + ui->creative_query_cursor, length - ui->creative_query_cursor + 1);
                    ui->creative_query[ui->creative_query_cursor++] = text[i];
                    ui->creative_query_anchor = ui->creative_query_cursor;
                    changed = 1;
                }
            }
        }
    }
    if (changed) creative_filter(ui);
    if (raw && raw->keys[MAAGE_INPUT_KEY_CONTROL] && !raw->pressed[MAAGE_INPUT_KEY_A] && !raw->pressed[MAAGE_INPUT_KEY_DELETE] && !raw->pressed[MAAGE_INPUT_KEY_LEFT] && !raw->pressed[MAAGE_INPUT_KEY_RIGHT] && !raw->pressed[MAAGE_INPUT_KEY_HOME] && !raw->pressed[MAAGE_INPUT_KEY_END]) return 0;
    return 1;
}
static size_t creative_query_first(const maage_client_ui *ui);
static int creative_navigation_input(maage_client_ui *ui, const maage_client_ui_input *input, maage_client_ui_rect frame) {
    maage_client_ui_rect rectangle;
    const maage_creative_tab_definition *tab = inventory_tab(ui);
    unsigned int i;
    size_t maximum;
    int released = !input->primary_down;
    if (ui->creative_pressed_tab && released) {
        maage_creative_tab_id pressed = ui->creative_pressed_tab;
        ui->creative_pressed_tab = 0;
        for (i = 0; i < 12; ++i)
            if (ui->creative_tabs[i] == pressed && inventory_contains(ui, creative_tab_rectangle(frame, i, 1))) creative_select(ui, pressed);
        return 1;
    }
    if (input->primary_pressed) {
        for (i = 0; i < 12; ++i)
            if (ui->creative_tabs[i] && inventory_contains(ui, creative_tab_rectangle(frame, i, 1))) {
                ui->creative_pressed_tab = ui->creative_tabs[i];
                ui->inventory_drag_active = 0;
                return 1;
            }
        if (ui->creative_page_count > 1) {
            for (i = 0; i < 2; ++i)
                if (inventory_contains(ui, creative_page_button(ui, frame, i != 0))) {
                    if (!i && ui->creative_tab_page) --ui->creative_tab_page;
                    if (i && ui->creative_tab_page + 1 < ui->creative_page_count) ++ui->creative_tab_page;
                    creative_tabs_page(ui);
                    return 1;
                }
        }
        if (tab && tab->kind == MAAGE_CREATIVE_SEARCH) {
            rectangle.x = frame.x + 82;
            rectangle.y = frame.y + 6;
            rectangle.width = 89;
            rectangle.height = 9;
            if (inventory_contains(ui, rectangle)) {
                float x = rectangle.x;
                size_t cursor = creative_query_first(ui);
                while (ui->creative_query[cursor] && x + ui->resources.glyph_width[(unsigned char) ui->creative_query[cursor]] * 0.5f < ui->mouse_x)
                    x += ui->resources.glyph_width[(unsigned char) ui->creative_query[cursor++]];
                ui->creative_query_cursor = ui->creative_query_anchor = cursor;
                return 1;
            }
        }
    }
    if (!tab || tab->kind == MAAGE_CREATIVE_INVENTORY) return 0;
    maximum = creative_scroll_rows(ui);
    if (input->scroll && maximum) {
        ui->creative_scroll -= (float) ((input->scroll > 0 ? 1 : -1) / (double) maximum);
        if (ui->creative_scroll < 0) ui->creative_scroll = 0;
        if (ui->creative_scroll > 1) ui->creative_scroll = 1;
        ui->creative_row = (size_t) (ui->creative_scroll * maximum + 0.5f);
    }
    rectangle.x = frame.x + 175;
    rectangle.y = frame.y + 18;
    rectangle.width = 14;
    rectangle.height = 112;
    if (input->primary_pressed && inventory_contains(ui, rectangle)) {
        ui->creative_dragging = maximum != 0;
        return 1;
    }
    if (!input->primary_down) ui->creative_dragging = 0;
    if (ui->creative_dragging) {
        ui->creative_scroll = (ui->mouse_y - rectangle.y - 7.5f) / 97;
        if (ui->creative_scroll < 0) ui->creative_scroll = 0;
        if (ui->creative_scroll > 1) ui->creative_scroll = 1;
        ui->creative_row = (size_t) (ui->creative_scroll * maximum + 0.5f);
        return 1;
    }
    return ui->creative_pressed_tab != 0;
}
static int creative_drag_eligible(const maage_client_ui *ui, unsigned int slot) {
    const maage_item_stack *cursor = &ui->snapshot.player.inventory.cursor, *source = &ui->snapshot.player.inventory.slots[slot];
    return cursor->count && (!source->count || (source->item_id == cursor->item_id && source->metadata == cursor->metadata && source->damage == cursor->damage));
}
static unsigned int inventory_input(maage_client_ui *ui, const maage_client_ui_input *input, void *context) {
    const maage_container_definition *definition = inventory_definition(ui);
    const maage_creative_tab_definition *tab;
    const maage_input_snapshot *raw = input->raw;
    maage_client_ui_rect frame, rectangle;
    maage_creative_entry_id entry = 0;
    maage_inventory_operation operation;
    maage_session_command command;
    size_t pages;
    unsigned int i, slot = MAAGE_INVENTORY_SLOTS, count = 0, secondary;
    int creative, cell = 0, hotbar = -1, middle = raw && raw->mouse_pressed[2], drop = 0, consumed;
    (void) context;
    if (!ui->has_snapshot || !ui->items || !definition || ui->container_close_requested || !ui->snapshot.player.crafting.kind || ui->snapshot.player.crafting.closing) return 0;
    if (raw && (!raw->focused || raw->focus_lost)) {
        maage_client_ui_inventory_reset(ui);
        return 0;
    }
    creative = inventory_creative(ui);
    frame = inventory_frame(ui);
    tab = inventory_tab(ui);
    if (creative && creative_navigation_input(ui, input, frame)) return 0;
    if (raw && ui->controls) {
        for (i = 0; i < MAAGE_HOTBAR_SLOTS; ++i)
            if (maage_control_gui_pressed(ui->controls, ui->controls->hotbar[i], raw)) hotbar = (int) i;
        drop = maage_control_gui_pressed(ui->controls, ui->controls->drop, raw);
    }
    consumed = creative ? creative_search_input(ui, input, hotbar) : 0;
    tab = inventory_tab(ui);
    if (creative && (consumed || (maage_client_ui_search_focused(ui) && ui->snapshot.player.inventory.cursor.count))) hotbar = -1;
    if (creative && consumed && input->text && *input->text) drop = 0;
    for (i = 0; i < inventory_slot_count(ui); ++i)
        if (inventory_slot_contains(ui, inventory_slot(ui, frame, i))) {
            slot = i;
            break;
        }
    if (creative && tab && tab->kind != MAAGE_CREATIVE_INVENTORY) entry = creative_hover_entry(ui, frame, &cell);
    if (creative) {
        memset(&operation, 0, sizeof(operation));
        if (hotbar >= 0) {
            if (cell) creative_command(ui, MAAGE_CREATIVE_HOTBAR, 0, entry, (unsigned int) hotbar, NULL);
            else if (slot < MAAGE_INVENTORY_SLOTS) {
                operation.kind = MAAGE_INVENTORY_HOTBAR_SWAP;
                operation.slot = slot;
                operation.hotbar = (unsigned int) hotbar;
                creative_command(ui, MAAGE_CREATIVE_PLAYER_SLOT, 0, 0, 0, &operation);
            }
            return 0;
        }
        if (drop && !consumed) {
            secondary = raw && raw->keys[MAAGE_INPUT_KEY_CONTROL] ? 1U : 0U;
            if (cell) creative_command(ui, MAAGE_CREATIVE_DROP, secondary, entry, 0, NULL);
            else if (slot < MAAGE_INVENTORY_SLOTS) {
                operation.slot = slot;
                creative_command(ui, MAAGE_CREATIVE_PLAYER_DROP, secondary, 0, 0, &operation);
            }
            return 0;
        }
        if (middle) {
            if (cell) creative_command(ui, MAAGE_CREATIVE_CLONE, 0, entry, 0, NULL);
            else if (slot < MAAGE_INVENTORY_SLOTS) {
                operation.kind = MAAGE_INVENTORY_CLONE;
                operation.slot = slot;
                creative_command(ui, MAAGE_CREATIVE_PLAYER_SLOT, 0, 0, 0, &operation);
            }
            return 0;
        }
        if (ui->inventory_drag_active) {
            secondary = ui->inventory_drag.secondary;
            if (secondary ? input->primary_pressed : input->secondary_pressed) {
                ui->inventory_drag_active = 0;
                return 0;
            }
            if (slot < MAAGE_INVENTORY_SLOTS && creative_drag_eligible(ui, slot)) {
                for (i = 0; i < MAAGE_INVENTORY_SLOTS; ++i)
                    count += ui->inventory_drag.slots[i] != 0;
                if (count < ui->snapshot.player.inventory.cursor.count) ui->inventory_drag.slots[slot] = 1;
            }
            if (secondary ? !(raw && raw->secondary_down) : !input->primary_down) {
                if (tab->kind == MAAGE_CREATIVE_INVENTORY && inventory_slot_contains(ui, inventory_destroy_slot(frame))) creative_command(ui, MAAGE_CREATIVE_DESTROY, 0, 0, 0, NULL);
                else {
                    count = 0;
                    for (i = 0; i < MAAGE_INVENTORY_SLOTS; ++i)
                        count += ui->inventory_drag.slots[i] != 0;
                    if (count <= 1 && !inventory_contains(ui, frame)) creative_command(ui, MAAGE_CREATIVE_OUTSIDE, secondary, 0, 0, NULL);
                    else {
                        ui->inventory_drag.kind = count > 1 ? MAAGE_INVENTORY_DISTRIBUTE : MAAGE_INVENTORY_CLICK;
                        creative_command(ui, MAAGE_CREATIVE_PLAYER_SLOT, 0, 0, 0, &ui->inventory_drag);
                    }
                }
                ui->inventory_drag_active = 0;
            }
            return 0;
        }
        if (!input->primary_pressed && !input->secondary_pressed) return 0;
        secondary = input->secondary_pressed ? 1U : 0U;
        ui->creative_search_reset = 1;
        if (tab->kind == MAAGE_CREATIVE_INVENTORY && inventory_slot_contains(ui, inventory_destroy_slot(frame))) {
            creative_command(ui, input->shift ? MAAGE_CREATIVE_CLEAR : MAAGE_CREATIVE_DESTROY, secondary, 0, 0, NULL);
            return 0;
        }
        if (cell) {
            creative_command(ui, input->shift ? MAAGE_CREATIVE_TAKE_STACK : MAAGE_CREATIVE_TAKE, secondary, entry, 0, NULL);
            return 0;
        }
        if (slot < MAAGE_INVENTORY_SLOTS) {
            operation.slot = slot;
            operation.secondary = secondary;
            if (input->shift) {
                operation.kind = MAAGE_INVENTORY_QUICK_MOVE;
                if (ui->inventory_last_shift && ui->inventory_last_slot == slot && ui->inventory_last_button == secondary && ui->inventory_last_click >= 0 && ui->notice_now - ui->inventory_last_click < 0.25 && ui->inventory_last_stack.count) {
                    operation.kind = MAAGE_INVENTORY_QUICK_MOVE_MATCHING;
                    operation.match = ui->inventory_last_stack;
                } else ui->inventory_last_stack = ui->snapshot.player.inventory.slots[slot];
            } else if (!input->shift && !ui->inventory_last_shift && !secondary && ui->inventory_last_slot == slot && ui->inventory_last_button == secondary && ui->inventory_last_click >= 0 && ui->notice_now - ui->inventory_last_click < 0.25) operation.kind = MAAGE_INVENTORY_COLLECT;
            else if (ui->snapshot.player.inventory.cursor.count) {
                ui->inventory_drag = operation;
                ui->inventory_drag_active = 1;
                if (creative_drag_eligible(ui, slot)) ui->inventory_drag.slots[slot] = 1;
            } else operation.kind = MAAGE_INVENTORY_CLICK;
            ui->inventory_last_slot = slot;
            ui->inventory_last_button = secondary;
            ui->inventory_last_click = ui->notice_now;
            ui->inventory_last_shift = input->shift;
            if (!ui->inventory_drag_active) creative_command(ui, MAAGE_CREATIVE_PLAYER_SLOT, 0, 0, 0, &operation);
            return 0;
        }
        if (!inventory_contains(ui, frame)) creative_command(ui, MAAGE_CREATIVE_OUTSIDE, secondary, 0, 0, NULL);
        return 0;
    }
    pages = definition->slot_count > definition->progress_count ? definition->slot_count : definition->progress_count;
    if (input->scroll && pages > MAAGE_CONTAINER_VIEW_SLOTS && !ui->container_command_pending) {
        unsigned long first = ui->snapshot.player.crafting.view_first;
        if (input->scroll > 0 && first >= MAAGE_CONTAINER_VIEW_SLOTS) first -= MAAGE_CONTAINER_VIEW_SLOTS;
        if (input->scroll < 0 && pages - first > MAAGE_CONTAINER_VIEW_SLOTS) first += MAAGE_CONTAINER_VIEW_SLOTS;
        if (first != ui->snapshot.player.crafting.view_first) {
            memset(&command, 0, sizeof(command));
            command.kind = MAAGE_CMD_CONTAINER_PAGE;
            command.container_identity = ui->snapshot.player.crafting.identity;
            command.container_view_first = first;
            ui->route_result = maage_client_ui_queue_command(ui, &command);
        }
    }
    if (!input->primary_pressed && !input->secondary_pressed) return 0;
    if (slot < MAAGE_INVENTORY_SLOTS) {
        inventory_command(ui, input, MAAGE_SLOT_INVENTORY, slot);
        return 0;
    }
    for (i = 0; i < ui->snapshot.player.crafting.view_count; ++i) {
        const maage_container_slot_definition *registered = &definition->slots[ui->snapshot.player.crafting.view_first + i];
        rectangle = inventory_registered_slot(frame, registered);
        if (inventory_slot_contains(ui, rectangle)) {
            if ((registered->flags & MAAGE_CONTAINER_SLOT_REVISION) && ui->container_command_pending) return 0;
            inventory_command(ui, input, registered->group, registered->slot);
            return 0;
        }
    }
    if (!inventory_contains(ui, frame)) {
        memset(&command, 0, sizeof(command));
        command.kind = MAAGE_CMD_DROP;
        command.inventory_slot = MAAGE_INVENTORY_SLOTS;
        command.container_identity = ui->snapshot.player.crafting.identity;
        command.secondary_click = input->primary_pressed != 0;
        ui->route_result = maage_client_ui_queue_command(ui, &command);
    }
    return 0;
}
static void inventory_sprite(maage_client_ui *ui, maage_client_ui_rect rectangle, maage_texture_id texture, float x, float y, float width, float height, float edge) {
    float crop[4];
    crop[0] = x / edge;
    crop[1] = y / edge;
    crop[2] = (x + width) / edge;
    crop[3] = (y + height) / edge;
    maage_client_ui_registered_texture(ui, rectangle, texture, crop);
}
static void creative_draw_tab(maage_client_ui *ui, maage_client_ui_rect frame, unsigned int index) {
    const maage_creative_tab_definition *tab = maage_creative_tab(ui->creative, ui->creative_tabs[index]);
    const maage_creative_icon_quad *quad;
    maage_client_ui_rect rectangle = creative_tab_rectangle(frame, index, 0);
    maage_render_ui_vertex vertices[4];
    float uv[4], x, y;
    size_t i;
    unsigned int corner;
    if (!tab) return;
    inventory_sprite(ui, rectangle, inventory_definition(ui)->presentation.creative_tabs, (float) (index % 6) * 28, (tab->id == ui->creative_tab ? 32.0f : 0) + (index >= 6 ? 64 : 0), 28, 32, 256);
    x = rectangle.x + 6;
    y = rectangle.y + (index < 6 ? 9 : 7);
    if (tab->icon.count) maage_client_ui_stack(ui, &tab->icon, x, y);
    for (i = 0; i < tab->icon_quad_count; ++i) {
        quad = &tab->icon_quads[i];
        if (quad->texture >= ui->item_texture_count) {
            ui->draw_result = maage_status_make(MAAGE_STATUS_INVARIANT_VIOLATION, MAAGE_CONTEXT_GUI, MAAGE_OPERATION_ATLAS, 0);
            return;
        }
        memcpy(uv, ui->item_uv[quad->texture], sizeof(uv));
        for (corner = 0; corner < 4; ++corner) {
            vertices[corner].position[0] = x + quad->position[corner][0];
            vertices[corner].position[1] = y + quad->position[corner][1];
            vertices[corner].uv[0] = (uv[0] + (uv[2] - uv[0]) * quad->uv[corner][0]) * ui->resources.block_atlas_width / ui->resources.atlas_edge;
            vertices[corner].uv[1] = (MAAGE_CLIENT_UI_BLOCK_ATLAS_Y + (uv[1] + (uv[3] - uv[1]) * quad->uv[corner][1]) * ui->resources.block_atlas_height) / ui->resources.atlas_edge;
            memcpy(vertices[corner].color, quad->color, sizeof(quad->color));
        }
        if (!maage_client_ui_append_quads(ui, vertices, 1)) return;
    }
}
static size_t creative_query_first(const maage_client_ui *ui) {
    float width = 0;
    size_t first = ui->creative_query_cursor;
    while (first && width + ui->resources.glyph_width[(unsigned char) ui->creative_query[first - 1]] < 88)
        width += ui->resources.glyph_width[(unsigned char) ui->creative_query[--first]];
    return first;
}
static void creative_draw_search(maage_client_ui *ui, maage_client_ui_rect frame) {
    maage_client_ui_rect previous = ui->clip, field, selection;
    char text[16];
    size_t first = creative_query_first(ui), i, lower = ui->creative_query_cursor, upper = ui->creative_query_anchor;
    float x = frame.x + 82, cursor_x = x, a = x, b = x;
    field.x = x;
    field.y = frame.y + 6;
    field.width = 89;
    field.height = 9;
    ui->clip = field;
    strcpy(text, ui->creative_query + first);
    if (lower > upper) {
        lower = upper;
        upper = ui->creative_query_cursor;
    }
    for (i = first; ui->creative_query[i]; ++i) {
        if (i < ui->creative_query_cursor) cursor_x += ui->resources.glyph_width[(unsigned char) ui->creative_query[i]];
        if (i < lower) a += ui->resources.glyph_width[(unsigned char) ui->creative_query[i]];
        if (i < upper) b += ui->resources.glyph_width[(unsigned char) ui->creative_query[i]];
    }
    if (lower != upper) {
        selection = field;
        selection.x = a;
        selection.width = b - a;
        maage_client_ui_panel(ui, selection, 0, 0, 0.8f, 1);
    }
    maage_client_ui_text_shadow(ui, text, x, field.y, MAAGE_CLIENT_UI_LEFT, 1, 1, 1);
    if (fmod(ui->notice_now, 0.6) < 0.3) {
        if (ui->creative_query[ui->creative_query_cursor]) {
            selection = field;
            selection.x = cursor_x - 1;
            selection.width = 1;
            maage_client_ui_panel(ui, selection, 0.82f, 0.82f, 0.82f, 1);
        } else maage_client_ui_text_shadow(ui, "_", cursor_x, field.y, MAAGE_CLIENT_UI_LEFT, 1, 1, 1);
    }
    ui->clip = previous;
}
static unsigned int inventory_tooltip_advance(const maage_client_ui *ui, char character, int bold) {
    unsigned int glyph = (unsigned char) (character >= 32 && character <= 126 ? character : '?');
    /* The extra bold stroke consumes one logical pixel per glyph. */
    return ui->resources.glyph_width[glyph] + (bold ? 1U : 0U);
}
static void inventory_tooltip_measure(const maage_client_ui *ui, const char *text, float *width, unsigned int *lines, int bold) {
    float current = 0;
    if (!text || !*text) return;
    ++*lines;
    while (*text) {
        if (*text == '\n') {
            if (current > *width) *width = current;
            current = 0;
            ++*lines;
        } else current += inventory_tooltip_advance(ui, *text, bold);
        ++text;
    }
    if (current > *width) *width = current;
}
static void inventory_tooltip_line(maage_client_ui *ui, const char *text, float x, float *y, float red, float green, float blue, int bold) {
    const char *cursor;
    char character[2];
    float origin = x, line_y = *y, offset, shade;
    unsigned int pass;
    if (!text || !*text) return;
    character[1] = '\0';
    /* Finish the complete shadow before drawing either foreground stroke. */
    for (pass = 0; pass < 2; ++pass) {
        offset = pass ? 0 : 1;
        shade = pass ? 1 : 0.25f;
        x = origin + offset;
        line_y = *y + offset;
        for (cursor = text; *cursor; ++cursor) {
            if (*cursor == '\n') {
                x = origin + offset;
                line_y += 10;
            } else {
                character[0] = *cursor;
                maage_client_ui_text(ui, character, x, line_y, MAAGE_CLIENT_UI_LEFT, red * shade, green * shade, blue * shade);
                if (bold) maage_client_ui_text(ui, character, x + 1, line_y, MAAGE_CLIENT_UI_LEFT, red * shade, green * shade, blue * shade);
                x += inventory_tooltip_advance(ui, *cursor, bold);
            }
        }
    }
    *y = line_y + 10;
}
static void inventory_tooltip(maage_client_ui *ui, const char *title, const char *category, const char *extra, const char *item_tooltip) {
    maage_client_ui_rect rectangle, panel, previous;
    unsigned int lines = 0;
    float width = 0, y;
    if (!title || ui->snapshot.player.inventory.cursor.count || ui->inventory_drag_active || ui->content.width <= 8 || ui->content.height <= 16) return;
    inventory_tooltip_measure(ui, title, &width, &lines, 0);
    inventory_tooltip_measure(ui, category, &width, &lines, 1);
    inventory_tooltip_measure(ui, extra, &width, &lines, 0);
    inventory_tooltip_measure(ui, item_tooltip, &width, &lines, 0);
    rectangle.x = ui->mouse_x + 12;
    rectangle.y = ui->mouse_y - 12;
    rectangle.width = width;
    rectangle.height = lines == 1 ? 8 : 10.0f * lines;
    if (rectangle.width > ui->content.width - 8) rectangle.width = ui->content.width - 8;
    if (rectangle.height > ui->content.height - 8) rectangle.height = ui->content.height - 8;
    if (rectangle.x + rectangle.width + 4 > ui->content.x + ui->content.width) rectangle.x -= 28 + rectangle.width;
    if (rectangle.x < ui->content.x + 4) rectangle.x = ui->content.x + 4;
    if (rectangle.y < ui->content.y + 4) rectangle.y = ui->content.y + 4;
    if (rectangle.y + rectangle.height + 4 > ui->content.y + ui->content.height) rectangle.y = ui->content.y + ui->content.height - rectangle.height - 4;
    panel = rectangle;
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
    panel.y += rectangle.height + 5;
    maage_client_ui_gradient(ui, panel, 0x5028007FUL, 0x5028007FUL);
    panel.y -= rectangle.height + 4;
    panel.width = 1;
    panel.height = rectangle.height + 4;
    maage_client_ui_gradient(ui, panel, 0x505000FFUL, 0x5028007FUL);
    panel.x += rectangle.width + 5;
    maage_client_ui_gradient(ui, panel, 0x505000FFUL, 0x5028007FUL);
    previous = ui->clip;
    ui->clip = rectangle;
    ui->clip.width += 1;
    ui->clip.height += 1;
    y = rectangle.y;
    inventory_tooltip_line(ui, title, rectangle.x, &y, 1, 1, 1, 0);
    if (lines > 1) y += 2;
    inventory_tooltip_line(ui, category, rectangle.x, &y, 0.33f, 0.33f, 1, 1);
    inventory_tooltip_line(ui, extra, rectangle.x, &y, 0.67f, 0.67f, 0.67f, 0);
    inventory_tooltip_line(ui, item_tooltip, rectangle.x, &y, 0.67f, 0.67f, 0.67f, 0);
    ui->clip = previous;
}
static void inventory_draw(maage_client_ui *ui, void *context) {
    const maage_container_definition *definition = inventory_definition(ui);
    const maage_container_presentation *presentation;
    const maage_creative_tab_definition *tab = inventory_tab(ui), *category;
    const maage_creative_entry *entry;
    const maage_item_stack *source;
    const char *hover = NULL, *hover_category = NULL, *hover_extra = NULL, *hover_item_tooltip = NULL;
    maage_client_ui_rect frame, rectangle;
    maage_inventory displayed;
    maage_inventory_operation preview;
    maage_status result;
    size_t i, index;
    unsigned int slot, selected_tab = 12, count;
    int creative = inventory_creative(ui);
    (void) context;
    if (!ui->has_snapshot || !ui->items || !definition) return;
    presentation = &definition->presentation;
    frame = inventory_frame(ui);
    displayed = ui->snapshot.player.inventory;
    if (creative && ui->inventory_drag_active) {
        count = 0;
        for (i = 0; i < MAAGE_INVENTORY_SLOTS; ++i)
            count += ui->inventory_drag.slots[i] != 0;
        if (count > 1) {
            preview = ui->inventory_drag;
            preview.kind = MAAGE_INVENTORY_DISTRIBUTE;
            result = maage_inventory_operate(&displayed, ui->items, &preview, 1, tab->kind != MAAGE_CREATIVE_INVENTORY);
            if (maage_status_failed(result)) ui->draw_result = result;
        }
    }
    if (creative) {
        for (slot = 0; slot < 12; ++slot) {
            if (ui->creative_tabs[slot] == ui->creative_tab) selected_tab = slot;
            else creative_draw_tab(ui, frame, slot);
        }
    }
    if (!creative && presentation->background_split) {
        rectangle = frame;
        rectangle.height = (float) presentation->background_split;
        inventory_sprite(ui, rectangle, presentation->background, 0, 0, frame.width, rectangle.height, (float) presentation->texture_edge);
        rectangle.y += rectangle.height;
        rectangle.height = frame.height - rectangle.height;
        inventory_sprite(ui, rectangle, presentation->background, 0, (float) presentation->background_bottom, frame.width, rectangle.height, (float) presentation->texture_edge);
    } else inventory_sprite(ui, frame, creative ? tab->background : presentation->background, 0, 0, frame.width, frame.height, (float) presentation->texture_edge);
    if (creative) {
        if (tab->kind != MAAGE_CREATIVE_INVENTORY) {
            maage_client_ui_text(ui, tab->title, frame.x + 8, frame.y + 6, MAAGE_CLIENT_UI_LEFT, 0.25f, 0.25f, 0.25f);
            if (tab->kind == MAAGE_CREATIVE_SEARCH) creative_draw_search(ui, frame);
            rectangle.x = frame.x + 175;
            rectangle.y = frame.y + 18 + (float) (int) (95 * ui->creative_scroll);
            rectangle.width = 12;
            rectangle.height = 15;
            inventory_sprite(ui, rectangle, presentation->creative_tabs, creative_scroll_rows(ui) ? 232.0f : 244.0f, 0, 12, 15, 256);
        }
        if (selected_tab < 12) creative_draw_tab(ui, frame, selected_tab);
        if (ui->creative_page_count > 1) {
            char label[64];
            rectangle = creative_page_button(ui, frame, 0);
            maage_client_ui_button_state(ui, rectangle, "<", ui->creative_tab_page ? (inventory_contains(ui, rectangle) ? MAAGE_CLIENT_UI_BUTTON_HOVER : MAAGE_CLIENT_UI_BUTTON_NORMAL) : MAAGE_CLIENT_UI_BUTTON_DISABLED);
            rectangle = creative_page_button(ui, frame, 1);
            maage_client_ui_button_state(ui, rectangle, ">", ui->creative_tab_page + 1 < ui->creative_page_count ? (inventory_contains(ui, rectangle) ? MAAGE_CLIENT_UI_BUTTON_HOVER : MAAGE_CLIENT_UI_BUTTON_NORMAL) : MAAGE_CLIENT_UI_BUTTON_DISABLED);
            if (frame.y - 50 >= ui->content.y) {
                sprintf(label, "%lu / %lu", (unsigned long) ui->creative_tab_page + 1, (unsigned long) ui->creative_page_count);
                maage_client_ui_text(ui, label, frame.x + frame.width * 0.5f, frame.y - 44, MAAGE_CLIENT_UI_CENTER, 1, 1, 1);
            }
        }
        if (tab->kind == MAAGE_CREATIVE_INVENTORY) {
            maage_client_ui_player_preview(ui, frame, 1);
            rectangle = inventory_destroy_slot(frame);
            if (inventory_slot_contains(ui, rectangle)) {
                maage_client_ui_panel(ui, rectangle, 1, 1, 1, 0.5f);
                hover = "Destroy Item";
            }
        } else {
            for (slot = 0; slot < 45; ++slot) {
                rectangle = inventory_catalog_cell(frame, slot);
                index = ui->creative_row * 9 + slot;
                entry = index < ui->creative_count ? maage_creative_get(ui->creative, ui->creative_view[index]) : NULL;
                if (entry) maage_client_ui_stack(ui, &entry->stack, rectangle.x, rectangle.y);
                if (inventory_slot_contains(ui, rectangle)) {
                    maage_client_ui_panel(ui, rectangle, 1, 1, 1, 0.5f);
                    if (entry) {
                        hover = maage_client_item_name(ui->items, &entry->stack);
                        hover_extra = entry->tooltip;
                        hover_item_tooltip = inventory_item_tooltip(ui, &entry->stack);
                        category = maage_creative_tab(ui->creative, entry->tab);
                        if (tab->kind == MAAGE_CREATIVE_SEARCH && category) hover_category = category->title;
                    }
                }
            }
        }
        for (slot = 0; slot < 12; ++slot) {
            if (!ui->creative_tabs[slot]) continue;
            rectangle = creative_tab_rectangle(frame, slot, 1);
            rectangle.x += 3;
            rectangle.y += 3;
            rectangle.width = 23;
            rectangle.height = 27;
            if (inventory_contains(ui, rectangle)) hover = maage_creative_tab(ui->creative, ui->creative_tabs[slot])->title;
        }
    } else {
        if (presentation->title) maage_client_ui_text(ui, presentation->title, frame.x + presentation->title_x, frame.y + presentation->title_y, presentation->title_centered ? MAAGE_CLIENT_UI_CENTER : MAAGE_CLIENT_UI_LEFT, 0.25f, 0.25f, 0.25f);
        if (presentation->inventory_title) maage_client_ui_text(ui, presentation->inventory_title, frame.x + presentation->inventory_x, frame.y + presentation->inventory_y, MAAGE_CLIENT_UI_LEFT, 0.25f, 0.25f, 0.25f);
        if (presentation->player_preview) maage_client_ui_player_preview(ui, frame, 0);
        for (i = 0; i < ui->snapshot.player.crafting.progress_count; ++i)
            maage_client_ui_registered_progress(ui, frame, &definition->progress[ui->snapshot.player.crafting.view_first + i], &ui->snapshot.player.crafting.progress[i]);
        for (i = 0; i < ui->snapshot.player.crafting.view_count; ++i) {
            rectangle = inventory_registered_slot(frame, &definition->slots[ui->snapshot.player.crafting.view_first + i]);
            source = &ui->snapshot.player.crafting.view[i];
            maage_client_ui_stack(ui, source, rectangle.x, rectangle.y);
            if (inventory_slot_contains(ui, rectangle)) {
                maage_client_ui_panel(ui, rectangle, 1, 1, 1, 0.5f);
                if (source->count) {
                    hover = maage_client_item_name(ui->items, source);
                    hover_item_tooltip = inventory_item_tooltip(ui, source);
                }
            }
        }
    }
    for (slot = 0; slot < inventory_slot_count(ui); ++slot) {
        rectangle = inventory_slot(ui, frame, slot);
        maage_client_ui_stack(ui, &displayed.slots[slot], rectangle.x, rectangle.y);
        if (inventory_slot_contains(ui, rectangle)) {
            maage_client_ui_panel(ui, rectangle, 1, 1, 1, 0.5f);
            if (displayed.slots[slot].count) {
                hover = maage_client_item_name(ui->items, &displayed.slots[slot]);
                hover_item_tooltip = inventory_item_tooltip(ui, &displayed.slots[slot]);
            }
        }
    }
    inventory_tooltip(ui, hover, hover_category, hover_extra, hover_item_tooltip);
    maage_client_ui_stack(ui, &displayed.cursor, ui->mouse_x - 8, ui->mouse_y - 8);
}
maage_status maage_client_ui_inventory_register(maage_client_ui *ui) {
    maage_status result = maage_client_ui_register_screen(ui, MAAGE_CLIENT_UI_INVENTORY, inventory_draw, inventory_input, NULL);
    if (maage_status_failed(result)) return result;
    return maage_client_ui_register_screen(ui, MAAGE_CLIENT_UI_CRAFTING_TABLE, inventory_draw, inventory_input, NULL);
}
maage_status maage_client_ui_set_content(maage_client_ui *ui, const maage_item_registry *items, const maage_block_registry *blocks, const maage_creative_registry *creative, const float inventory_grass_color[3], const float (*item_uv)[4], size_t texture_count) {
    maage_creative_entry_id *filter;
    const maage_creative_tab_definition *tab;
    size_t count = maage_creative_entry_count(creative);
    if (!ui || !items || !blocks || !creative || !inventory_grass_color || !item_uv) return maage_status_make(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_CONTEXT_GUI, MAAGE_OPERATION_SET, 0);
    if (!maage_item_registry_is_finalized(items) || !maage_block_registry_is_finalized(blocks) || !maage_creative_is_finalized(creative)) return maage_status_make(MAAGE_STATUS_INVALID_STATE, MAAGE_CONTEXT_GUI, MAAGE_OPERATION_SET, 0);
    if (count > (size_t) - 1 / sizeof(*filter)) return maage_status_make(MAAGE_STATUS_BUFFER_OVERFLOW, MAAGE_CONTEXT_GUI, MAAGE_OPERATION_SET, 0);
    filter = count ? (maage_creative_entry_id *) malloc(count * sizeof(*filter)) : NULL;
    if (count && !filter) return maage_status_make(MAAGE_STATUS_ALLOCATION_FAILED, MAAGE_CONTEXT_GUI, MAAGE_OPERATION_SET, 0);
    free(ui->creative_filter);
    ui->creative_filter = filter;
    ui->creative = creative;
    ui->items = items;
    ui->blocks = blocks;
    ui->item_uv = item_uv;
    ui->item_texture_count = texture_count;
    memcpy(ui->inventory_grass_color, inventory_grass_color, sizeof(ui->inventory_grass_color));
    ui->creative_tab_page = 0;
    creative_tabs_page(ui);
    tab = maage_creative_tab_at(creative, 0);
    if (tab) creative_select(ui, tab->id);
    return maage_status_make(MAAGE_STATUS_SUCCESS, 0, MAAGE_OPERATION_SET, 0);
}

void maage_client_ui_inventory_opened(maage_client_ui *ui) {
    if (inventory_creative(ui)) creative_select(ui, ui->creative_tab);
    else maage_client_ui_inventory_reset(ui);
}
