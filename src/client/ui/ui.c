#include "internal.h"
#include "maage/log.h"
#include "maage/time.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

static maage_client_ui_rect maage_client_ui_menu_rectangle(const maage_client_ui *ui,
    unsigned int index, maage_client_ui_screen_id screen)
{
    maage_client_ui_rect rect;
    rect.width = ui->content.width > 224 ? 200 : ui->content.width - 24;
    if (rect.width < 20) rect.width = 20;
    rect.height = 20;
    rect.x = (float)(int)(ui->content.x + (ui->content.width - rect.width) * 0.5f);
    rect.y = (float)(int)(ui->content.y + ui->content.height * 0.5f) - 18 + (float)index * 24;
    if (screen == MAAGE_CLIENT_UI_OPTIONS) {
        unsigned int row = index < 7 ? index : index - 7;
        rect.width = (ui->content.width - 12) * 0.5f;
        if (rect.width < 20) rect.width = 20;
        rect.x = ui->content.x + 4 + (index >= 7 ? rect.width + 4 : 0);
        rect.y = (float)(int)(ui->content.y + (ui->content.height - 168) * 0.5f) + (float)row * 24;
    }
    return rect;
}

static int maage_client_ui_contains(maage_client_ui_rect rectangle, float x, float y)
{
    return x >= rectangle.x && x < rectangle.x + rectangle.width &&
        y >= rectangle.y && y < rectangle.y + rectangle.height;
}
static int maage_client_ui_highlight(const maage_client_ui *ui,
    maage_client_ui_rect rectangle,unsigned int index)
{
    return maage_client_ui_contains(rectangle,ui->mouse_x,ui->mouse_y) ||
        (ui->selected_keyboard && ui->selected == index);
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
    maage_client_ui_button(ui, rect, text, maage_client_ui_highlight(ui,rect,0));
    bar = rect; bar.x += 12; bar.width -= 24; bar.y += 15; bar.height = 2;
    maage_client_ui_panel(ui, bar, 0.12f, 0.12f, 0.12f, 1);
    handle = bar; handle.x += bar.width * ui->fps_step / 26.0f - 3;
    handle.width = 6; handle.y -= 2; handle.height = 6;
    maage_client_ui_panel(ui, handle, 0.9f, 0.9f, 0.9f, 1);
}
/* Fullscreen requests a native AppKit Space, not CoreGraphics exclusive capture. */
static maage_window_mode maage_client_ui_platform_mode(maage_client_window_choice choice)
{
    if (choice == MAAGE_CLIENT_WINDOWED) return MAAGE_WINDOW_WINDOWED;
    if (choice == MAAGE_CLIENT_FULLSCREEN) return MAAGE_WINDOW_NATIVE_FULLSCREEN;
    return MAAGE_WINDOW_BORDERLESS_FULLSCREEN;
}
static void maage_client_ui_request_window_mode(maage_client_ui *ui, maage_client_window_choice choice)
{
    maage_status result;
    if (ui->mode_pending || ui->mode_changing) return;
    result = maage_window_set_mode(ui->window, maage_client_ui_platform_mode(choice));
    if (maage_status_code(result) == MAAGE_STATUS_SUCCESS) {
        ui->window_mode = choice;
        if (choice) ui->last_nonwindowed_mode = choice;
        ui->mode_pending = 1;
        ui->mode_requested_at = maage_time_monotonic_seconds();
    } else ui->route_result = result;
}
static void maage_client_ui_change_volume(maage_client_ui *ui, unsigned int bus,
    maage_client_ui_rect rectangle)
{
    float ratio;
    int percent;
    if (bus >= 7) return;
    ratio = (ui->mouse_x - rectangle.x - 12) / (rectangle.width - 24);
    percent = (int)(ratio * 100.0f + 0.5f);
    if (percent < 0) percent = 0;
    if (percent > 100) percent = 100;
    ui->audio_volume[bus] = percent;
}
static unsigned int maage_client_ui_menu_input(maage_client_ui *ui,
    const maage_client_ui_input *input, void *context)
{
    maage_client_ui_screen_id screen = maage_client_ui_current(ui);
    maage_client_ui_rect rectangle;
    unsigned int count = screen == MAAGE_CLIENT_UI_PAUSE ? 3U : 14U, n;
    int hover = -1, activate;
    (void)context;
    for (n = 0; n < count; ++n) {
        rectangle = maage_client_ui_menu_rectangle(ui, n, screen);
        if (maage_client_ui_contains(rectangle, ui->mouse_x, ui->mouse_y)) hover = (int)n;
    }
    if (ui->mouse_x != ui->previous_mouse_x ||
        ui->mouse_y != ui->previous_mouse_y || input->primary_pressed)
        ui->selected_keyboard = 0;
    if (input->previous) {
        ui->selected = (ui->selected + count - 1) % count;
        ui->selected_keyboard = 1;
    }
    if (input->next) {
        ui->selected = (ui->selected + 1) % count;
        ui->selected_keyboard = 1;
    }
    if (hover >= 0 && (input->primary_pressed || ui->mouse_x != ui->previous_mouse_x ||
        ui->mouse_y != ui->previous_mouse_y)) ui->selected = (unsigned int)hover;
    if (screen == MAAGE_CLIENT_UI_OPTIONS) {
        if (!ui->has_snapshot && ui->selected == 5U) {
            if (hover == 5 && input->primary_pressed) return 0;
            ui->selected = input->previous ? 4U : 6U;
        }
        if ((input->primary_pressed && hover == 0) ||
            (ui->dragging_fps && input->primary_down)) {
            rectangle = maage_client_ui_menu_rectangle(ui, 0, screen);
            rectangle.x += 12; rectangle.width -= 24;
            maage_client_ui_slider_move(ui, rectangle);
        }
        if (input->primary_pressed && hover == 0) ui->dragging_fps = 1;
        if (input->primary_pressed && hover >= 7 && hover < 14)
            ui->dragging_volume = hover - 6;
        if (ui->dragging_volume && input->primary_down) {
            n = (unsigned int)ui->dragging_volume - 1U;
            rectangle = maage_client_ui_menu_rectangle(ui, n + 7U, screen);
            maage_client_ui_change_volume(ui, n, rectangle);
        }
        if (!input->primary_down) {
            ui->dragging_fps = 0;
            ui->dragging_volume = 0;
        }
        if (ui->selected == 0) {
            if (input->left && ui->fps_step > 0) --ui->fps_step;
            if (input->right && ui->fps_step < 26) ++ui->fps_step;
        } else if (ui->selected == 2) {
            if (input->left) ui->requested_scale = (ui->requested_scale + 4) % 5;
            if (input->right) ui->requested_scale = (ui->requested_scale + 1) % 5;
        } else if (ui->selected == 3) {
            if (input->left) ui->leaves_mode = (ui->leaves_mode + 2) % 3;
            if (input->right) ui->leaves_mode = (ui->leaves_mode + 1) % 3;
        } else if (ui->selected >= 7U) {
            int *volume = &ui->audio_volume[ui->selected - 7U];
            if (input->left) *volume = *volume < 5 ? 0 : *volume - 5;
            if (input->right) *volume = *volume > 95 ? 100 : *volume + 5;
        }
        activate = input->accept || (input->primary_pressed && hover >= 0);
        if (!activate) return 0;
        if (ui->selected == 0) return 0;
        if (ui->selected == 1) {
            if (ui->vsync_supported) {
                maage_status status = maage_renderer_set_vsync(ui->renderer, !ui->vsync);
                if (maage_status_code(status) == MAAGE_STATUS_SUCCESS) ui->vsync = !ui->vsync;
                else {
                    /* Temporary resize/Space-transition errors must not
                     * permanently disable the VSync control. */
                    unsigned int code = maage_status_code(status);
                    if (code == MAAGE_STATUS_NOT_IMPLEMENTED ||
                        code == MAAGE_STATUS_SYSTEM_NOT_SUPPORTED)
                        ui->vsync_supported = 0;
                    ui->route_result = status;
                }
            }
        } else if (ui->selected == 2) ui->requested_scale = (ui->requested_scale + 1) % 5;
        else if (ui->selected == 3) ui->leaves_mode = (ui->leaves_mode + 1) % 3;
        else if (ui->selected == 4) maage_client_ui_request_window_mode(ui,
            (maage_client_window_choice)((ui->window_mode + 1) % 3));
        else if (ui->selected == 5 && ui->has_snapshot)
            maage_client_ui_queue_game_mode(ui);
        else if (ui->selected == 6) ui->route_result = maage_client_ui_back(ui);
        else if (ui->selected >= 7U && input->accept) {
            int *volume = &ui->audio_volume[ui->selected - 7U];
            *volume = *volume > 95 ? 0 : *volume + 5;
        }
        return 0;
    }
    activate = input->accept || (input->primary_pressed && hover >= 0);
    if (!activate) return 0;
    if (ui->selected == 0) {
        if (ui->has_snapshot && !ui->snapshot.player.health) {
            maage_client_ui_queue_respawn(ui);
            return 0;
        }
        ui->route_result = maage_client_ui_back(ui);
    } else if (ui->selected == 1)
        ui->route_result = maage_client_ui_open(ui, MAAGE_CLIENT_UI_OPTIONS);
    else return MAAGE_CLIENT_UI_EXIT;
    return 0;
}
static void maage_client_ui_menu_draw(maage_client_ui *ui, void *context)
{
    static const char *const labels[3] = {"Resume Game", "Options...", "Save and Quit to Title"};
    static const char *const scales[5] = {"Auto", "1x", "2x", "3x", "4x"};
    static const char *const leaves[3] = {"Fast", "Smart", "Fancy"};
    static const char *const modes[3] = {"Windowed", "Borderless", "Fullscreen"};
    static const char *const buses[7] = {"Master", "Blocks", "Player", "Ambient",
        "Weather", "Music", "UI"};
    maage_client_ui_screen_id screen = maage_client_ui_current(ui);
    maage_client_ui_rect rect, bar, handle;
    unsigned int n;
    float center = (float)(int)(ui->content.x + ui->content.width * 0.5f);
    float middle = (float)(int)(ui->content.y + ui->content.height * 0.5f);
    char label[64];
    (void)context;
    maage_client_ui_text(ui, screen == MAAGE_CLIENT_UI_PAUSE ? "Game Menu" : "Options",
        center, middle - (screen == MAAGE_CLIENT_UI_PAUSE ? 54 : 101),
        MAAGE_CLIENT_UI_CENTER, 1, 1, 1);
    if (screen == MAAGE_CLIENT_UI_PAUSE) {
        for (n = 0; n < 3; ++n) {
            rect = maage_client_ui_menu_rectangle(ui, n, screen);
            maage_client_ui_button(ui, rect, n == 0 && ui->has_snapshot &&
                !ui->snapshot.player.health ? "Respawn" : labels[n],
                maage_client_ui_highlight(ui,rect,n));
        }
        return;
    }
    rect = maage_client_ui_menu_rectangle(ui, 0, screen);
    maage_client_ui_slider_draw(ui, rect);
    rect = maage_client_ui_menu_rectangle(ui, 1, screen);
    maage_client_ui_button(ui, rect, !ui->vsync_supported ? "VSync: Unavailable" :
        (ui->vsync ? "VSync: On" : "VSync: Off"), maage_client_ui_highlight(ui,rect,1));
    rect = maage_client_ui_menu_rectangle(ui, 2, screen);
    sprintf(label, "GUI Scale: %s", scales[ui->requested_scale]);
    maage_client_ui_button(ui, rect, label, maage_client_ui_highlight(ui,rect,2));
    rect = maage_client_ui_menu_rectangle(ui, 3, screen);
    sprintf(label, "Leaves: %s", leaves[ui->leaves_mode]);
    maage_client_ui_button(ui, rect, label, maage_client_ui_highlight(ui,rect,3));
    rect = maage_client_ui_menu_rectangle(ui, 4, screen);
    sprintf(label, "Window: %s", ui->mode_pending || ui->mode_changing ?
        "Switching..." : modes[ui->window_mode]);
    maage_client_ui_button(ui, rect, label, maage_client_ui_highlight(ui,rect,4));
    rect = maage_client_ui_menu_rectangle(ui, 5, screen);
    maage_client_ui_button(ui, rect, !ui->has_snapshot ? "Game: N/A" :
        (ui->snapshot.player.game_mode == MAAGE_GAME_MODE_CREATIVE ?
        "Game: Creative" : "Game: Survival"), maage_client_ui_highlight(ui,rect,5));
    rect = maage_client_ui_menu_rectangle(ui, 6, screen);
    maage_client_ui_button(ui, rect, "Back", maage_client_ui_highlight(ui,rect,6));
    for (n = 0; n < 7U; ++n) {
        rect = maage_client_ui_menu_rectangle(ui, n + 7U, screen);
        sprintf(label, "%s: %d%%", buses[n], ui->audio_volume[n]);
        maage_client_ui_button(ui, rect, label, maage_client_ui_highlight(ui,rect,n+7U));
        bar = rect; bar.x += 12; bar.width -= 24; bar.y += 15; bar.height = 2;
        maage_client_ui_panel(ui, bar, 0.12f, 0.12f, 0.12f, 1);
        handle = bar; handle.x += bar.width * ui->audio_volume[n] / 100.0f - 3;
        handle.width = 6; handle.y -= 2; handle.height = 6;
        maage_client_ui_panel(ui, handle, 0.9f, 0.9f, 0.9f, 1);
    }
}

