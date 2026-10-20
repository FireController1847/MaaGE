#include "internal.h"
#include "maage/log.h"
#include <stdlib.h>
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
        rectangle.y = (float)(int)(ui->content.y + ui->content.height * 0.5f) + (float)index * 24;
    return rectangle;
}

static int maage_client_ui_contains(maage_client_ui_rect rectangle, float x, float y)
{
    return x >= rectangle.x && x < rectangle.x + rectangle.width &&
        y >= rectangle.y && y < rectangle.y + rectangle.height;
}

static unsigned int maage_client_ui_menu_input(maage_client_ui *ui,
    const maage_client_ui_input *input, void *context)
{
    maage_client_ui_screen_id screen;
    maage_client_ui_rect rectangle;
    unsigned int count, i;
    int hover = -1, activate;
    (void)context;
    screen = maage_client_ui_current(ui);
    count = screen == MAAGE_CLIENT_UI_PAUSE ? 3 : (ui->game ? 2 : 1);
    for (i = 0; i < count; ++i) {
        rectangle = maage_client_ui_menu_rectangle(ui, i, screen);
        if (maage_client_ui_contains(rectangle, ui->mouse_x, ui->mouse_y)) hover = (int)i;
    }
    if (input->previous) ui->selected = (ui->selected + count - 1) % count;
    if (input->next) ui->selected = (ui->selected + 1) % count;
    if (hover >= 0 && (input->primary_pressed || ui->mouse_x != ui->previous_mouse_x ||
        ui->mouse_y != ui->previous_mouse_y)) ui->selected = (unsigned int)hover;
    activate = input->accept || (input->primary_pressed && hover >= 0);
    if (!activate) return 0;
    if (screen == MAAGE_CLIENT_UI_OPTIONS) {
        if (ui->game && !ui->selected)
            maage_gameplay_set_mode(ui->game, ui->game->mode == MAAGE_GAME_MODE_CREATIVE ?
                MAAGE_GAME_MODE_SURVIVAL : MAAGE_GAME_MODE_CREATIVE);
        else maage_client_ui_back(ui);
    }
    else if (ui->selected == 0) {
        if (ui->game && !ui->game->health) return MAAGE_CLIENT_UI_RESPAWN;
        maage_client_ui_back(ui);
    }
    else if (ui->selected == 1) maage_client_ui_open(ui, MAAGE_CLIENT_UI_OPTIONS);
    else return MAAGE_CLIENT_UI_EXIT;
    return 0;
}

static void maage_client_ui_menu_draw(maage_client_ui *ui, void *context)
{
    static const char *const labels[3] = {"Resume Game", "Options...", "Exit Client"};
    maage_client_ui_screen_id screen;
    maage_client_ui_rect rectangle;
    unsigned int i;
    float center, middle;
    (void)context;
    screen = maage_client_ui_current(ui);
    center = (float)(int)(ui->content.x + ui->content.width * 0.5f);
    middle = (float)(int)(ui->content.y + ui->content.height * 0.5f);
    maage_client_ui_text(ui, screen == MAAGE_CLIENT_UI_PAUSE ? "Game Menu" : "Options",
        center, middle - 54, MAAGE_CLIENT_UI_CENTER, 1, 1, 1);
    if (screen == MAAGE_CLIENT_UI_PAUSE) {
        for (i = 0; i < 3; ++i) {
            rectangle = maage_client_ui_menu_rectangle(ui, i, screen);
            maage_client_ui_button(ui, rectangle, i == 0 && ui->game && !ui->game->health ?
                "Respawn" : labels[i], ui->selected == i);
        }
    } else {
        if (ui->game) {
            rectangle = maage_client_ui_menu_rectangle(ui, 0, screen);
            maage_client_ui_button(ui, rectangle, ui->game->mode == MAAGE_GAME_MODE_CREATIVE ?
                "Game Mode: Creative" : "Game Mode: Survival", ui->selected == 0);
        }
        rectangle = maage_client_ui_menu_rectangle(ui, ui->game ? 1 : 0, screen);
        maage_client_ui_button(ui, rectangle, "Back", ui->selected == (ui->game ? 1U : 0U));
    }
}

