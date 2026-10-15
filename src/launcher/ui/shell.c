#include "widgets.h"
#include <stdlib.h>

maage_render_result maage_launcher_ui_create(maage_launcher_state *state, maage_launcher_ui **output)
{
    maage_launcher_ui *ui;
    maage_render_result result;
    maage_ui_theme theme = {
        {35,35,37,255}, {46,46,49,255}, {29,29,31,255}, {25,25,27,255},
        {239,239,241,255}, {163,163,170,255}, {184,25,124,255}, {174,35,56,255},
        {83,83,89,255}, {58,58,63,255}, 12, 20, 32
    };
    if (!output || !state) return MAAGE_RENDER_INVALID_ARGUMENT;
    *output = NULL;
    ui = calloc(1, sizeof(*ui));
    if (!ui) return MAAGE_RENDER_OUT_OF_MEMORY;
    result = maage_canvas_create(&ui->canvas);
    if (result != MAAGE_RENDER_OK) { free(ui); return result; }
    ui->app = state;
    ui->theme = theme;
    ui->last_page = ui->last_section = -1;
    *output = ui;
    return MAAGE_RENDER_OK;
}
void maage_launcher_ui_destroy(maage_launcher_ui *ui)
{
    if (!ui) return;
    maage_canvas_destroy(ui->canvas);
    free(ui);
}
static void maage_ui_navigation(maage_launcher_ui *ui, maage_rect safe, float *bottom)
{
    static const char *tabs[] = { "Play", "News", "Installations", "Settings" };
    const maage_launcher_account *account = ui->app->accounts.status == MAAGE_ACCOUNT_SIGNED_IN ? maage_launcher_selected_account(ui->app) : NULL;
    const char *label = "Sign in";
    maage_rect rect;
    maage_ui_layout row;
    float widths[4], total = 0, factor;
    int index, blocked = ui->blocked, compact = safe.width < 740;
    float header = compact ? 92 : 52;
    maage_ui_fill(ui, maage_ui_rect(safe.x, safe.y, safe.width, header), ui->theme.navigation);
    maage_ui_label(ui, maage_ui_rect(safe.x + 18, safe.y + 12, 94, 30), 22, ui->theme.text, "MaaGE");
    if (ui->app->accounts.status == MAAGE_ACCOUNT_AUTHENTICATING) label = "Signing in...";
    if (ui->app->accounts.status == MAAGE_ACCOUNT_SIGNED_IN) label = account && account->display_name ? account->display_name : "Accounts";
    ui->account_anchor = maage_ui_rect(safe.x + safe.width - 164, safe.y + 10, 148, ui->theme.control_height);
    ui->blocked = 0;
    if (maage_ui_button(ui, 7, ui->account_anchor, account && account->avatar ? "" : label, MAAGE_UI_NORMAL, 1)) ui->account_menu = !ui->account_menu;
    if (account && account->avatar) {
        maage_ui_image(ui, maage_ui_rect(ui->account_anchor.x + 6, ui->account_anchor.y + 4, 24, 24), account->avatar);
        maage_ui_label(ui, maage_ui_rect(ui->account_anchor.x + 38, ui->account_anchor.y + 7, 102, 24), 14, ui->theme.text, label);
    }
    ui->blocked = blocked;
    for (index = 0; index < 4; ++index) { widths[index] = maage_canvas_text_width(ui->canvas, 14, tabs[index]) + 28; total += widths[index]; }
    factor = compact ? safe.width / total : 1;
    row.rect = maage_ui_rect(compact ? safe.x : safe.x + 118, compact ? safe.y + 52 : safe.y, compact ? safe.width : total, compact ? 40 : 52);
    row.cursor = 0; row.gap = 0; row.horizontal = 1;
    for (index = 0; index < 4; ++index) {
        rect = maage_ui_take(&row, widths[index] * factor);
        if (maage_ui_button(ui, 1 + index, rect, tabs[index], MAAGE_UI_TAB, 1)) ui->app->navigation.page = (maage_launcher_page)index;
        if ((int)ui->app->navigation.page == index) maage_ui_fill(ui, maage_ui_rect(rect.x + 12, rect.y + rect.height - 2, rect.width - 24, 2), ui->theme.accent);
    }
    *bottom = safe.y + header;
}
maage_render_result maage_launcher_ui_frame(maage_launcher_ui *ui, maage_renderer *renderer,
    const maage_window_state *window, const maage_input_state *input)
{
    maage_render_result result;
    maage_rect body;
    float bottom, padding, zoom, width;
    int index, next = 0, menu_was_open;
    if (!ui || !renderer || !window || !input) return MAAGE_RENDER_INVALID_ARGUMENT;
    if (window->width <= 0 || window->height <= 0 || window->framebuffer_width <= 0 || window->framebuffer_height <= 0 || !window->visible)
        return MAAGE_RENDER_SKIPPED;
    zoom = ui->app->settings.values[MAAGE_SETTING_UI_SCALE].data.number;
    ui->scale = (float)window->framebuffer_width / window->width * zoom;
    result = maage_canvas_resize(ui->canvas, window->framebuffer_width, window->framebuffer_height, ui->scale);
    if (result != MAAGE_RENDER_OK) return result;
    ui->width = window->width / zoom; ui->height = window->height / zoom;
    ui->input = *input;
    ui->input.x /= zoom; ui->input.y /= zoom;
    if (input->cancelled) {
        ui->active = ui->focus = ui->account_menu = 0;
        ui->input.pressed = ui->input.released = 0;
        ui->input.key = MAAGE_KEY_NONE; ui->input.text[0] = '\0';
    }
    if (ui->input.key == MAAGE_KEY_ESCAPE) { ui->account_menu = 0; ui->focus = 7; ui->active = 0; }
    if (ui->input.key == MAAGE_KEY_TAB && ui->focus_count) {
        next = ui->input.shift ? ui->focus_count - 1 : 0;
        for (index = 0; index < ui->focus_count; ++index) if (ui->focus_ids[index] == ui->focus) {
            next = (index + (ui->input.shift ? ui->focus_count - 1 : 1)) % ui->focus_count;
            break;
        }
        ui->focus = ui->focus_ids[next];
    }
    ui->focus_count = 0; ui->clip_depth = 0;
    ui->clip = maage_ui_rect(0, 0, ui->width, ui->height);
    maage_canvas_clip(ui->canvas, ui->clip);
    maage_ui_fill(ui, ui->clip, ui->theme.background);
    ui->safe = maage_ui_rect((float)window->safe_area.left / zoom, (float)window->safe_area.top / zoom,
        ui->width - (float)(window->safe_area.left + window->safe_area.right) / zoom,
        ui->height - (float)(window->safe_area.top + window->safe_area.bottom) / zoom);
    maage_ui_clip_push(ui, ui->safe);
    menu_was_open = ui->account_menu;
    ui->blocked = menu_was_open;
    maage_ui_navigation(ui, ui->safe, &bottom);
    if (ui->last_page != (int)ui->app->navigation.page) {
        ui->last_page = (int)ui->app->navigation.page; ui->focus = ui->active = 0;
    }
    ui->blocked = menu_was_open || ui->account_menu;
    padding = ui->theme.padding;
    width = ui->safe.width - padding * 2;
    if (width > 960) width = 960;
    body = maage_ui_rect(ui->safe.x + (ui->safe.width - width) / 2, bottom + 24, width,
        ui->safe.y + ui->safe.height - bottom - (ui->app->notice ? 68 : 44));
    maage_ui_pages(ui, body);
    if (ui->app->notice) maage_ui_label(ui, maage_ui_rect(body.x, ui->safe.y + ui->safe.height - 30, body.width, 24), 13, ui->theme.text, ui->app->notice);
    if (ui->account_menu) {
        if (!menu_was_open) {
            ui->focus_count = 1; ui->focus_ids[0] = 7; ui->focus = 7; ui->active = 0;
            ui->input.pressed = ui->input.released = 0; ui->input.key = MAAGE_KEY_NONE;
        }
        maage_ui_account_menu(ui);
    }
    maage_ui_clip_pop(ui);
    if (ui->input.released) ui->active = 0;
    return maage_renderer_present_pixels(renderer, maage_canvas_pixels(ui->canvas));
}
