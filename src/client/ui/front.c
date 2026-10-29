#include "front_internal.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
static maage_client_ui_rect front_rect(maage_client_ui_frontend *f, float x, float y, float w, float h) {
    maage_client_ui_rect r;
    r.x = f->ui->content.x + x;
    r.y = f->ui->content.y + y;
    r.width = w;
    r.height = h;
    return r;
}
static int front_hit(maage_client_ui_frontend *f, maage_client_ui_rect r) {
    return f->ui->mouse_x >= r.x && f->ui->mouse_x < r.x + r.width && f->ui->mouse_y >= r.y && f->ui->mouse_y < r.y + r.height;
}
static maage_client_ui_rect front_button_rect(maage_client_ui_frontend *f, unsigned int n) {
    float w = f->ui->content.width, h = f->ui->content.height;
    unsigned int screen = maage_client_ui_current(f->ui);
    if (screen == MAAGE_CLIENT_UI_WORLDS) {
        if (n < 2) return front_rect(f, w / 2 - 154 + (float) n * 158, h - 52, 150, 20);
        return front_rect(f, w / 2 - 154 + (float) (n - 2) * 78 + (n >= 4 ? 2 : 0), h - 28, 72, 20);
    }
    if (screen == MAAGE_CLIENT_UI_CREATE_WORLD) {
        if (n == 2) return front_rect(f, w / 2 - 75, 115, 150, 20);
        if (n == 5) return front_rect(f, w / 2 - 75, 187, 150, 20);
        if (n >= 6) return front_rect(f, w / 2 - 155 + (float) ((n - 6) % 2) * 160, n < 8 ? 100 : 151, 150, 20);
    }
    if (screen == MAAGE_CLIENT_UI_RENAME_WORLD) return front_rect(f, w / 2 - 100, (float) (int) (h / 4) + 108 + (float) n * 24, 200, 20);
    if (screen == MAAGE_CLIENT_UI_DELETE_WORLD) return front_rect(f, w / 2 - 155 + (float) n * 160, (float) (int) (h / 6) + 96, 150, 20);
    return front_rect(f, w / 2 - 155 + (float) n * 160, h - 28, 150, 20);
}
static void front_button(maage_client_ui_frontend *f, unsigned int n, const char *text, int enabled) {
    maage_client_ui_rect r = front_button_rect(f, n);
    maage_client_ui_button_state(f->ui, r, text,
                                 !enabled ? MAAGE_CLIENT_UI_BUTTON_DISABLED : ((f->focus_keyboard ? f->focus == n : front_hit(f, r)) ? MAAGE_CLIENT_UI_BUTTON_HOVER : MAAGE_CLIENT_UI_BUTTON_NORMAL));
}
static float maage_front_scroll_max(const maage_client_ui_frontend *f) {
    float maximum = (float) f->count * 36 - (f->ui->content.height - 96) + 4;
    return maximum > 0 ? maximum : 0;
}
static maage_client_ui_rect maage_front_scrollbar(maage_client_ui_frontend *f) {
    float height = f->ui->content.height - 96, maximum = maage_front_scroll_max(f), thumb;
    thumb = f->count ? height * height / ((float) f->count * 36) : height;
    if (thumb < 32) thumb = 32;
    if (thumb > height - 8) thumb = height - 8;
    return front_rect(f, f->ui->content.width / 2 + 124, 32 + (maximum ? f->scroll / maximum * (height - thumb) : 0), 6, thumb);
}
static void front_center(maage_client_ui_frontend *f, const char *text, float y, float r, float g, float b) {
    maage_client_ui_text_shadow(f->ui, text, f->ui->content.x + f->ui->content.width / 2, f->ui->content.y + y, MAAGE_CLIENT_UI_CENTER, r, g, b);
}
static void front_field(maage_client_ui_frontend *f, const char *text, float y, unsigned int focus) {
    maage_client_ui *ui = f->ui;
    maage_client_ui_rect r = front_rect(f, ui->content.width / 2 - 100, y, 200, 20);
    const char *visible = text;
    float edge = f->focus == focus ? 0.85f : 0.4f;
    maage_client_ui_panel(ui, r, edge, edge, edge, 1);
    r.x += 1;
    r.y += 1;
    r.width -= 2;
    r.height -= 2;
    maage_client_ui_panel(ui, r, 0, 0, 0, 1);
    while (*visible && maage_client_ui_text_width(ui, visible) > r.width - 12)
        ++visible;
    ui->clip = r;
    maage_client_ui_text(ui, visible, r.x + 4, r.y + 5, MAAGE_CLIENT_UI_LEFT, 1, 1, 1);
    if (f->focus == focus && ((unsigned long) (ui->notice_now * 20) / 6UL) % 2UL == 0) maage_client_ui_text(ui, "_", r.x + 4 + maage_client_ui_text_width(ui, visible), r.y + 5, MAAGE_CLIENT_UI_LEFT, 1, 1, 1);
    ui->clip = ui->content;
}
static int front_name_valid(const char *name) {
    while (*name) {
        if (*name != ' ' && *name != '\t') return 1;
        ++name;
    }
    return 0;
}
static int front_seed_valid(const char *seed) {
    maage_world_seed parsed;
    return !*seed || maage_status_is_success(maage_world_seed_parse(seed, &parsed));
}
static void front_draw(maage_client_ui *ui, void *context) {
    maage_client_ui_frontend *f = (maage_client_ui_frontend *) context;
    unsigned int screen = maage_client_ui_current(ui);
    size_t i, end;
    float w = ui->content.width, h = ui->content.height, y;
    maage_client_ui_rect r;
    char line[192];
    maage_client_ui_menu_background(ui);
    if (screen == MAAGE_CLIENT_UI_LOADING) {
        front_center(f, "Loading world...", h / 2 - 20, 1, 1, 1);
        front_center(f, "Preparing terrain and starting simulation", h / 2 + 4, 0.7f, 0.7f, 0.7f);
        return;
    }
    if (screen == MAAGE_CLIENT_UI_WORLDS) {
        float list_width = w < 240 ? w - 16 : 220;
        front_center(f, "Select World", 20, 1, 1, 1);
        r = front_rect(f, 0, 32, w, h - 96);
        maage_client_ui_panel(ui, r, 0, 0, 0, 0.5f);
        ui->clip = r;
        f->first = (size_t) (f->scroll / 36);
        end = f->first + (size_t) ((h - 96) / 36) + 2;
        if (end > f->count) end = f->count;
        for (i = f->first; i < end; ++i) {
            y = 36 + (float) i * 36 - f->scroll;
            r = front_rect(f, (w - list_width) / 2, y - 2, list_width, 36);
            if (i == f->selected) {
                maage_client_ui_panel(ui, r, 0.5f, 0.5f, 0.5f, 1);
                r.x += 1;
                r.y += 1;
                r.width -= 2;
                r.height -= 2;
                maage_client_ui_panel(ui, r, 0, 0, 0, 1);
            }
            ui->clip = front_rect(f, (w - list_width) / 2 + 2, y + 1, list_width - 4, 32);
            if (ui->clip.y < ui->content.y + 32) {
                ui->clip.height -= ui->content.y + 32 - ui->clip.y;
                ui->clip.y = ui->content.y + 32;
            }
            if (ui->clip.y + ui->clip.height > ui->content.y + h - 64) ui->clip.height = ui->content.y + h - 64 - ui->clip.y;
            maage_client_ui_text(ui, f->entries[i].name, ui->content.x + (w - list_width) / 2 + 2, ui->content.y + y + 1, MAAGE_CLIENT_UI_LEFT, 1, 1, 1);
            sprintf(line, "Created %.10s", f->entries[i].created_utc);
            maage_client_ui_text(ui, line, ui->content.x + (w - list_width) / 2 + 2, ui->content.y + y + 1 + 11, MAAGE_CLIENT_UI_LEFT, 0.5f, 0.5f, 0.5f);
            maage_client_ui_text(ui, maage_status_code(f->entries[i].status) == MAAGE_STATUS_MALFORMED_INPUT && maage_status_context(f->entries[i].status) == MAAGE_CONTEXT_WORLD_IO && maage_status_operation(f->entries[i].status) == MAAGE_OPERATION_LOAD && maage_status_detail(f->entries[i].status) == MAAGE_DETAIL_WORLD_FORMAT ? "Incompatible dev format" : maage_status_failed(f->entries[i].status) ? "Unreadable / unsupported"
                                                                                                                                                                                                                                                                                                                                                                 : f->entries[i].game_mode                     ? "Creative Mode"
                                                                                                                                                                                                                                                                                                                                                                                                               : "Survival Mode",
                                 ui->content.x + (w - list_width) / 2 + 2, ui->content.y + y + 22,
                                 MAAGE_CLIENT_UI_LEFT, 0.5f, 0.5f, 0.5f);
            ui->clip = front_rect(f, 0, 32, w, h - 96);
        }
        if (maage_front_scroll_max(f) > 0) {
            r = front_rect(f, w / 2 + 124, 32, 6, h - 96);
            maage_client_ui_panel(ui, r, 0, 0, 0, 1);
            r = maage_front_scrollbar(f);
            maage_client_ui_panel(ui, r, 0.5f, 0.5f, 0.5f, 1);
            r.width -= 1;
            r.height -= 1;
            maage_client_ui_panel(ui, r, 0.75f, 0.75f, 0.75f, 1);
        }
        r = front_rect(f, 0, 32, w, 4);
        maage_client_ui_gradient(ui, r, 0xFF000000UL, 0x00000000UL);
        r = front_rect(f, 0, h - 68, w, 4);
        maage_client_ui_gradient(ui, r, 0x00000000UL, 0xFF000000UL);
        ui->clip = ui->content;
        if (!f->count) front_center(f, f->busy ? "Reading worlds..." : "No worlds yet. Create your first world!", 65, 0.8f, 0.8f, 0.8f);
        front_button(f, 0, "Play Selected World", !f->busy && f->selected < f->count && !maage_status_failed(f->entries[f->selected].status));
        front_button(f, 1, "Create New World", !f->busy);
        front_button(f, 2, "Rename", !f->busy && f->selected < f->count && !maage_status_failed(f->entries[f->selected].status));
        front_button(f, 3, "Delete", !f->busy && f->selected < f->count);
        front_button(f, 4, "Refresh", !f->busy);
        front_button(f, 5, "Cancel", 1);
    } else if (screen == MAAGE_CLIENT_UI_CREATE_WORLD || screen == MAAGE_CLIENT_UI_RENAME_WORLD) {
        front_center(f, screen == MAAGE_CLIENT_UI_CREATE_WORLD ? "Create New World" : "Rename World", 20, 1, 1, 1);
        if (screen == MAAGE_CLIENT_UI_RENAME_WORLD || !f->more) {
            maage_client_ui_text(ui, "World Name", ui->content.x + w / 2 - 100, ui->content.y + 47, MAAGE_CLIENT_UI_LEFT, 0.63f, 0.63f, 0.63f);
            front_field(f, f->name, 60, 3);
        }
        if (screen == MAAGE_CLIENT_UI_CREATE_WORLD) {
            if (!f->more) {
                front_button(f, 2, f->mode ? "Game Mode: Creative" : "Game Mode: Survival", !f->busy);
                front_center(f, f->mode ? "Unlimited resources, free flying and" : "Search for resources, craft and survive,", 137, 0.63f, 0.63f, 0.63f);
                front_center(f, f->mode ? "destroy blocks instantly" : "health and hunger", 149, 0.63f, 0.63f, 0.63f);
            } else {
                maage_client_ui_text(ui, "Seed for the World Generator", ui->content.x + w / 2 - 100, ui->content.y + 47, MAAGE_CLIENT_UI_LEFT, 0.63f, 0.63f, 0.63f);
                front_field(f, f->seed, 60, 4);
                front_center(f, front_seed_valid(f->seed) ? "Leave blank for a random seed" : "Enter a signed 64-bit integer seed", 85, 0.63f, 0.63f, 0.63f);
                front_button(f, 6, "Structures: On", 0);
                front_button(f, 7, "World Type: Default", 0);
                front_button(f, 8, "Allow Cheats: Unavailable", 0);
                front_button(f, 9, "Bonus Chest: Unavailable", 0);
            }
            front_button(f, 5, f->more ? "Done" : "More World Options...", !f->busy);
        }
        front_button(f, 0, screen == MAAGE_CLIENT_UI_CREATE_WORLD ? "Create New World" : "Save Name", !f->busy && front_name_valid(f->name) && (screen != MAAGE_CLIENT_UI_CREATE_WORLD || front_seed_valid(f->seed)));
        front_button(f, 1, "Cancel", !f->busy);
    } else {
        front_center(f, "Are you sure you want to delete this world?", 70, 1, 1, 1);
        if (f->selected < f->count) {
            sprintf(line, "\"%.127s\" will be lost forever!", f->entries[f->selected].name);
            front_center(f, line, 90, 1, 1, 1);
        }
        front_button(f, 0, "Delete", !f->busy);
        front_button(f, 1, "Cancel", !f->busy);
    }
    if (f->message[0]) front_center(f, f->message, 29, 1, 0.65f, 0.3f);
}
static void front_edit(char *text, size_t capacity, const maage_client_ui_input *input) {
    size_t length = strlen(text), i;
    for (i = 0; input->text && input->text[i]; ++i) {
        if (input->text[i] == '\b') {
            if (length) text[--length] = 0;
        } else if (length + 1 < capacity) {
            text[length++] = input->text[i];
            text[length] = 0;
        }
    }
}
static int front_focus_enabled(const maage_client_ui_frontend *f, unsigned int screen, unsigned int focus) {
    if (screen == MAAGE_CLIENT_UI_CREATE_WORLD) return focus < 2 || focus == 5 || (f->more ? focus == 4 : focus == 2 || focus == 3);
    return screen != MAAGE_CLIENT_UI_RENAME_WORLD || focus != 2;
}
static unsigned int front_input(maage_client_ui *ui, const maage_client_ui_input *input, void *context) {
    maage_client_ui_frontend *f = (maage_client_ui_frontend *) context;
    unsigned int screen = maage_client_ui_current(ui), n, count = screen == MAAGE_CLIENT_UI_WORLDS ? 6 : screen == MAAGE_CLIENT_UI_CREATE_WORLD ? 6
                                                                                                     : screen == MAAGE_CLIENT_UI_RENAME_WORLD   ? 4
                                                                                                                                                : 2;
    int hover = -1, text_submit = 0;
    float w = ui->content.width, list_width = w < 240 ? w - 16 : 220;
    size_t row;
    if (screen == MAAGE_CLIENT_UI_LOADING) return 0;
    if (input->escape) {
        if (!f->busy && screen != MAAGE_CLIENT_UI_TITLE) {
            if (screen == MAAGE_CLIENT_UI_CREATE_WORLD && f->more) {
                f->more = 0;
                f->focus = 5;
            } else
                ui->route_result = maage_client_ui_back(ui);
        }
        return 0;
    }
    if (ui->mouse_x != ui->previous_mouse_x || ui->mouse_y != ui->previous_mouse_y || input->primary_pressed) f->focus_keyboard = 0;
    if (input->previous || input->next) {
        do {
            f->focus = (f->focus + (input->previous ? count - 1 : 1)) % count;
        } while (!front_focus_enabled(f, screen, f->focus));
        f->focus_keyboard = 1;
    }
    for (n = 0; n < count; ++n) {
        if (!front_focus_enabled(f, screen, n) ||
            ((screen == MAAGE_CLIENT_UI_CREATE_WORLD || screen == MAAGE_CLIENT_UI_RENAME_WORLD) && n == 3) ||
            (n == 4 && screen == MAAGE_CLIENT_UI_CREATE_WORLD)) continue;
        if (screen == MAAGE_CLIENT_UI_RENAME_WORLD && n >= 2) continue;
        if (screen == MAAGE_CLIENT_UI_WORLDS || n < 3 || n == 5)
            if (front_hit(f, front_button_rect(f, n))) hover = (int) n;
    }
    if (screen == MAAGE_CLIENT_UI_CREATE_WORLD || screen == MAAGE_CLIENT_UI_RENAME_WORLD) {
        if (front_hit(f, front_rect(f, w / 2 - 100, 60, 200, 20))) hover = screen == MAAGE_CLIENT_UI_CREATE_WORLD && f->more ? 4 : 3;
    }
    if (hover >= 0 && input->primary_pressed) f->focus = (unsigned int) hover;
    if (screen == MAAGE_CLIENT_UI_CREATE_WORLD || screen == MAAGE_CLIENT_UI_RENAME_WORLD) {
        if (f->focus == 3) front_edit(f->name, sizeof(f->name), input);
        if (f->focus == 4) front_edit(f->seed, sizeof(f->seed), input);
    }
    if (screen == MAAGE_CLIENT_UI_WORLDS) {
        if ((input->list_up || input->left) && f->selected) --f->selected;
        if ((input->list_down || input->right) && f->selected + 1 < f->count) ++f->selected;
        if (input->list_up || input->list_down || input->left || input->right) {
            if ((float) f->selected * 36 < f->scroll) f->scroll = (float) f->selected * 36;
            if ((float) (f->selected + 1) * 36 > f->scroll + ui->content.height - 100) f->scroll = (float) (f->selected + 1) * 36 - ui->content.height + 100;
        }
        if (input->scroll) f->scroll -= (float) input->scroll * 18;
        if (maage_front_scroll_max(f) > 0) {
            maage_client_ui_rect bar = maage_front_scrollbar(f);
            if (input->primary_pressed && front_hit(f, bar)) {
                f->scrolling = 1;
                f->scroll_grab = ui->mouse_y - bar.y;
            }
            if (f->scrolling && input->primary_down) f->scroll = (ui->mouse_y - ui->content.y - 32 - f->scroll_grab) / (ui->content.height - 96 - bar.height) * maage_front_scroll_max(f);
        }
        if (!input->primary_down) f->scrolling = 0;
        if (f->scroll < 0) f->scroll = 0;
        if (f->scroll > maage_front_scroll_max(f)) f->scroll = maage_front_scroll_max(f);
        if (input->primary_pressed && front_hit(f, front_rect(f, (w - list_width) / 2, 32, list_width, ui->content.height - 96))) {
            float offset = ui->mouse_y - ui->content.y - 36 + f->scroll;
            row = offset < 0 ? f->count : (size_t) (offset / 36);
            if (row < f->count) {
                if (row == f->last_row && ui->notice_now - f->last_click < 0.3 && !f->busy && !maage_status_failed(f->entries[row].status)) {
                    f->selected = row;
                    f->play = 1;
                }
                f->selected = row;
                f->last_row = row;
                f->last_click = ui->notice_now;
            }
        }
    }
    if (!(input->accept || (input->primary_pressed && hover >= 0))) return 0;
    n = f->focus;
    if (f->busy) return 0;
    if (screen == MAAGE_CLIENT_UI_CREATE_WORLD || screen == MAAGE_CLIENT_UI_RENAME_WORLD) {
        if (input->primary_pressed && hover >= 3 && hover != 5) return 0;
        if (input->accept && (n == 3 || n == 4)) {
            n = 0;
            text_submit = 1;
        }
    }
    if (screen == MAAGE_CLIENT_UI_WORLDS) {
        if ((n == 0 || n == 2) && !(f->selected < f->count && !maage_status_failed(f->entries[f->selected].status))) return 0;
        if (n == 3 && f->selected >= f->count) return 0;
    }
    if ((screen == MAAGE_CLIENT_UI_CREATE_WORLD || screen == MAAGE_CLIENT_UI_RENAME_WORLD) && n == 0 && (!front_name_valid(f->name) || (screen == MAAGE_CLIENT_UI_CREATE_WORLD && !front_seed_valid(f->seed)))) return 0;
    if (!text_submit && ui->click) ui->click(ui->click_context);
    if (screen == MAAGE_CLIENT_UI_WORLDS) {
        if (n == 0) f->play = 1;
        if (n == 1) {
            strcpy(f->name, "New World");
            f->seed[0] = 0;
            f->mode = 0;
            f->more = 0;
            f->focus = 3;
            ui->route_result = maage_client_ui_open(ui, MAAGE_CLIENT_UI_CREATE_WORLD);
        }
        if (n == 2) {
            strcpy(f->name, f->entries[f->selected].name);
            f->focus = 3;
            ui->route_result = maage_client_ui_open(ui, MAAGE_CLIENT_UI_RENAME_WORLD);
        }
        if (n == 3) {
            f->focus = 0;
            ui->route_result = maage_client_ui_open(ui, MAAGE_CLIENT_UI_DELETE_WORLD);
        }
        if (n == 4) f->action = 1;
        if (n == 5) ui->route_result = maage_client_ui_back(ui);
    } else if (screen == MAAGE_CLIENT_UI_CREATE_WORLD) {
        if (n == 2) f->mode = !f->mode;
        if (n == 5) {
            f->more = !f->more;
            f->focus = f->more ? 4 : 5;
        }
        if (n == 0) f->play = 2;
        if (n == 1) ui->route_result = maage_client_ui_back(ui);
    } else if (screen == MAAGE_CLIENT_UI_RENAME_WORLD) {
        if (n == 0) f->action = 2;
        if (n == 1) ui->route_result = maage_client_ui_back(ui);
    } else {
        if (n == 0) f->action = 3;
        if (n == 1) ui->route_result = maage_client_ui_back(ui);
    }
    return 0;
}

