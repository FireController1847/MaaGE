#include "widgets.h"
#include <stdlib.h>
#include <string.h>

maage_render_result maage_launcher_ui_create(maage_launcher_ui **output)
{
    maage_launcher_ui *ui;
    maage_render_result result;
    maage_ui_theme theme = {
        {38,38,38,255}, {55,55,55,255}, {30,30,30,255}, {33,33,33,255},
        {245,245,245,255}, {167,167,167,255}, {194,18,131,255}, {198,17,54,255},
        {90,90,90,255}, {58,58,58,255}, 16, 24
    };
    int image, x, y, cell, offset;
    if (!output) return MAAGE_RENDER_INVALID_ARGUMENT;
    *output = NULL;
    ui = calloc(1, sizeof(*ui));
    if (!ui) return MAAGE_RENDER_OUT_OF_MEMORY;
    result = maage_canvas_create(&ui->canvas);
    if (result != MAAGE_RENDER_OK) { free(ui); return result; }
    ui->theme = theme;
    ui->memory = 4.5f; ui->ui_scale = 1;
    strcpy(ui->display_name, "Untitled profile");
    /* Original mosaic avatars; no game or account assets are distributed. */
    for (image = 0; image < 4; ++image) for (y = 0; y < 32; ++y) for (x = 0; x < 32; ++x) {
        cell = ((x / 6) * 13 + (y / 6) * 7 + image * 11) % 5;
        offset = (y * 32 + x) * 4;
        ui->art[image][offset] = (unsigned char)(45 + image * 32 + cell * 13);
        ui->art[image][offset + 1] = (unsigned char)(60 + cell * 22);
        ui->art[image][offset + 2] = (unsigned char)(100 + image * 18 + cell * 15);
        ui->art[image][offset + 3] = 255;
    }
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
    static const char *tabs[] = { "Play", "News", "Profiles", "Accounts", "Settings", "About" };
    maage_rect rect;
    maage_ui_layout row;
    float header = safe.width >= 920 ? 64 : 112;
    float tab_width = safe.width >= 920 ? 91 : safe.width / 6;
    int index, old_page = ui->page;
    maage_ui_fill(ui, maage_ui_rect(safe.x, safe.y, safe.width, header), ui->theme.navigation);
    maage_ui_label(ui, maage_ui_rect(safe.x + 22, safe.y + 16, 125, 36), 25, ui->theme.text, "MaaGE");
    rect = maage_ui_rect(safe.x + safe.width - 150, safe.y + 14, 134, 36);
    if (maage_ui_button(ui, 7, rect, ui->empty_accounts ? "Add account" : "Sample account", MAAGE_UI_NORMAL, 1)) ui->page = 3;
    row.rect = maage_ui_rect(safe.width >= 920 ? safe.x + 166 : safe.x,
        safe.width >= 920 ? safe.y : safe.y + 64, tab_width * 6, safe.width >= 920 ? 64 : 48);
    row.cursor = 0; row.gap = 0; row.horizontal = 1;
    for (index = 0; index < 6; ++index) {
        rect = maage_ui_take(&row, tab_width);
        if (maage_ui_button(ui, 1 + index, rect, tabs[index], MAAGE_UI_TAB, 1)) ui->page = index;
        if (ui->page == index) maage_ui_fill(ui, maage_ui_rect(rect.x + 12, rect.y + rect.height - 3, rect.width - 24, 3), ui->theme.accent);
    }
    if (old_page != ui->page) { ui->focus = 0; ui->active = 0; }
    *bottom = safe.y + header;
}
maage_render_result maage_launcher_ui_frame(maage_launcher_ui *ui, maage_renderer *renderer,
    const maage_window_state *window, const maage_input_state *input)
{
    maage_render_result result;
    maage_rect safe, body;
    float bottom, padding, zoom;
    int index, next = 0;
    if (!ui || !renderer || !window || !input) return MAAGE_RENDER_INVALID_ARGUMENT;
    if (window->width <= 0 || window->height <= 0 || window->framebuffer_width <= 0 || window->framebuffer_height <= 0 || !window->visible)
        return MAAGE_RENDER_SKIPPED;
    zoom = ui->ui_scale;
    ui->scale = (float)window->framebuffer_width / window->width * zoom;
    result = maage_canvas_resize(ui->canvas, window->framebuffer_width, window->framebuffer_height, ui->scale);
    if (result != MAAGE_RENDER_OK) return result;
    ui->width = window->width / zoom; ui->height = window->height / zoom;
    ui->input = *input;
    ui->input.x /= zoom; ui->input.y /= zoom;
    if (input->cancelled) { ui->active = 0; ui->focus = 0; }
    if (input->key == MAAGE_KEY_ESCAPE) { ui->dialog = 0; ui->focus = 0; ui->active = 0; }
    if (input->key == MAAGE_KEY_TAB && ui->focus_count) {
        for (index = 0; index < ui->focus_count; ++index) if (ui->focus_ids[index] == ui->focus) {
            next = (index + (input->shift ? ui->focus_count - 1 : 1)) % ui->focus_count;
            break;
        }
        ui->focus = ui->focus_ids[next];
    }
    ui->focus_count = 0; ui->clip_depth = 0;
    ui->clip = maage_ui_rect(0, 0, ui->width, ui->height);
    maage_canvas_clip(ui->canvas, ui->clip);
    maage_ui_fill(ui, ui->clip, ui->theme.background);
    safe = maage_ui_rect((float)window->safe_area.left / zoom, (float)window->safe_area.top / zoom,
        ui->width - (float)(window->safe_area.left + window->safe_area.right) / zoom,
        ui->height - (float)(window->safe_area.top + window->safe_area.bottom) / zoom);
    maage_ui_clip_push(ui, safe);
    ui->blocked = ui->dialog != 0;
    maage_ui_navigation(ui, safe, &bottom);
    padding = safe.width > 800 ? 36 : ui->theme.padding;
    body = maage_ui_rect(safe.x + padding, bottom + 28, safe.width - padding * 2, safe.y + safe.height - bottom - 66);
    maage_ui_pages(ui, body);
    maage_ui_label(ui, maage_ui_rect(safe.x + padding, safe.y + safe.height - 26, safe.width - padding * 2, 22), 12, ui->theme.muted, "Interface preview / local example data");
    if (ui->dialog) {
        if (!ui->blocked) { ui->focus_count = 0; ui->focus = 0; ui->active = 0; ui->input.released = 0; }
        maage_ui_dialog(ui);
    }
    maage_ui_clip_pop(ui);
    if (ui->input.released) ui->active = 0;
    return maage_renderer_present_pixels(renderer, maage_canvas_pixels(ui->canvas));
}