maage_client_ui *maage_client_ui_create(maage_renderer *renderer, const char *application_data,
    const maage_pixel_buffer *block_atlas)
{
    maage_client_ui *ui;
    if (!renderer) return NULL;
    ui = (maage_client_ui *)calloc(1, sizeof(*ui));
    if (!ui) return NULL;
    ui->width = 320; ui->height = 240;
    ui->content.width = 320; ui->content.height = 240;
    ui->mouse_x = ui->mouse_y = ui->previous_mouse_x = ui->previous_mouse_y = -1;
    if (!maage_client_ui_resources_load(&ui->resources, renderer, application_data, block_atlas) ||
        !maage_client_ui_register_screen(ui, MAAGE_CLIENT_UI_PAUSE,
            maage_client_ui_menu_draw, maage_client_ui_menu_input, NULL) ||
        !maage_client_ui_register_screen(ui, MAAGE_CLIENT_UI_OPTIONS,
            maage_client_ui_menu_draw, maage_client_ui_menu_input, NULL) ||
        !maage_client_ui_inventory_register(ui)) {
        maage_client_ui_destroy(ui);
        return NULL;
    }
    ui->batch.vertices = ui->vertices; ui->batch.indices = ui->indices;
    ui->batch.texture = ui->resources.texture;
    return ui;
}

void maage_client_ui_destroy(maage_client_ui *ui)
{
    if (!ui) return;
    maage_render_texture_destroy(ui->resources.texture);
    free(ui->navigation.screens); free(ui->navigation.stack); free(ui);
}

unsigned int maage_client_ui_update(maage_client_ui *ui, const maage_client_ui_input *input,
    int framebuffer_width, int framebuffer_height, int window_width, int window_height)
{
    maage_client_ui_screen *screen;
    maage_client_ui_screen_id id;
    unsigned int action = 0;
    int scale, vertical;
    float x_scale, y_scale, right, bottom;
    if (!ui || !input || framebuffer_width <= 0 || framebuffer_height <= 0 ||
        window_width <= 0 || window_height <= 0 || !ui->navigation.finalized) return 0;
    scale = framebuffer_width / 320; vertical = framebuffer_height / 240;
    if (scale > vertical) scale = vertical;
    if (scale < 1) scale = 1;
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
    if (input->escape) {
        if (maage_client_ui_modal(ui)) maage_client_ui_back(ui);
        else maage_client_ui_open(ui, MAAGE_CLIENT_UI_PAUSE);
        return 0;
    }
    id = maage_client_ui_current(ui);
    if (input->inventory && ui->game) {
        if (id == MAAGE_CLIENT_UI_INVENTORY) maage_client_ui_back(ui);
        else if (!id) maage_client_ui_open(ui, MAAGE_CLIENT_UI_INVENTORY);
        return 0;
    }
    if (id) {
        screen = &ui->navigation.screens[id];
        if (screen->input) action = screen->input(ui, input, screen->context);
    }
    return action;
}

int maage_client_ui_debug_visible(const maage_client_ui *ui)
{
    return ui && ui->debug;
}

void maage_client_ui_set_safe_area(maage_client_ui *ui, const maage_window_insets *insets)
{
    if (!ui) return;
    if (insets) ui->safe_area = *insets;
    else memset(&ui->safe_area, 0, sizeof(ui->safe_area));
}

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

const maage_render_ui_data *maage_client_ui_draw(maage_client_ui *ui,
    const maage_client_ui_debug *debug)
{
    maage_client_ui_rect rectangle;
    maage_client_ui_screen_id id;
    maage_client_ui_screen *screen;
    if (!ui || !ui->navigation.finalized) return NULL;
    ui->batch.vertex_count = ui->batch.index_count = 0;
    ui->overflow = 0;
    ui->batch.logical_width = ui->width; ui->batch.logical_height = ui->height;
    ui->clip.x = ui->clip.y = 0; ui->clip.width = ui->width; ui->clip.height = ui->height;
    maage_client_ui_gameplay_draw(ui);
    if (!maage_client_ui_modal(ui)) {
        rectangle.x = ui->width * 0.5f - 7.5f;
        rectangle.y = ui->height * 0.5f - 7.5f;
        rectangle.width = rectangle.height = 15;
        maage_client_ui_rectangle(ui, rectangle, 0, 0,
            15.0f / MAAGE_CLIENT_UI_ATLAS_EDGE, 15.0f / MAAGE_CLIENT_UI_ATLAS_EDGE, 1, 1, 1, 1);
    }
    if (ui->debug && debug) {
        maage_client_ui_debug_column(ui, debug->left, debug->left_count, 0);
        maage_client_ui_debug_column(ui, debug->right, debug->right_count, 1);
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
    return ui->overflow ? NULL : &ui->batch;
}