maage_status maage_client_ui_create(maage_renderer *renderer, maage_window *window,
    const char *application_data, const maage_pixel_buffer *block_atlas,
    maage_client_ui **output, maage_status_trace *trace)
{
    maage_client_ui *ui;
    maage_status result;
    if (output) *output = NULL;
    if (!renderer || !window || !output) return maage_status_make(MAAGE_STATUS_INVALID_ARGUMENT,
        MAAGE_CONTEXT_GUI, MAAGE_OPERATION_INITIALIZE, renderer ? 3 : 0);
    ui = (maage_client_ui *)calloc(1, sizeof(*ui));
    if (!ui) return maage_status_make(MAAGE_STATUS_ALLOCATION_FAILED,
        MAAGE_CONTEXT_GUI, MAAGE_OPERATION_INITIALIZE, 0);
    ui->width = 320; ui->height = 240; ui->gui_scale = 1;
    ui->renderer = renderer; ui->window = window;
    ui->last_nonwindowed_mode = ui->confirmed_fullscreen_style = MAAGE_CLIENT_BORDERLESS;
    ui->fps_step = 26; ui->vsync = 1; ui->leaves_mode = MAAGE_LEAVES_FANCY;
    ui->audio_volume[0] = MAAGE_CLIENT_AUDIO_DEFAULT_MASTER;
    ui->audio_volume[1] = MAAGE_CLIENT_AUDIO_DEFAULT_BLOCKS;
    ui->audio_volume[2] = MAAGE_CLIENT_AUDIO_DEFAULT_PLAYER;
    ui->audio_volume[3] = MAAGE_CLIENT_AUDIO_DEFAULT_AMBIENT;
    ui->audio_volume[4] = MAAGE_CLIENT_AUDIO_DEFAULT_WEATHER;
    ui->audio_volume[5] = MAAGE_CLIENT_AUDIO_DEFAULT_MUSIC;
    ui->audio_volume[6] = MAAGE_CLIENT_AUDIO_DEFAULT_UI;
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
    maage_registry_pages_destroy(&ui->navigation.screens, free);
    free(ui->navigation.stack); free(ui);
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
    if (maage_client_ui_current(ui) != MAAGE_CLIENT_UI_TITLE &&
        maage_client_ui_current(ui) && ui->click &&
        (input->primary_pressed || input->accept)) ui->click(ui->click_context);
    if (input->f11) maage_client_ui_request_window_mode(ui,
        (maage_client_window_choice)(ui->window_mode ? MAAGE_CLIENT_WINDOWED : ui->last_nonwindowed_mode));
    if (input->escape && maage_client_ui_current(ui) < MAAGE_CLIENT_UI_TITLE) { ui->dragging_fps = 0; ui->dragging_volume = 0;
        return maage_client_ui_modal(ui) ? maage_client_ui_back(ui) : maage_client_ui_open(ui, MAAGE_CLIENT_UI_PAUSE);
    }
    id = maage_client_ui_current(ui);
    if (input->inventory && ui->has_snapshot) {
        if (id == MAAGE_CLIENT_UI_INVENTORY) return maage_client_ui_back(ui);
        if (!id) return maage_client_ui_open(ui, MAAGE_CLIENT_UI_INVENTORY);
        return maage_status_make(MAAGE_STATUS_NOOP, 0, MAAGE_OPERATION_UPDATE, 0);
    }
    if (id) {
        screen = maage_client_ui_screen_get(ui, id);
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

maage_status maage_client_ui_set_window_state(maage_client_ui *ui,
    const maage_window_state *state)
{
    double now;
    if (!ui || !state) return maage_status_make(MAAGE_STATUS_INVALID_ARGUMENT,
        MAAGE_CONTEXT_GUI, MAAGE_OPERATION_SET, 0);
    now = maage_time_monotonic_seconds();
    ui->mode_changing = state->mode_transitioning;
    if (ui->mode_pending && now >= 0 && ui->mode_requested_at >= 0 &&
        now - ui->mode_requested_at >= 12.0) {
        ui->mode_pending = ui->mode_changing = 0;
        ui->window_mode = ui->confirmed_window_mode;
        ui->last_nonwindowed_mode = ui->confirmed_fullscreen_style;
        ui->route_result = maage_status_make(MAAGE_STATUS_TIMEOUT,
            MAAGE_CONTEXT_WINDOW, MAAGE_OPERATION_SET, 0);
        maage_client_ui_notify(ui, ui->route_result, now);
        return ui->route_result;
    }
    if (ui->mode_changing) return maage_status_make(MAAGE_STATUS_SUCCESS, 0, MAAGE_OPERATION_SET, 0);
    if (ui->mode_pending) {
        if (state->mode == maage_client_ui_platform_mode(
            (maage_client_window_choice)ui->window_mode)) {
            ui->mode_pending = 0;
            ui->confirmed_window_mode = ui->window_mode;
            ui->confirmed_fullscreen_style = ui->last_nonwindowed_mode;
        } else if (now >= 0 && (ui->mode_requested_at < 0 ||
            now - ui->mode_requested_at >= 4.0)) {
            ui->mode_pending = 0;
            ui->window_mode = ui->confirmed_window_mode;
            ui->last_nonwindowed_mode = ui->confirmed_fullscreen_style;
            ui->route_result = maage_status_make(MAAGE_STATUS_TIMEOUT,
                MAAGE_CONTEXT_WINDOW, MAAGE_OPERATION_SET, 0);
            maage_client_ui_notify(ui, ui->route_result, now);
            return ui->route_result;
        }
    } else {
        if (state->mode == MAAGE_WINDOW_WINDOWED) ui->window_mode = MAAGE_CLIENT_WINDOWED;
        else if (state->mode == MAAGE_WINDOW_NATIVE_FULLSCREEN)
            ui->window_mode = MAAGE_CLIENT_FULLSCREEN;
        else if (state->mode == MAAGE_WINDOW_BORDERLESS_FULLSCREEN)
            ui->window_mode = MAAGE_CLIENT_BORDERLESS;
        else return maage_status_make(MAAGE_STATUS_NOOP, 0, MAAGE_OPERATION_SET, 0);
        if (ui->window_mode) ui->last_nonwindowed_mode = ui->window_mode;
        ui->confirmed_window_mode = ui->window_mode;
        ui->confirmed_fullscreen_style = ui->last_nonwindowed_mode;
    }
    return maage_status_make(MAAGE_STATUS_SUCCESS, 0, MAAGE_OPERATION_SET, 0);
}

maage_status maage_client_ui_apply_options(maage_client_ui *ui, const maage_client_options *options)
{
    maage_status result;
    if (!ui || !options) return maage_status_make(MAAGE_STATUS_INVALID_ARGUMENT,
        MAAGE_CONTEXT_GUI, MAAGE_OPERATION_SET, 0);
    ui->fps_step = options->max_fps ? options->max_fps / 10 - 1 : 26;
    ui->requested_scale = options->gui_scale;
    ui->leaves_mode = options->leaves;
    { unsigned int n; for (n = 0; n < 7; ++n) {
        ui->audio_volume[n] = options->audio_volume[n];
        if (ui->audio_volume[n] < 0) ui->audio_volume[n] = 0;
        if (ui->audio_volume[n] > 100) ui->audio_volume[n] = 100;
    } }
    if (ui->vsync_supported && ui->vsync != options->vsync) {
        result = maage_renderer_set_vsync(ui->renderer, options->vsync);
        if (maage_status_code(result) != MAAGE_STATUS_SUCCESS) {
            if (maage_status_code(result) == MAAGE_STATUS_NOT_IMPLEMENTED ||
                maage_status_code(result) == MAAGE_STATUS_SYSTEM_NOT_SUPPORTED)
                ui->vsync_supported = 0;
            ui->route_result = result;
        } else ui->vsync = options->vsync;
    }
    ui->last_nonwindowed_mode = options->fullscreen_style;
    ui->confirmed_fullscreen_style = options->fullscreen_style;
    if (options->window_mode != MAAGE_CLIENT_WINDOWED)
        maage_client_ui_request_window_mode(ui, options->window_mode);
    return ui->route_result;
}

void maage_client_ui_get_options(const maage_client_ui *ui, maage_client_options *options)
{
    if (!ui || !options) return;
    options->max_fps = ui->fps_step == 26 ? 0 : (ui->fps_step + 1) * 10;
    options->vsync = ui->vsync;
    options->gui_scale = ui->requested_scale;
    options->leaves = (maage_leaves_mode)ui->leaves_mode;
    { unsigned int n; for (n = 0; n < 7; ++n)
        options->audio_volume[n] = ui->audio_volume[n]; }
    /* Pending native transitions are never included in persistent snapshots. */
    options->window_mode = (maage_client_window_choice)ui->confirmed_window_mode;
    options->fullscreen_style = (maage_client_window_choice)ui->confirmed_fullscreen_style;
}

int maage_client_ui_options_visible(const maage_client_ui *ui)
{ return maage_client_ui_current(ui) == MAAGE_CLIENT_UI_OPTIONS; }
int maage_client_ui_window_mode_changing(const maage_client_ui *ui)
{ return ui && (ui->mode_pending || ui->mode_changing); }

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
    if (ui->has_snapshot) maage_client_ui_gameplay_draw(ui);
    if (ui->has_snapshot && !maage_client_ui_modal(ui)) {
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
        if(id!=MAAGE_CLIENT_UI_TITLE && id!=MAAGE_CLIENT_UI_LOADING)
            maage_client_ui_panel(ui, rectangle, 0.03f, 0.03f, 0.04f, 0.68f);
        ui->clip = ui->content;
        screen = maage_client_ui_screen_get(ui, id);
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

/* Local presentation-only click intent, independent of Server authority. */
void maage_client_ui_set_click_callback(maage_client_ui *ui,
    maage_client_ui_click_callback callback, void *context)
{
    if (!ui) return;
    ui->click = callback;
    ui->click_context = context;
}

maage_status maage_client_ui_open_root(maage_client_ui *ui,maage_client_ui_screen_id id)
{
    if(!ui)return maage_status_make(MAAGE_STATUS_INVALID_ARGUMENT,MAAGE_CONTEXT_GUI,MAAGE_OPERATION_NAVIGATE,0);
    ui->navigation.depth=0;ui->selected=0;
    if(id==MAAGE_CLIENT_UI_TITLE) {
        ui->has_snapshot=0;
        ui->command_read=ui->command_write=ui->command_count=0;
    }
    return maage_client_ui_open(ui,id);
}
maage_renderer *maage_client_ui_renderer(const maage_client_ui *ui)
{ return ui?ui->renderer:NULL; }
maage_window *maage_client_ui_window(const maage_client_ui *ui)
{ return ui?ui->window:NULL; }
