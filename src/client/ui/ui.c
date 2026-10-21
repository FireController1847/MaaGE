#include "internal.h"
#include "maage/log.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

static maage_client_ui_rect maage_client_ui_menu_rectangle(const maage_client_ui *ui,
    unsigned int index, maage_client_ui_screen_id screen)
{
    maage_client_ui_rect rectangle;
    rectangle.width = ui->content.width > 224 ? 200 : ui->content.width - 24;
    if (rectangle.width < 20) rectangle.width = 20;
    rectangle.height = 20;
    rectangle.x = (float)(int)(ui->content.x + (ui->content.width - rectangle.width) * 0.5f);
    rectangle.y = (float)(int)(ui->content.y + ui->content.height * 0.5f) - 18 + (float)index * 24;
    if (screen == MAAGE_CLIENT_UI_OPTIONS)
        rectangle.y = (float)(int)(ui->content.y + ui->content.height * 0.5f) - 54 + (float)index * 24;
    return rectangle;
}

static int maage_client_ui_contains(maage_client_ui_rect rectangle, float x, float y)
{
    return x >= rectangle.x && x < rectangle.x + rectangle.width &&
        y >= rectangle.y && y < rectangle.y + rectangle.height;
}


/* One shared integer slider: 0..26 selects 10..260, 26 means Unlimited.
 * Dragging tracks held mouse state rather than repeated press events. */
