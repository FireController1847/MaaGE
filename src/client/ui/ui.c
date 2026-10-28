#include "internal.h"
#include "maage/log.h"
#include "maage/time.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

static void maage_client_ui_cursor(maage_client_ui *ui, int pointer)
{
    maage_status result;
    if (ui->cursor_unavailable) return;
    result = maage_window_set_cursor(ui->window,
        pointer ? MAAGE_WINDOW_CURSOR_POINTER : MAAGE_WINDOW_CURSOR_ARROW);
    if (maage_status_failed(result)) {
        ui->cursor_unavailable = 1;
        maage_log_status(MAAGE_LOG_WARNING, "maage.client.ui", result,
            "Native cursor shapes unavailable; retaining the platform cursor.");
    }
}

/* Pause retains the translucent world overlay; Settings has its own
 * Vanilla 1.7.10-inspired numeric screens in options.c. */
static maage_client_ui_rect pause_rect(const maage_client_ui *ui,unsigned int n)
{
    maage_client_ui_rect r;
    r.width=ui->content.width>224.0f?200.0f:ui->content.width-24.0f;
    if(r.width<20.0f)r.width=20.0f;
    r.height=20.0f;
    r.x=(float)(int)(ui->content.x+(ui->content.width-r.width)*0.5f);
    r.y=(float)(int)(ui->content.y+ui->content.height*0.5f)-18.0f+n*24.0f;
    return r;
}
static int pause_hit(const maage_client_ui *ui,maage_client_ui_rect r)
{
    return ui->mouse_x>=r.x&&ui->mouse_x<r.x+r.width&&
        ui->mouse_y>=r.y&&ui->mouse_y<r.y+r.height;
}
/* The same Client-to-platform mapping is used for requests and for
 * confirming their completion. Keep it typed so strict C90 never falls back
 * to an implicit int declaration and mismatched enum comparisons. */