maage_status maage_client_ui_front_register(maage_client_ui *ui, maage_client_ui_frontend **output) {
    maage_client_ui_frontend *f;
    maage_status result;
    unsigned int id;
    if (output) *output = NULL;
    if (!ui || !output) return maage_status_make(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_CONTEXT_GUI, MAAGE_OPERATION_REGISTER, 0);
    f = (maage_client_ui_frontend *) calloc(1, sizeof(*f));
    if (!f) return maage_status_make(MAAGE_STATUS_ALLOCATION_FAILED, MAAGE_CONTEXT_GUI, MAAGE_OPERATION_REGISTER, 0);
    f->ui = ui;
    f->last_row = (size_t) - 1;
    for (id = MAAGE_CLIENT_UI_TITLE; id <= MAAGE_CLIENT_UI_LOADING; ++id) {
        result = maage_client_ui_register_screen(ui, id,
                                                 id == MAAGE_CLIENT_UI_TITLE ? maage_client_ui_title_draw : front_draw,
                                                 id == MAAGE_CLIENT_UI_TITLE ? maage_client_ui_title_input : front_input, f);
        if (maage_status_failed(result)) {
            free(f);
            return result;
        }
    }
    *output = f;
    return result;
}
void maage_client_ui_front_destroy(maage_client_ui_frontend *f) {
    free(f);
}
void maage_client_ui_front_begin(maage_client_ui_frontend *f) {
    if (!f) return;
    f->quit = f->play = 0;
    f->action = f->focus = 0;
    f->busy = 0;
    f->focus_keyboard = 0;
    f->message[0] = 0;
}
void maage_client_ui_front_busy(maage_client_ui_frontend *f, int busy) {
    if (f) f->busy = busy;
}
void maage_client_ui_front_complete(maage_client_ui_frontend *f, const maage_world_summary *entries, size_t count, unsigned int operation) {
    if (!f) return;
    f->entries = entries;
    f->count = count;
    f->busy = 0;
    if (f->selected >= f->count) f->selected = f->count ? f->count - 1 : 0;
    f->scroll = 0;
    f->scrolling = 0;
    f->first = 0;
    f->last_row = (size_t) - 1;
    f->message[0] = 0;
    if (operation != 1) f->ui->route_result = maage_client_ui_back(f->ui);
}
void maage_client_ui_front_error(maage_client_ui_frontend *f, const char *message) {
    if (!f) return;
    f->busy = 0;
    if (!message) message = "";
    strncpy(f->message, message, sizeof(f->message) - 1);
    f->message[sizeof(f->message) - 1] = 0;
}
int maage_client_ui_front_take(maage_client_ui_frontend *f, maage_client_ui_front_request *r) {
    if (!f || !r) return 0;
    memset(r, 0, sizeof(*r));
    r->selected = f->selected;
    r->mode = f->mode;
    strcpy(r->name, f->name);
    strcpy(r->seed, f->seed);
    if (f->quit) {
        r->action = 6;
        f->quit = 0;
    } else if (f->play) {
        r->action = f->play == 2 ? 5 : 4;
        f->play = 0;
    } else if (f->action) {
        r->action = f->action;
        f->action = 0;
    }
    return r->action != 0;
}