static void maage_client_ui_slider_move(maage_client_ui *ui, maage_client_ui_rect track)
{
    float ratio = (ui->mouse_x - track.x) / track.width;
    int position = (int)(ratio * 26.0f + 0.5f);
    if (position < 0) position = 0;
    if (position > 26) position = 26;
    ui->fps_step = position;
}
static void maage_client_ui_slider_draw(maage_client_ui *ui, maage_client_ui_rect rect)
{
    maage_client_ui_rect bar, handle;
    char text[64];
    if (ui->fps_step == 26) strcpy(text, "Max FPS: Unlimited");
    else sprintf(text, "Max FPS: %d", (ui->fps_step + 1) * 10);
    maage_client_ui_button(ui, rect, text, ui->selected == 0);
    bar = rect; bar.x += 12; bar.width -= 24; bar.y += 15; bar.height = 2;
    maage_client_ui_panel(ui, bar, 0.12f, 0.12f, 0.12f, 1);
    handle = bar; handle.x += bar.width * ui->fps_step / 26.0f - 3;
    handle.width = 6; handle.y -= 2; handle.height = 6;
    maage_client_ui_panel(ui, handle, 0.9f, 0.9f, 0.9f, 1);
}
static unsigned int maage_client_ui_menu_input(maage_client_ui *ui,
    const maage_client_ui_input *input, void *context)
{
    maage_client_ui_screen_id screen = maage_client_ui_current(ui);
    maage_client_ui_rect rectangle;
    unsigned int count = screen == MAAGE_CLIENT_UI_PAUSE ? 3 : (ui->game ? 6 : 5), i;
    int hover = -1, activate;
    (void)context;
    for (i = 0; i < count; ++i) {
        rectangle = maage_client_ui_menu_rectangle(ui, i, screen);
        if (maage_client_ui_contains(rectangle, ui->mouse_x, ui->mouse_y)) hover = (int)i;
    }
    if (input->previous) ui->selected = (ui->selected + count - 1) % count;
    if (input->next) ui->selected = (ui->selected + 1) % count;
    if (hover >= 0 && (input->primary_pressed || ui->mouse_x != ui->previous_mouse_x ||
        ui->mouse_y != ui->previous_mouse_y)) ui->selected = (unsigned int)hover;
    if (screen == MAAGE_CLIENT_UI_OPTIONS) {
        unsigned int game_index = ui->game ? 4 : 99;
        unsigned int back_index = ui->game ? 5 : 4;
        if (input->primary_pressed && hover == 0) ui->dragging_fps = 1;
        if (!input->primary_down) ui->dragging_fps = 0;
        if (ui->dragging_fps && input->primary_down) {
            rectangle = maage_client_ui_menu_rectangle(ui, 0, screen);
            rectangle.x += 12; rectangle.width -= 24;
            maage_client_ui_slider_move(ui, rectangle);
        }
        if (ui->selected == 0) {
            if (input->left && ui->fps_step > 0) --ui->fps_step;
            if (input->right && ui->fps_step < 26) ++ui->fps_step;
        }
        if (ui->selected == 2) {
            if (input->left) ui->requested_scale = (ui->requested_scale + 4) % 5;
            if (input->right) ui->requested_scale = (ui->requested_scale + 1) % 5;
        }
        if (ui->selected == 3) {
            if (input->left) ui->leaves_mode = (ui->leaves_mode + 2) % 3;
            if (input->right) ui->leaves_mode = (ui->leaves_mode + 1) % 3;
        }
        activate = input->accept || (input->primary_pressed && hover >= 0);
        if (!activate) return 0;
        if (ui->selected == 0) return 0;
        if (ui->selected == 1) {
            if (ui->vsync_supported) {
                maage_status status = maage_renderer_set_vsync(ui->renderer, !ui->vsync);
                if (maage_status_code(status) == MAAGE_STATUS_SUCCESS) ui->vsync = !ui->vsync;
                else { ui->vsync_supported = 0; ui->route_result = status; }
            }
        } else if (ui->selected == 2) ui->requested_scale = (ui->requested_scale + 1) % 5;
        else if (ui->selected == 3) ui->leaves_mode = (ui->leaves_mode + 1) % 3;
        else if (ui->selected == game_index)
            ui->route_result = maage_gameplay_set_mode(ui->game, ui->game->mode == MAAGE_GAME_MODE_CREATIVE ?
                MAAGE_GAME_MODE_SURVIVAL : MAAGE_GAME_MODE_CREATIVE);
        else if (ui->selected == back_index) ui->route_result = maage_client_ui_back(ui);
        return 0;
    }
    activate = input->accept || (input->primary_pressed && hover >= 0);
    if (!activate) return 0;
    if (ui->selected == 0) {
        if (ui->game && !ui->game->health) return MAAGE_CLIENT_UI_RESPAWN;
        ui->route_result = maage_client_ui_back(ui);
    } else if (ui->selected == 1) ui->route_result = maage_client_ui_open(ui, MAAGE_CLIENT_UI_OPTIONS);
    else return MAAGE_CLIENT_UI_EXIT;
    return 0;
}
static void maage_client_ui_menu_draw(maage_client_ui *ui, void *context)
{
    static const char *const labels[3] = {"Resume Game", "Options...", "Exit Client"};
    static const char *const scales[5] = {"Auto", "1x", "2x", "3x", "4x"};
    static const char *const leaves[3] = {"Fast", "Smart", "Fancy"};
    maage_client_ui_screen_id screen = maage_client_ui_current(ui);
    maage_client_ui_rect rectangle;
    unsigned int i;
    float center = (float)(int)(ui->content.x + ui->content.width * 0.5f);
    float middle = (float)(int)(ui->content.y + ui->content.height * 0.5f);
    char text[64];
    (void)context;
    maage_client_ui_text(ui, screen == MAAGE_CLIENT_UI_PAUSE ? "Game Menu" : "Options",
        center, middle - (screen == MAAGE_CLIENT_UI_PAUSE ? 54 : 88),
        MAAGE_CLIENT_UI_CENTER, 1, 1, 1);
    if (screen == MAAGE_CLIENT_UI_PAUSE) {
        for (i = 0; i < 3; ++i) {
            rectangle = maage_client_ui_menu_rectangle(ui, i, screen);
            maage_client_ui_button(ui, rectangle, i == 0 && ui->game && !ui->game->health ?
                "Respawn" : labels[i], ui->selected == i);
        }
    } else {
        rectangle = maage_client_ui_menu_rectangle(ui, 0, screen);
        maage_client_ui_slider_draw(ui, rectangle);
        rectangle = maage_client_ui_menu_rectangle(ui, 1, screen);
        maage_client_ui_button(ui, rectangle, !ui->vsync_supported ? "VSync: Unavailable" :
            (ui->vsync ? "VSync: On" : "VSync: Off"), ui->selected == 1);
        rectangle = maage_client_ui_menu_rectangle(ui, 2, screen);
        sprintf(text, "GUI Scale: %s", scales[ui->requested_scale]);
        maage_client_ui_button(ui, rectangle, text, ui->selected == 2);
        rectangle = maage_client_ui_menu_rectangle(ui, 3, screen);
        sprintf(text, "Leaves: %s", leaves[ui->leaves_mode]);
        maage_client_ui_button(ui, rectangle, text, ui->selected == 3);
        if (ui->game) {
            rectangle = maage_client_ui_menu_rectangle(ui, 4, screen);
            maage_client_ui_button(ui, rectangle, ui->game->mode == MAAGE_GAME_MODE_CREATIVE ?
                "Game Mode: Creative" : "Game Mode: Survival", ui->selected == 4);
        }
        rectangle = maage_client_ui_menu_rectangle(ui, ui->game ? 5 : 4, screen);
        maage_client_ui_button(ui, rectangle, "Back", ui->selected == (ui->game ? 5U : 4U));
    }
}