static maage_window_mode maage_client_ui_platform_mode(maage_client_window_choice choice)
{
    if(choice==MAAGE_CLIENT_WINDOWED)return MAAGE_WINDOW_WINDOWED;
    if(choice==MAAGE_CLIENT_FULLSCREEN)return MAAGE_WINDOW_NATIVE_FULLSCREEN;
    return MAAGE_WINDOW_BORDERLESS_FULLSCREEN;
}
void maage_client_ui_request_window_mode(maage_client_ui *ui,maage_client_window_choice choice)
{
    maage_status result;
    if(!ui||ui->mode_pending||ui->mode_changing)return;
    result=maage_window_set_mode(ui->window,maage_client_ui_platform_mode(choice));
    if(maage_status_code(result)==MAAGE_STATUS_SUCCESS) {
        ui->window_mode=choice;
        if(choice)ui->last_nonwindowed_mode=choice;
        ui->mode_pending=1;
        ui->mode_requested_at=maage_time_monotonic_seconds();
    } else ui->route_result=result;
}
static unsigned int pause_input(maage_client_ui *ui,const maage_client_ui_input *input,void *context)
{
    int hover=-1,activate;
    unsigned int n,selected;
    (void)context;
    for(n=0;n<3U;++n)if(pause_hit(ui,pause_rect(ui,n)))hover=(int)n;
    if(ui->mouse_x!=ui->previous_mouse_x||ui->mouse_y!=ui->previous_mouse_y||
        input->primary_pressed)ui->selected_keyboard=0;
    if(input->previous){ui->selected=(ui->selected+2U)%3U;ui->selected_keyboard=1;}
    if(input->next){ui->selected=(ui->selected+1U)%3U;ui->selected_keyboard=1;}
    if(hover>=0&&!ui->selected_keyboard)ui->selected=(unsigned int)hover;
    activate=(input->primary_pressed&&hover>=0)||input->accept;
    if(!activate)return 0;
    selected=input->primary_pressed&&hover>=0?(unsigned int)hover:ui->selected;
    if(ui->click)ui->click(ui->click_context);
    if(selected==0U) {
        if(ui->has_snapshot&&!ui->snapshot.player.health)
            maage_client_ui_queue_respawn(ui);
        else ui->route_result=maage_client_ui_back(ui);
    }else if(selected==1U)
        ui->route_result=maage_client_ui_open(ui,MAAGE_CLIENT_UI_OPTIONS);
    else return MAAGE_CLIENT_UI_EXIT;
    return 0;
}
static void pause_draw(maage_client_ui *ui,void *context)
{
    static const char *const labels[3]={"Resume Game","Options...","Save and Quit to Title"};
    unsigned int n;
    maage_client_ui_rect r;
    float cx=ui->content.x+ui->content.width*0.5f;
    float cy=ui->content.y+ui->content.height*0.5f;
    (void)context;
    maage_client_ui_text(ui,"Game Menu",cx,cy-54.0f,MAAGE_CLIENT_UI_CENTER,1,1,1);
    for(n=0;n<3U;++n) {
        r=pause_rect(ui,n);
        maage_client_ui_button(ui,r,n==0&&ui->has_snapshot&&
            !ui->snapshot.player.health?"Respawn":labels[n],
            ui->selected_keyboard?ui->selected==n:pause_hit(ui,r));
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
    ui->fov_degrees=70;
    ui->audio_volume[0] = MAAGE_CLIENT_AUDIO_DEFAULT_MASTER;
    ui->audio_volume[1] = MAAGE_CLIENT_AUDIO_DEFAULT_BLOCKS;
    ui->audio_volume[2] = MAAGE_CLIENT_AUDIO_DEFAULT_PLAYER;
    ui->audio_volume[3] = MAAGE_CLIENT_AUDIO_DEFAULT_AMBIENT;
    ui->audio_volume[4] = MAAGE_CLIENT_AUDIO_DEFAULT_WEATHER;
    ui->audio_volume[5] = MAAGE_CLIENT_AUDIO_DEFAULT_MUSIC;
    ui->audio_volume[6] = MAAGE_CLIENT_AUDIO_DEFAULT_UI;
    ui->vsync_supported = maage_status_is_success(maage_renderer_set_vsync(renderer, 1));
    ui->content.width = 320; ui->content.height = 240;
    ui->mouse_x = ui->mouse_y = ui->previous_mouse_x = ui->previous_mouse_y = -1;
    result = maage_client_ui_resources_load(&ui->resources, renderer, application_data, block_atlas, trace);
    if (maage_status_failed(result)) {
        result = maage_status_failed(ui->resources.recovery_status) ? ui->resources.recovery_status :
            maage_status_make(MAAGE_STATUS_ALLOCATION_FAILED, MAAGE_CONTEXT_GUI, MAAGE_OPERATION_ATLAS, 0);
        maage_client_ui_destroy(ui);
        return result;
    }
    result=maage_client_ui_register_screen(ui,MAAGE_CLIENT_UI_PAUSE,pause_draw,pause_input,NULL);
    if(!maage_status_failed(result))result=maage_client_ui_options_register(ui);
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
    free(ui->creative_items);
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
    ui->primary_down = input->primary_down;
    if (ui->primary_down) maage_client_ui_cursor(ui, 0);
    if (input->f3) ui->debug = !ui->debug;
    if (input->f11) maage_client_ui_request_window_mode(ui,
        (maage_client_window_choice)(ui->window_mode ? MAAGE_CLIENT_WINDOWED : ui->last_nonwindowed_mode));
    if(input->escape && (maage_client_ui_current(ui)<MAAGE_CLIENT_UI_TITLE ||
        maage_client_ui_current(ui)==MAAGE_CLIENT_UI_VIDEO_SETTINGS ||
        maage_client_ui_current(ui)==MAAGE_CLIENT_UI_SOUND_SETTINGS ||
        maage_client_ui_current(ui)==MAAGE_CLIENT_UI_CRAFTING_TABLE)) {
        ui->dragging_fps=0;ui->dragging_volume=0;ui->dragging_fov=0;
        return maage_client_ui_modal(ui) ? maage_client_ui_back(ui) : maage_client_ui_open(ui, MAAGE_CLIENT_UI_PAUSE);
    }
    id = maage_client_ui_current(ui);
    if (input->inventory && ui->has_snapshot) {
        if (id == MAAGE_CLIENT_UI_INVENTORY || id == MAAGE_CLIENT_UI_CRAFTING_TABLE) return maage_client_ui_back(ui);
        if (!id && !ui->container_command_pending) {
            maage_session_command command;
            maage_status result;
            memset(&command, 0, sizeof(command)); command.kind = MAAGE_CMD_CONTAINER_OPEN;
            result = maage_client_ui_queue_command(ui, &command);
            if (maage_status_failed(result)) return result;
            return maage_client_ui_open(ui, MAAGE_CLIENT_UI_INVENTORY);
        }
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
    ui->fov_degrees = options->fov_degrees;
    if(ui->fov_degrees<30)ui->fov_degrees=30;
    if(ui->fov_degrees>110)ui->fov_degrees=110;
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
        if (!maage_status_is_success(result)) {
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
    options->fov_degrees=ui->fov_degrees;
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
{
    maage_client_ui_screen_id id=maage_client_ui_current(ui);
    return id==MAAGE_CLIENT_UI_OPTIONS ||
        id==MAAGE_CLIENT_UI_VIDEO_SETTINGS ||
        id==MAAGE_CLIENT_UI_SOUND_SETTINGS;
}
int maage_client_ui_window_mode_changing(const maage_client_ui *ui)
{ return ui && (ui->mode_pending || ui->mode_changing); }

double maage_client_ui_fov_radians(const maage_client_ui *ui)
{
    return (ui ? ui->fov_degrees : 70)*0.017453292519943295;
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
    ui->pointer_hover = 0;
    ui->batch.gaussian_background_indices = 0;
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
        if (id == MAAGE_CLIENT_UI_OPTIONS || id == MAAGE_CLIENT_UI_VIDEO_SETTINGS ||
            id == MAAGE_CLIENT_UI_SOUND_SETTINGS)
            maage_client_ui_menu_background(ui);
        else if (id < MAAGE_CLIENT_UI_TITLE)
            maage_client_ui_panel(ui, rectangle, 0.03f, 0.03f, 0.04f, 0.68f);
        /* Only interactive content is kept within safe-area bounds; the title
         * panorama and its darkening layer must fill even the notch region. */
        ui->clip = id == MAAGE_CLIENT_UI_TITLE ? rectangle : ui->content;
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
    maage_client_ui_cursor(ui, id && ui->pointer_hover);
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
        ui->container_command_pending=0;ui->container_command_sequence=0;ui->container_close_requested=0;
        ui->command_read=ui->command_write=ui->command_count=0;
    }
    return maage_client_ui_open(ui,id);
}
maage_renderer *maage_client_ui_renderer(const maage_client_ui *ui)
{ return ui?ui->renderer:NULL; }
maage_window *maage_client_ui_window(const maage_client_ui *ui)
{ return ui?ui->window:NULL; }
