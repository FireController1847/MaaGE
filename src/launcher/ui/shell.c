#include "widgets.h"
#include <stdlib.h>

maage_render_result maage_launcher_ui_create(maage_launcher_state *state, maage_launcher_ui **output)
{
    maage_launcher_ui *ui;
    maage_render_result result;
    maage_ui_theme theme = {
        {28,28,31,255}, {34,34,38,255}, {39,39,44,255}, {30,30,34,255},
        {242,242,245,255}, {175,175,185,255}, {199,28,137,255}, {184,43,66,255},
        {55,55,62,255}, {61,61,69,255}, {56,39,53,255},
        16, 24, 40, 16, 14, 30, 22, 16
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
    static const char *tabs[] = { "Play", "Installations", "News", "Settings" };
    static const maage_launcher_page pages[] = { MAAGE_PAGE_PLAY, MAAGE_PAGE_INSTALLATIONS, MAAGE_PAGE_NEWS, MAAGE_PAGE_SETTINGS };
    const maage_launcher_account *account = maage_launcher_selected_account(ui->app);
    const char *label = "Sign in";
    maage_rect rect;
    maage_ui_layout row;
    float widths[4], total = 0, factor, points[6];
    int index, blocked = ui->blocked, compact = safe.width < 840;
    float header = compact ? 112 : 64;
    maage_ui_fill(ui, maage_ui_rect(safe.x, safe.y, safe.width, header), ui->theme.navigation);
    maage_ui_fill(ui, maage_ui_rect(safe.x, safe.y + header - 1, safe.width, 1), ui->theme.line);
    maage_ui_label(ui, maage_ui_rect(safe.x + 24, safe.y + 16, 112, 34), 26, ui->theme.text, "MaaGE");
    if (ui->app->accounts.status == MAAGE_ACCOUNT_AUTHENTICATING) label = "Signing in...";
    if (account && account->authenticated) label = account->display_name ? account->display_name : "Accounts";
    ui->account_anchor = maage_ui_rect(safe.x + safe.width - 208, safe.y + 12, 184, ui->theme.control_height);
    ui->blocked = ui->app->accounts.sign_in.active;
    if (maage_ui_tab(ui, 7, ui->account_anchor, "", ui->account_menu)) ui->account_menu = !ui->account_menu;
    maage_ui_outline(ui, ui->account_anchor, ui->theme.line);
    if (account && account->avatar) maage_ui_image(ui, maage_ui_rect(ui->account_anchor.x + 8, ui->account_anchor.y + 6, 28, 28), account->avatar);
    rect = maage_ui_rect(ui->account_anchor.x + (account && account->avatar ? 48 : 16), ui->account_anchor.y + 10,
        account && account->avatar ? 108 : 140, 26);
    maage_ui_label(ui, rect, ui->theme.button_size, ui->theme.text, label);
    points[0] = ui->account_anchor.x + 162; points[1] = ui->account_anchor.y + 18;
    points[2] = points[0] + 8; points[3] = points[1]; points[4] = points[0] + 4; points[5] = points[1] + 4;
    maage_canvas_triangle(ui->canvas, points, ui->theme.muted);
    ui->blocked = blocked;
    for (index = 0; index < 4; ++index) { widths[index] = maage_canvas_text_width(ui->canvas, ui->theme.button_size, tabs[index]) + 36; total += widths[index]; }
    factor = compact ? (safe.width - 48) / total : 1;
    row.rect = maage_ui_rect(compact ? safe.x + 24 : safe.x + 144, compact ? safe.y + 64 : safe.y + 10,
        compact ? safe.width - 48 : total, 44);
    row.cursor = 0; row.gap = 0; row.horizontal = 1;
    for (index = 0; index < 4; ++index) {
        rect = maage_ui_take(&row, widths[index] * factor);
        if (maage_ui_tab(ui, 1 + index, rect, tabs[index], ui->app->navigation.page == pages[index])) ui->app->navigation.page = pages[index];
    }
    *bottom = safe.y + header;
}
maage_render_result maage_launcher_ui_frame(maage_launcher_ui *ui, maage_renderer *renderer,
    const maage_window_state *window, const maage_input_state *input)
{
    maage_render_result result;
    maage_rect body;
    float bottom, padding, zoom, width;
    int index, next = 0, menu_was_open, dialog;
    if (!ui || !renderer || !window || !input) return MAAGE_RENDER_INVALID_ARGUMENT;
    if (window->width <= 0 || window->height <= 0 || window->framebuffer_width <= 0 || window->framebuffer_height <= 0 || !window->visible)
        return MAAGE_RENDER_SKIPPED;
    dialog = ui->app->accounts.sign_in.active;
    zoom = ui->app->settings.values[MAAGE_SETTING_UI_SCALE].data.number;
    ui->scale = (float)window->framebuffer_width / window->width * zoom;
    result = maage_canvas_resize(ui->canvas, window->framebuffer_width, window->framebuffer_height, ui->scale);
    if (result != MAAGE_RENDER_OK) return result;
    ui->width = window->width / zoom; ui->height = window->height / zoom;
    ui->input = *input;
    if (input->pressed) ui->keyboard_focus = 0;
    if (input->key != MAAGE_KEY_NONE || input->text[0]) ui->keyboard_focus = 1;
    ui->input.x /= zoom; ui->input.y /= zoom;
    if (input->cancelled) {
        ui->active = ui->focus = ui->account_menu = ui->keyboard_focus = 0;
        ui->input.pressed = ui->input.released = 0;
        ui->input.key = MAAGE_KEY_NONE; ui->input.text[0] = '\0';
    }
    if (ui->input.key == MAAGE_KEY_ESCAPE) {
        if (dialog) maage_launcher_request(ui->app, MAAGE_ACTION_CANCEL_SIGN_IN, NULL);
        else { ui->account_menu = 0; ui->focus = 7; }
        ui->active = 0; ui->input.key = MAAGE_KEY_NONE;
    }
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
    ui->blocked = dialog || menu_was_open;
    maage_ui_navigation(ui, ui->safe, &bottom);
    if (ui->last_page != (int)ui->app->navigation.page) {
        ui->last_page = (int)ui->app->navigation.page; ui->active = 0;
        if (ui->focus < 1 || ui->focus > 7) ui->focus = 0;
    }
    ui->blocked = dialog || menu_was_open || ui->account_menu;
    padding = ui->theme.padding;
    width = ui->safe.width - padding * 2;
    if (width > 1120) width = 1120;
    body = maage_ui_rect(ui->safe.x + (ui->safe.width - width) / 2, bottom + padding, width,
        ui->safe.y + ui->safe.height - bottom - (ui->app->notice ? padding * 2 + 32 : padding * 2));
    maage_ui_pages(ui, body);
    if (ui->app->notice) maage_ui_label(ui, maage_ui_rect(body.x, ui->safe.y + ui->safe.height - 30, body.width, 26), ui->theme.small_size, ui->theme.text, ui->app->notice);
    if (ui->account_menu && !dialog) {
        if (!menu_was_open) {
            ui->focus_count = 1; ui->focus_ids[0] = 7; ui->focus = 7; ui->active = 0;
            ui->input.pressed = ui->input.released = 0; ui->input.key = MAAGE_KEY_NONE;
        }
        maage_ui_account_menu(ui);
    }
    if (dialog) {
        if (!ui->sign_in_dialog) {
            ui->account_menu = 0; ui->active = 0; ui->focus = 9103; ui->scroll[7] = 0;
            ui->input.pressed = ui->input.released = 0; ui->input.key = MAAGE_KEY_NONE;
        }
        ui->focus_count = 0;
        maage_ui_sign_in_dialog(ui);
    } else if (ui->sign_in_dialog) { ui->focus = 7; ui->active = 0; }
    ui->sign_in_dialog = dialog;
    maage_ui_clip_pop(ui);
    if (ui->input.released) ui->active = 0;
    return maage_renderer_present_pixels(renderer, maage_canvas_pixels(ui->canvas));
}