maage_status maage_client_ui_create(maage_renderer *renderer,
    const char *application_data, const maage_pixel_buffer *block_atlas,
    maage_client_ui **output, maage_status_trace *trace)
{
    maage_client_ui *ui;
    maage_status result;
    if (output) *output = NULL;
    if (!renderer || !output) return maage_status_make(MAAGE_STATUS_INVALID_ARGUMENT,
        MAAGE_CONTEXT_GUI, MAAGE_OPERATION_INITIALIZE, renderer ? 3 : 0);
    ui = (maage_client_ui *)calloc(1, sizeof(*ui));
    if (!ui) return maage_status_make(MAAGE_STATUS_ALLOCATION_FAILED,
        MAAGE_CONTEXT_GUI, MAAGE_OPERATION_INITIALIZE, 0);
    ui->width = 320; ui->height = 240; ui->gui_scale = 1;
    ui->renderer = renderer; ui->fps_step = 26; ui->vsync = 1; ui->leaves_mode = MAAGE_LEAVES_FANCY;
    ui->vsync_supported = maage_status_code(maage_renderer_set_vsync(renderer, 1)) == MAAGE_STATUS_SUCCESS;
    ui->content.width = 320; ui->content.height = 240;
    ui->mouse_x = ui->mouse_y = ui->previous_mouse_x = ui->previous_mouse_y = -1;
    result = maage_client_ui_resources_load(&ui->resources, renderer, application_data, block_atlas, trace);
    if (maage_status_failed(result)) {
        result = maage_status_failed(ui->resources.recovery_status) ? ui->resources.recovery_status :
            maage_status_make(MAAGE_STATUS_ALLOCATION_FAILED, MAAGE_CONTEXT_GUI, MAAGE_OPERATION_ATLAS, 0);
        maage_client_ui_destroy(ui);
        return result;
    }
    result = maage_client_ui_register_screen(ui, MAAGE_CLIENT_UI_PAUSE,
        maage_client_ui_menu_draw, maage_client_ui_menu_input, NULL);
    if (!maage_status_failed(result)) result = maage_client_ui_register_screen(ui,
        MAAGE_CLIENT_UI_OPTIONS, maage_client_ui_menu_draw, maage_client_ui_menu_input, NULL);
    if (!maage_status_failed(result)) result = maage_client_ui_inventory_register(ui);
    if (maage_status_failed(result)) {
        maage_client_ui_destroy(ui);
        return MAAGE_STATUS_TRACE(trace, result, MAAGE_CONTEXT_GUI, MAAGE_OPERATION_INITIALIZE);
    }
    ui->batch.vertices = ui->vertices; ui->batch.indices = ui->indices;
    ui->batch.texture = ui->resources.texture;
    *output = ui;
    return maage_status_make(ui->resources.failed_resources ? MAAGE_STATUS_PARTIAL : MAAGE_STATUS_SUCCESS, MAAGE_CONTEXT_GUI, MAAGE_OPERATION_INITIALIZE, 0);
}

