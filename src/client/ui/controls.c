#include "internal.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

/* Header/footer coordinates and 20-pixel list rows follow the original
 * Controls layout, translated into the safe-area GUI coordinate space. */
static float controls_center(const maage_client_ui *ui) {
    return (float) (int) (ui->content.x + ui->content.width * 0.5f);
}
static maage_client_ui_rect controls_list(const maage_client_ui *ui) {
    maage_client_ui_rect r = ui->content;
    r.y += 63;
    r.height -= 95;
    if (r.height < 0) r.height = 0;
    return r;
}
static int controls_contains(maage_client_ui_rect r, float x, float y) {
    return r.width > 0 && r.height > 0 && x >= r.x && x < r.x + r.width && y >= r.y && y < r.y + r.height;
}
static maage_client_ui_rect controls_option(const maage_client_ui *ui, unsigned int id) {
    maage_client_ui_rect r;
    float width = (ui->content.width - 10) * 0.5f;
    if (width > 150) width = 150;
    if (width < 0) width = 0;
    r.width = width;
    r.height = 20;
    r.x = controls_center(ui) + (id % 2 ? 5 : -5 - width);
    r.y = ui->content.y + 18 + (float) (id / 2) * 24;
    if (id >= 3) {
        r.x = controls_center(ui) + (id == 4 ? 5 : -5 - width);
        r.y = ui->content.y + ui->content.height - 29;
    }
    return r;
}
static int controls_compare(const maage_controls *controls, size_t a, size_t b) {
    const maage_control *left = &controls->actions[a], *right = &controls->actions[b];
    int result = strcmp(maage_control_category_name(left->category), maage_control_category_name(right->category));
    return result ? result : strcmp(left->name, right->name);
}
static maage_status controls_order(maage_client_ui *ui) {
    size_t i, j;
    int category = -1, next;
    float y = 0;
    maage_client_ui_control_row value, *order;
    if (!ui->controls) return maage_status_make(MAAGE_STATUS_INVALID_STATE, MAAGE_CONTEXT_GUI, MAAGE_OPERATION_UPDATE, 0);
    if (ui->control_count == ui->controls->count) return maage_status_make(MAAGE_STATUS_NOOP, 0, MAAGE_OPERATION_UPDATE, 0);
    if (ui->controls->count > (((size_t) - 1) - 5) / 2 || ui->controls->count > ((size_t) - 1) / sizeof(*order)) return maage_status_make(MAAGE_STATUS_BUFFER_OVERFLOW, MAAGE_CONTEXT_GUI, MAAGE_OPERATION_UPDATE, 0);
    order = (maage_client_ui_control_row *) realloc(ui->control_order, ui->controls->count * sizeof(*order));
    if (!order && ui->controls->count) return maage_status_make(MAAGE_STATUS_ALLOCATION_FAILED, MAAGE_CONTEXT_GUI, MAAGE_OPERATION_UPDATE, 0);
    ui->control_order = order;
    ui->control_count = ui->controls->count;
    for (i = 0; i < ui->control_count; ++i) {
        value.handle = i;
        value.y = 0;
        j = i;
        while (j && controls_compare(ui->controls, value.handle, order[j - 1].handle) < 0) {
            order[j] = order[j - 1];
            --j;
        }
        order[j] = value;
    }
    for (i = 0; i < ui->control_count; ++i) {
        next = ui->controls->actions[order[i].handle].category;
        if (next != category) {
            y += 20;
            category = next;
        }
        order[i].y = y;
        y += 20;
    }
    return maage_status_make(MAAGE_STATUS_SUCCESS, 0, MAAGE_OPERATION_UPDATE, 0);
}
static float controls_row(const maage_client_ui *ui, size_t index) {
    if (index < ui->control_count) return ui->control_order[index].y;
    return ui->control_count ? ui->control_order[ui->control_count - 1].y + 20 : 0;
}
static float controls_max_scroll(const maage_client_ui *ui) {
    float maximum = controls_row(ui, ui->control_count) + 4 - controls_list(ui).height;
    return maximum > 0 ? maximum : 0;
}
static void controls_clamp(maage_client_ui *ui) {
    float maximum = controls_max_scroll(ui);
    if (ui->control_scroll > maximum) ui->control_scroll = maximum;
    if (ui->control_scroll < 0) ui->control_scroll = 0;
}
/* Every row shares columns measured from the whole list. Reserve selection
 * markers even when idle so entering capture cannot move buttons or hitboxes. */