maage_status maage_client_ui_resource_status(const maage_client_ui *ui)
{
    return ui ? ui->resources.load_status : maage_status_make(MAAGE_STATUS_INVALID_ARGUMENT,
        MAAGE_CONTEXT_GUI, MAAGE_OPERATION_INITIALIZE, 0);
}

void maage_client_ui_notify(maage_client_ui *ui, maage_status result, double now)
{
    if (!ui || maage_status_is_success(result) || ui->notice_result == result) return;
    ui->notice_result = result;
    ui->notice_until = now + 6.0;
    sprintf(ui->notice_text, "Operation 0x%08lX", (unsigned long)result);
}

void maage_client_ui_notice_time(maage_client_ui *ui, double now)
{
    if (ui) ui->notice_now = now;
}

maage_status maage_client_ui_destroy(maage_client_ui *ui)
{
    if (!ui) return maage_status_make(MAAGE_STATUS_NOOP, 0, MAAGE_OPERATION_DESTROY, 0);
    maage_render_texture_destroy(ui->resources.texture);
    free(ui->debug_cache.vertices);
    free(ui->vertices); free(ui->indices);
    free(ui->navigation.screens); free(ui->navigation.stack); free(ui);
    return maage_status_make(MAAGE_STATUS_SUCCESS, 0, MAAGE_OPERATION_DESTROY, 0);
}

maage_status maage_client_ui_update(maage_client_ui *ui, const maage_client_ui_input *input,
    int framebuffer_width, int framebuffer_height, int window_width, int window_height, unsigned int *actions)
{
    maage_client_ui_screen *screen;
    maage_client_ui_screen_id id;
    unsigned int action = 0;
    int scale, vertical;
    float x_scale, y_scale, right, bottom;
    if (actions) *actions = 0;
    if (!ui || !input || !actions) return maage_status_make(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_CONTEXT_GUI, MAAGE_OPERATION_UPDATE, !ui ? 0 : (!input ? 1 : 6));
    if (!ui->navigation.finalized) return maage_status_make(MAAGE_STATUS_INVALID_STATE, MAAGE_CONTEXT_GUI, MAAGE_OPERATION_UPDATE, 0);
    if (framebuffer_width <= 0 || framebuffer_height <= 0 || window_width <= 0 || window_height <= 0)
        return maage_status_make(MAAGE_STATUS_NOOP, 0, MAAGE_OPERATION_UPDATE, 0);
    ui->route_result = maage_status_make(MAAGE_STATUS_SUCCESS, 0, MAAGE_OPERATION_UPDATE, 0);
    scale = framebuffer_width / 320; vertical = framebuffer_height / 240;
    if (scale > vertical) scale = vertical;
    if (scale < 1) scale = 1;
    if (ui->requested_scale && scale > ui->requested_scale) scale = ui->requested_scale;
    ui->gui_scale = scale;
    ui->width = (float)framebuffer_width / scale;
    ui->height = (float)framebuffer_height / scale;
    x_scale = (float)framebuffer_width / (float)window_width / scale;
    y_scale = (float)framebuffer_height / (float)window_height / scale;
    ui->content.x = ui->safe_area.left > 0 ? (float)ui->safe_area.left * x_scale : 0;
    ui->content.y = ui->safe_area.top > 0 ? (float)ui->safe_area.top * y_scale : 0;
    right = ui->safe_area.right > 0 ? (float)ui->safe_area.right * x_scale : 0;
    bottom = ui->safe_area.bottom > 0 ? (float)ui->safe_area.bottom * y_scale : 0;
    if (ui->content.x > ui->width) ui->content.x = ui->width;
    if (ui->content.y > ui->height) ui->content.y = ui->height;
    ui->content.width = ui->width - ui->content.x - right;
    ui->content.height = ui->height - ui->content.y - bottom;
    if (ui->content.width < 0) ui->content.width = 0;
    if (ui->content.height < 0) ui->content.height = 0;
    ui->previous_mouse_x = ui->mouse_x; ui->previous_mouse_y = ui->mouse_y;
    ui->mouse_x = input->mouse_x * (float)framebuffer_width / (float)window_width / scale;
    ui->mouse_y = input->mouse_y * (float)framebuffer_height / (float)window_height / scale;
    if (input->f3) ui->debug = !ui->debug;
    if (input->escape) { ui->dragging_fps = 0;
        return maage_client_ui_modal(ui) ? maage_client_ui_back(ui) : maage_client_ui_open(ui, MAAGE_CLIENT_UI_PAUSE);
    }
    id = maage_client_ui_current(ui);
    if (input->inventory && ui->game) {
        if (id == MAAGE_CLIENT_UI_INVENTORY) return maage_client_ui_back(ui);
        if (!id) return maage_client_ui_open(ui, MAAGE_CLIENT_UI_INVENTORY);
        return maage_status_make(MAAGE_STATUS_NOOP, 0, MAAGE_OPERATION_UPDATE, 0);
    }
    if (id) {
        screen = &ui->navigation.screens[id];
        if (screen->input) action = screen->input(ui, input, screen->context);
    }
    *actions = action;
    return ui->route_result;
}

int maage_client_ui_debug_visible(const maage_client_ui *ui)
{
    return ui && ui->debug;
}

maage_status maage_client_ui_set_safe_area(maage_client_ui *ui, const maage_window_insets *insets)
{
    if (!ui) return maage_status_make(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_CONTEXT_GUI, MAAGE_OPERATION_SET, 0);
    if (insets) ui->safe_area = *insets;
    else memset(&ui->safe_area, 0, sizeof(ui->safe_area));
    return maage_status_make(MAAGE_STATUS_SUCCESS, 0, MAAGE_OPERATION_SET, 0);
}

int maage_client_ui_max_fps(const maage_client_ui *ui) { return ui && ui->fps_step != 26 ? (ui->fps_step + 1) * 10 : 0; }
int maage_client_ui_vsync(const maage_client_ui *ui) { return ui ? ui->vsync : 1; }
int maage_client_ui_gui_scale(const maage_client_ui *ui) { return ui ? ui->requested_scale : 0; }
maage_leaves_mode maage_client_ui_leaves_mode(const maage_client_ui *ui)
{ return ui ? (maage_leaves_mode)ui->leaves_mode : MAAGE_LEAVES_FANCY; }
float maage_client_ui_width(const maage_client_ui *ui) { return ui ? ui->width : 0; }
float maage_client_ui_height(const maage_client_ui *ui) { return ui ? ui->height : 0; }

static void maage_client_ui_compact_line(const maage_client_ui *ui, const char *source,
    float maximum_width, char output[MAAGE_CLIENT_UI_DEBUG_CHARACTERS])
{
    unsigned int count = 0, character;
    float width = 0, advance, dots;
    while (count + 4 < MAAGE_CLIENT_UI_DEBUG_CHARACTERS && source[count]) {
        character = (unsigned char)source[count];
        if (character < 32 || character > 126) character = '?';
        advance = ui->resources.glyph_width[character];
        if (width + advance > maximum_width) break;
        output[count++] = (char)character; width += advance;
    }
    if (source[count]) {
        dots = (float)ui->resources.glyph_width['.'] * 3;
        while (count && width + dots > maximum_width)
            width -= ui->resources.glyph_width[(unsigned char)output[--count]];
        if (width + dots <= maximum_width) {
            output[count++] = '.'; output[count++] = '.'; output[count++] = '.';
        }
    }
    output[count] = 0;
}