static void controls_layout(maage_client_ui *ui) {
    size_t i;
    char label[40], selected[48];
    float label_width = 0, key_width = 75, width, available, left;
    for (i = 0; i < ui->controls->count; ++i) {
        width = maage_client_ui_text_width(ui, ui->controls->actions[i].name);
        if (width > label_width) label_width = width;
        maage_control_label(ui->controls->actions[i].binding, label);
        sprintf(selected, "> %s <", label);
        width = maage_client_ui_text_width(ui, selected) + 8;
        if (width > key_width) key_width = width;
    }
    available = ui->content.width - 8;
    if (available < 0) available = 0;
    width = label_width + 10 + key_width + 10 + 50 + 8 + 6;
    if (width < 252) width = 252;
    if (width > available) width = available;
    left = (float) (int) (controls_center(ui) - width * 0.5f);
    ui->control_scrollbar_x = left + width - 6;
    ui->control_reset_x = ui->control_scrollbar_x - 8 - 50;
    if (key_width > ui->control_reset_x - 10 - left) key_width = ui->control_reset_x - 10 - left;
    if (key_width < 0) key_width = 0;
    ui->control_key_width = key_width;
    ui->control_key_x = ui->control_reset_x - 10 - key_width;
    ui->control_label_x = ui->control_key_x - 10 - label_width;
    if (ui->control_label_x < left) ui->control_label_x = left;
}
static maage_client_ui_rect controls_binding(const maage_client_ui *ui, size_t index, int reset) {
    maage_client_ui_rect r;
    r.x = reset ? ui->control_reset_x : ui->control_key_x;
    r.y = controls_list(ui).y + 4 + controls_row(ui, index) - ui->control_scroll;
    r.width = reset ? 50 : ui->control_key_width;
    r.height = 18;
    return r;
}
static maage_client_ui_rect controls_scrollbar(const maage_client_ui *ui, int thumb) {
    maage_client_ui_rect r = controls_list(ui);
    float total = r.height + controls_max_scroll(ui), travel;
    r.x = ui->control_scrollbar_x;
    r.width = 6;
    if (thumb && total > 0) {
        r.height = r.height * r.height / total;
        if (r.height < 20) r.height = 20;
        if (r.height > controls_list(ui).height) r.height = controls_list(ui).height;
        travel = controls_list(ui).height - r.height;
        if (controls_max_scroll(ui) > 0) r.y += ui->control_scroll / controls_max_scroll(ui) * travel;
    }
    return r;
}
static void controls_click(maage_client_ui *ui) {
    if (ui->click) ui->click(ui->click_context);
}
/* Capture is routed before Escape, F3, F11, inventory and screen navigation.
 * The activation update only arms capture; its mouse press is already consumed. */
int maage_client_ui_controls_capture(maage_client_ui *ui, const maage_client_ui_input *input) {
    unsigned int binding;
    if (maage_client_ui_current(ui) != MAAGE_CLIENT_UI_CONTROLS || !ui->control_capture) return 0;
    if (!input->raw || !input->raw->focused || input->raw->focus_lost || ui->mode_pending || ui->mode_changing) {
        ui->control_capture = 0;
        return 1;
    }
    binding = input->raw->first_pressed;
    if (binding) {
        ui->route_result = maage_control_set(ui->controls, ui->control_capture - 1, binding == MAAGE_INPUT_KEY_ESCAPE ? 0 : binding);
        ui->control_capture = 0;
    }
    return 1;
}
static void controls_activate(maage_client_ui *ui, size_t focus) {
    size_t index, handle, i;
    if (focus == 0) ui->invert_mouse = !ui->invert_mouse;
    else if (focus == 1) return;
    else if (focus == 2) ui->route_result = maage_client_ui_back(ui);
    else if (focus == 3) {
        if (!maage_controls_changed(ui->controls)) return;
        for (i = 0; i < ui->control_count; ++i) {
            ui->route_result = maage_control_set(ui->controls, i, ui->controls->actions[i].default_binding);
            if (maage_status_failed(ui->route_result)) return;
        }
    } else {
        index = (focus - 4) / 2;
        if (index >= ui->control_count) return;
        handle = ui->control_order[index].handle;
        if (focus % 2) {
            if (ui->controls->actions[handle].binding == ui->controls->actions[handle].default_binding) return;
            ui->route_result = maage_control_set(ui->controls, handle, ui->controls->actions[handle].default_binding);
        } else ui->control_capture = handle + 1;
    }
    controls_click(ui);
}
static void controls_sensitivity(maage_client_ui *ui) {
    maage_client_ui_rect r = controls_option(ui, 1);
    float value = r.width > 8 ? (ui->mouse_x - r.x - 4) / (r.width - 8) : 0;
    if (value < 0) value = 0;
    if (value > 1) value = 1;
    ui->mouse_sensitivity = (int) (value * 100 + 0.5f);
}
static unsigned int controls_input(maage_client_ui *ui, const maage_client_ui_input *input, void *context) {
    size_t i, focus, count;
    maage_client_ui_rect r, list = controls_list(ui), thumb;
    float row, travel;
    int hit = 0, backwards = input->previous || (input->next && input->shift);
    (void) context;
    ui->route_result = controls_order(ui);
    if (maage_status_failed(ui->route_result)) return 0;
    controls_layout(ui);
    count = ui->control_count * 2 + 4;
    if (input->next || input->previous) {
        focus = ui->control_focus;
        do {
            focus = backwards ? (focus + count - 1) % count : (focus + 1) % count;
        } while ((focus == 3 && !maage_controls_changed(ui->controls)) || (focus >= 4 && focus % 2 && ui->controls->actions[ui->control_order[(focus - 4) / 2].handle].binding == ui->controls->actions[ui->control_order[(focus - 4) / 2].handle].default_binding));
        ui->control_focus = focus;
        ui->selected_keyboard = 1;
        if (focus >= 4) {
            row = controls_row(ui, (focus - 4) / 2) + 4;
            if (row < ui->control_scroll) ui->control_scroll = row;
            if (row + 18 > ui->control_scroll + list.height) ui->control_scroll = row + 18 - list.height;
        }
    }
    if (input->scroll && controls_contains(list, ui->mouse_x, ui->mouse_y)) ui->control_scroll -= (float) input->scroll * 10;
    if (ui->control_focus == 1 && (input->left || input->right)) {
        ui->mouse_sensitivity += input->right ? 1 : -1;
        if (ui->mouse_sensitivity < 0) ui->mouse_sensitivity = 0;
        if (ui->mouse_sensitivity > 100) ui->mouse_sensitivity = 100;
    }
    if (input->primary_pressed) {
        for (i = 0; i < 5; ++i) {
            if (i == 2) continue; /* Unsupported touchscreen control. */
            r = controls_option(ui, (unsigned int) i);
            if (!controls_contains(r, ui->mouse_x, ui->mouse_y)) continue;
            focus = i < 2 ? i : i - 1;
            ui->control_focus = focus;
            ui->selected_keyboard = 0;
            if (i == 1) {
                ui->dragging_sensitivity = 1;
                controls_sensitivity(ui);
                controls_click(ui);
            } else controls_activate(ui, focus);
            hit = 1;
            break;
        }
        if (!hit && controls_contains(list, ui->mouse_x, ui->mouse_y)) {
            if (controls_max_scroll(ui) > 0 && controls_contains(controls_scrollbar(ui, 0), ui->mouse_x, ui->mouse_y)) {
                thumb = controls_scrollbar(ui, 1);
                ui->dragging_controls = 1;
                ui->control_drag_y = ui->mouse_y;
                ui->control_drag_scroll = ui->control_scroll;
                if (!controls_contains(thumb, ui->mouse_x, ui->mouse_y)) {
                    travel = list.height - thumb.height;
                    if (travel > 0) ui->control_scroll = (ui->mouse_y - list.y - thumb.height * 0.5f) / travel * controls_max_scroll(ui);
                    controls_clamp(ui);
                    ui->control_drag_scroll = ui->control_scroll;
                }
            } else
                for (i = 0; i < ui->control_count * 2; ++i) {
                    r = controls_binding(ui, i / 2, (int) (i % 2));
                    if (controls_contains(r, ui->mouse_x, ui->mouse_y)) {
                        ui->control_focus = i + 4;
                        ui->selected_keyboard = 0;
                        controls_activate(ui, i + 4);
                        break;
                    }
                }
        }
    }
    if (ui->dragging_sensitivity && input->primary_down) controls_sensitivity(ui);
    if (ui->dragging_controls && input->primary_down) {
        travel = list.height - controls_scrollbar(ui, 1).height;
        if (travel > 0) ui->control_scroll = ui->control_drag_scroll + (ui->mouse_y - ui->control_drag_y) / travel * controls_max_scroll(ui);
    }
    if (!input->primary_down) ui->dragging_sensitivity = ui->dragging_controls = 0;
    if (input->accept && !input->primary_pressed) controls_activate(ui, ui->control_focus);
    controls_clamp(ui);
    return 0;
}
static int controls_highlight(const maage_client_ui *ui, maage_client_ui_rect r, size_t focus) {
    return ui->selected_keyboard ? ui->control_focus == focus : controls_contains(r, ui->mouse_x, ui->mouse_y);
}
static void controls_key_draw(maage_client_ui *ui, maage_client_ui_rect r, size_t handle, int highlighted) {
    char label[40], selected[48];
    maage_client_ui_rect clip = ui->clip;
    int capture = ui->control_capture == handle + 1;
    int conflict = maage_control_conflict(ui->controls, handle);
    float green = conflict ? 0.33f : 1, blue = conflict ? 0.33f : 1;
    maage_control_label(ui->controls->actions[handle].binding, label);
    if (capture) {
        sprintf(selected, "> %s <", label);
        green = 1;
        blue = 0.33f;
    }
    maage_client_ui_button(ui, r, "", highlighted || capture);
    if (r.x > ui->clip.x) {
        ui->clip.width -= r.x - ui->clip.x;
        ui->clip.x = r.x;
    }
    if (ui->clip.x + ui->clip.width > r.x + r.width) ui->clip.width = r.x + r.width - ui->clip.x;
    maage_client_ui_text_shadow(ui, capture ? selected : label, r.x + r.width * 0.5f, r.y + 5, MAAGE_CLIENT_UI_CENTER, 1, green, blue);
    ui->clip = clip;
}
static void controls_draw(maage_client_ui *ui, void *context) {
    maage_client_ui_rect list = controls_list(ui), clip = ui->clip, r, label_clip;
    size_t i, handle;
    int category = -1, next, changed;
    float center = controls_center(ui), y;
    char label[64];
    maage_status result;
    (void) context;
    result = controls_order(ui);
    if (maage_status_failed(result)) {
        ui->draw_result = result;
        return;
    }
    controls_layout(ui);
    controls_clamp(ui);
    ui->clip = list;
    maage_client_ui_panel(ui, list, 0, 0, 0, 0.55f);
    for (i = 0; i < ui->control_count; ++i) {
        handle = ui->control_order[i].handle;
        next = ui->controls->actions[handle].category;
        r = controls_binding(ui, i, 0);
        if (next != category) {
            y = r.y - 15;
            maage_client_ui_text_shadow(ui, maage_control_category_name((maage_control_category) next), center, y, MAAGE_CLIENT_UI_CENTER, 1, 1, 1);
            category = next;
        }
        if (r.y + r.height <= list.y || r.y >= list.y + list.height) continue;
        label_clip = ui->clip;
        ui->clip.x = ui->control_label_x;
        ui->clip.width = r.x - 5 - ui->control_label_x;
        if (ui->clip.width < 0) ui->clip.width = 0;
        maage_client_ui_text_shadow(ui, ui->controls->actions[handle].name, ui->control_label_x, r.y + 5, MAAGE_CLIENT_UI_LEFT, 1, 1, 1);
        ui->clip = label_clip;
        controls_key_draw(ui, r, handle, controls_highlight(ui, r, i * 2 + 4));
        r = controls_binding(ui, i, 1);
        changed = ui->controls->actions[handle].binding != ui->controls->actions[handle].default_binding;
        maage_client_ui_button_state(ui, r, "Reset", !changed ? MAAGE_CLIENT_UI_BUTTON_DISABLED : (controls_highlight(ui, r, i * 2 + 5) ? MAAGE_CLIENT_UI_BUTTON_HOVER : MAAGE_CLIENT_UI_BUTTON_NORMAL));
    }
    if (controls_max_scroll(ui) > 0) {
        maage_client_ui_panel(ui, controls_scrollbar(ui, 0), 0, 0, 0, 1);
        maage_client_ui_panel(ui, controls_scrollbar(ui, 1), 0.5f, 0.5f, 0.5f, 1);
    }
    r = list;
    r.height = r.height < 4 ? r.height : 4;
    maage_client_ui_gradient(ui, r, 0xff000000UL, 0x00000000UL);
    r.y = list.y + list.height - r.height;
    maage_client_ui_gradient(ui, r, 0x00000000UL, 0xff000000UL);
    ui->clip = clip;
    maage_client_ui_text_shadow(ui, "Controls", center, ui->content.y + 8, MAAGE_CLIENT_UI_CENTER, 1, 1, 1);
    r = controls_option(ui, 0);
    maage_client_ui_button(ui, r, ui->invert_mouse ? "Invert Mouse: ON" : "Invert Mouse: OFF", controls_highlight(ui, r, 0));
    r = controls_option(ui, 1);
    if (!ui->mouse_sensitivity) strcpy(label, "Sensitivity: *yawn*");
    else if (ui->mouse_sensitivity == 100) strcpy(label, "Sensitivity: HYPERSPEED!!!");
    else sprintf(label, "Sensitivity: %d%%", ui->mouse_sensitivity * 2);
    maage_client_ui_slider(ui, r, label, (float) ui->mouse_sensitivity / 100, controls_highlight(ui, r, 1));
    maage_client_ui_button_state(ui, controls_option(ui, 2), "Touchscreen Mode: OFF", MAAGE_CLIENT_UI_BUTTON_DISABLED);
    r = controls_option(ui, 3);
    maage_client_ui_button(ui, r, "Done", controls_highlight(ui, r, 2));
    r = controls_option(ui, 4);
    maage_client_ui_button_state(ui, r, "Reset Keys", !maage_controls_changed(ui->controls) ? MAAGE_CLIENT_UI_BUTTON_DISABLED : (controls_highlight(ui, r, 3) ? MAAGE_CLIENT_UI_BUTTON_HOVER : MAAGE_CLIENT_UI_BUTTON_NORMAL));
}
maage_status maage_client_ui_controls_register(maage_client_ui *ui) {
    return maage_client_ui_register_screen(ui, MAAGE_CLIENT_UI_CONTROLS, controls_draw, controls_input, NULL);
}