static void maage_client_ui_debug_column(maage_client_ui *ui,
    const char lines[MAAGE_CLIENT_UI_DEBUG_LINES][MAAGE_CLIENT_UI_DEBUG_CHARACTERS],
    unsigned int count, int right)
{
    char text[MAAGE_CLIENT_UI_DEBUG_CHARACTERS];
    maage_client_ui_rect background;
    unsigned int i;
    float x, y, maximum_width, text_width;
    maximum_width = (float)(int)(ui->content.width * 0.5f) - 6;
    if (maximum_width <= 0) return;
    ui->clip.x = ui->content.x + (right ? (float)(int)(ui->content.width * 0.5f) + 2 : 0);
    ui->clip.y = ui->content.y;
    ui->clip.width = maximum_width + 4; ui->clip.height = ui->content.height;
    if (count > MAAGE_CLIENT_UI_DEBUG_LINES) count = MAAGE_CLIENT_UI_DEBUG_LINES;
    for (i = 0; i < count; ++i) {
        y = ui->content.y + 2 + (float)i * 9;
        if (y + 8 > ui->content.y + ui->content.height) break;
        maage_client_ui_compact_line(ui, lines[i], maximum_width, text);
        text_width = maage_client_ui_text_width(ui, text);
        if (!text_width) continue;
        x = right ? ui->content.x + ui->content.width - 2 - text_width : ui->content.x + 2;
        background.x = x - 1; background.y = y - 1;
        background.width = text_width + 2; background.height = 9;
        maage_client_ui_panel(ui, background, 0, 0, 0, 0.56f);
        maage_client_ui_text(ui, text, x, y, MAAGE_CLIENT_UI_LEFT, 0.88f, 0.88f, 0.88f);
    }
}

static void maage_client_ui_debug_right(maage_client_ui *ui,
    const maage_client_ui_debug *debug)
{
    maage_client_ui_debug_cache *cache = &ui->debug_cache;
    maage_render_ui_vertex *vertices;
    size_t first, quads;
    unsigned int i, count = debug->right_count;
    int same;
    if (count > MAAGE_CLIENT_UI_DEBUG_LINES) count = MAAGE_CLIENT_UI_DEBUG_LINES;
    same = cache->valid && !cache->disabled && cache->count == count &&
        cache->width == ui->width && cache->height == ui->height &&
        cache->scale == ui->gui_scale && cache->texture == ui->resources.texture &&
        cache->atlas_edge == ui->resources.atlas_edge &&
        cache->content.x == ui->content.x && cache->content.y == ui->content.y &&
        cache->content.width == ui->content.width && cache->content.height == ui->content.height &&
        !memcmp(cache->glyph_width, ui->resources.glyph_width, sizeof(cache->glyph_width));
    for (i = 0; same && i < count; ++i)
        same = !strncmp(cache->lines[i], debug->right[i], MAAGE_CLIENT_UI_DEBUG_CHARACTERS);
    if (same) {
        maage_client_ui_append_quads(ui, cache->vertices, cache->quads);
        return;
    }
    first = ui->batch.vertex_count;
    maage_client_ui_debug_column(ui, debug->right, count, 1);
    cache->valid = 0;
    if (maage_status_failed(ui->draw_result) || cache->disabled) return;
    quads = (ui->batch.vertex_count - first) / 4;
    /* At most one background and one quad per bounded character per line. */
    if (quads > MAAGE_CLIENT_UI_DEBUG_LINES * (MAAGE_CLIENT_UI_DEBUG_CHARACTERS + 1) ||
        quads > (size_t)-1 / sizeof(*vertices) / 4) return;
    if (quads > cache->capacity) {
        vertices = (maage_render_ui_vertex *)realloc(cache->vertices, quads * 4 * sizeof(*vertices));
        if (!vertices) {
            cache->disabled = 1;
            maage_log_status(MAAGE_LOG_WARNING, "maage.client.ui",
                maage_status_make(MAAGE_STATUS_ALLOCATION_FAILED, MAAGE_CONTEXT_GUI, MAAGE_OPERATION_INITIALIZE, 0),
                "Optional F3 geometry cache unavailable; retaining immediate drawing for this session.");
            return;
        }
        cache->vertices = vertices; cache->capacity = quads;
    }
    if (quads) memcpy(cache->vertices, ui->vertices + first, quads * 4 * sizeof(*vertices));
    for (i = 0; i < count; ++i)
        memcpy(cache->lines[i], debug->right[i], MAAGE_CLIENT_UI_DEBUG_CHARACTERS);
    memcpy(cache->glyph_width, ui->resources.glyph_width, sizeof(cache->glyph_width));
    cache->quads = quads; cache->count = count; cache->content = ui->content;
    cache->width = ui->width; cache->height = ui->height; cache->scale = ui->gui_scale;
    cache->texture = ui->resources.texture; cache->atlas_edge = ui->resources.atlas_edge;
    cache->valid = 1;
}

maage_status maage_client_ui_draw(maage_client_ui *ui,
    const maage_client_ui_debug *debug, const maage_render_ui_data **output)
{
    maage_client_ui_rect rectangle;
    maage_client_ui_screen_id id;
    maage_client_ui_screen *screen;
    if (output) *output = NULL;
    if (!ui || !output) return maage_status_make(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_CONTEXT_GUI, MAAGE_OPERATION_PRESENT, !ui ? 0 : 2);
    if (!ui->navigation.finalized) return maage_status_make(MAAGE_STATUS_INVALID_STATE, MAAGE_CONTEXT_GUI, MAAGE_OPERATION_PRESENT, 0);
    ui->batch.vertex_count = ui->batch.index_count = 0;
    ui->draw_result = maage_status_make(MAAGE_STATUS_SUCCESS, 0, MAAGE_OPERATION_PRESENT, 0);
    ui->batch.logical_width = ui->width; ui->batch.logical_height = ui->height;
    ui->clip.x = ui->clip.y = 0; ui->clip.width = ui->width; ui->clip.height = ui->height;
    maage_client_ui_gameplay_draw(ui);
    if (!maage_client_ui_modal(ui)) {
        rectangle.x = ui->width * 0.5f - 7.5f;
        rectangle.y = ui->height * 0.5f - 7.5f;
        rectangle.width = rectangle.height = 15;
        maage_client_ui_rectangle(ui, rectangle, 0, 0,
            15.0f / ui->resources.atlas_edge, 15.0f / ui->resources.atlas_edge, 1, 1, 1, 1);
    }
    if (ui->debug && debug) {
        maage_client_ui_debug_column(ui, debug->left, debug->left_count, 0);
        maage_client_ui_debug_right(ui, debug);
    }
    ui->clip.x = ui->clip.y = 0; ui->clip.width = ui->width; ui->clip.height = ui->height;
    id = maage_client_ui_current(ui);
    if (id) {
        rectangle = ui->clip;
        maage_client_ui_panel(ui, rectangle, 0.03f, 0.03f, 0.04f, 0.68f);
        ui->clip = ui->content;
        screen = &ui->navigation.screens[id];
        screen->draw(ui, screen->context);
    }
    if (ui->notice_until > ui->notice_now) {
        ui->clip = ui->content;
        rectangle.width = ui->content.width < 164 ? ui->content.width : 164;
        rectangle.height = 24;
        rectangle.x = ui->content.x + ui->content.width - rectangle.width;
        rectangle.y = ui->content.y + ui->content.height - 60;
        if (rectangle.y < ui->content.y) rectangle.y = ui->content.y;
        maage_client_ui_panel(ui, rectangle, 0.06f, 0.06f, 0.06f, 0.86f);
        maage_client_ui_text(ui, ui->notice_text, rectangle.x + 4, rectangle.y + 3,
            MAAGE_CLIENT_UI_LEFT, 1, 0.8f, 0.4f);
        maage_client_ui_text(ui, "See logs/client.log", rectangle.x + 4, rectangle.y + 13,
            MAAGE_CLIENT_UI_LEFT, 0.88f, 0.88f, 0.88f);
    }
    if (maage_status_failed(ui->draw_result)) return ui->draw_result;
    *output = &ui->batch;
    return maage_status_make(MAAGE_STATUS_SUCCESS, 0, MAAGE_OPERATION_PRESENT, 0);
}
