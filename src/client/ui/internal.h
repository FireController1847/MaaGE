#ifndef MAAGE_CLIENT_UI_INTERNAL_H
#define MAAGE_CLIENT_UI_INTERNAL_H
#include "ui.h"
#include "maage/content.h"
#include "maage/image.h"
#include "maage/internal/registry_pages.h"

/* The original GUI resources occupy the first 512 atlas rows. */
#define MAAGE_CLIENT_UI_COMMAND_CAPACITY 64U
#define MAAGE_CLIENT_UI_BLOCK_ATLAS_Y 512
typedef struct maage_client_ui_screen {
    maage_client_ui_screen_draw draw;
    maage_client_ui_screen_input input;
    void *context;
} maage_client_ui_screen;
typedef struct maage_client_ui_navigation {
    maage_registry_pages screens;
    maage_client_ui_screen_id *stack;
    size_t registered, depth;
    int finalized;
} maage_client_ui_navigation;
typedef struct maage_client_ui_resources {
    maage_render_texture *texture;
    unsigned char glyph_width[256];
    int atlas_edge, block_atlas_width, block_atlas_height;
    maage_status load_status, recovery_status;
    maage_status source_results[5];
    unsigned int failed_resources;
    unsigned char title_available[7];
    int menu_background_available;
    float menu_background_uv[4];
} maage_client_ui_resources;
/* One optional retained F3 column; the left gameplay column stays immediate. */
typedef struct maage_client_ui_debug_cache {
    maage_render_ui_vertex *vertices;
    size_t quads, capacity;
    char lines[MAAGE_CLIENT_UI_DEBUG_LINES][MAAGE_CLIENT_UI_DEBUG_CHARACTERS];
    unsigned char glyph_width[256];
    maage_client_ui_rect content;
    maage_render_texture *texture;
    float width, height;
    unsigned int count;
    int atlas_edge, scale, valid, disabled;
} maage_client_ui_debug_cache;
struct maage_client_ui {
    maage_client_ui_navigation navigation;
    maage_renderer *renderer;
    maage_window *window;
    const maage_item_registry *items;
    const maage_block_registry *blocks;
    float inventory_grass_color[3];
    maage_session_snapshot snapshot; /* Value-only, no World pointers. */
    int has_snapshot, container_command_pending, container_close_requested;
    unsigned long container_command_sequence;
    maage_session_command commands[MAAGE_CLIENT_UI_COMMAND_CAPACITY];
    unsigned int command_read, command_write, command_count;
    maage_client_ui_click_callback click;
    void *click_context;
    unsigned int creative_page;
    maage_item_stack *creative_items;
    size_t creative_count;
    float item_uv[MAAGE_CONTENT_TEXTURE_COUNT][4];
    maage_client_ui_resources resources;
    maage_client_ui_debug_cache debug_cache;
    maage_render_ui_vertex *vertices;
    unsigned int *indices;
    size_t quad_capacity;
    maage_render_ui_data batch;
    maage_client_ui_rect clip, content;
    maage_window_insets safe_area;
    float width, height, mouse_x, mouse_y, previous_mouse_x, previous_mouse_y;
    unsigned int selected;
    int selected_keyboard;
    int primary_down, pointer_hover, cursor_unavailable;
    int debug, gui_scale, requested_scale, fps_step, vsync, vsync_supported, dragging_fps, leaves_mode;
    int audio_volume[7], dragging_volume;
    int dragging_fov, fov_degrees, video_first_row;
    int window_mode, last_nonwindowed_mode, mode_pending, mode_changing;
    int confirmed_window_mode, confirmed_fullscreen_style;
    double mode_requested_at;
    maage_status draw_result;
    maage_status notice_result, route_result;
    double notice_until, notice_now;
    char notice_text[64];
};
maage_status maage_client_ui_resources_load(maage_client_ui_resources *resources,
    maage_renderer *renderer, const char *application_data, const maage_pixel_buffer *block_atlas, maage_status_trace *trace);
maage_status maage_client_ui_font_initialize(maage_client_ui_resources *resources, const maage_image *font);
maage_status maage_client_ui_font_fallback(maage_image *font);
float maage_client_ui_text_width(const maage_client_ui *ui, const char *text);
/* Vanilla 1.7.10 tiled options background over the entire drawable,
 * restoring the previous clipping region before returning. */
void maage_client_ui_menu_background(maage_client_ui *ui);
void maage_client_ui_slider(maage_client_ui *ui,maage_client_ui_rect rectangle,
    const char *label,float value,int highlighted);
/* Shared cursor feedback for standard and custom-drawn controls. */
void maage_client_ui_pointer(maage_client_ui *ui, maage_client_ui_rect rectangle);
maage_status maage_client_ui_options_register(maage_client_ui *ui);
void maage_client_ui_request_window_mode(maage_client_ui *ui,maage_client_window_choice choice);
void maage_client_ui_rectangle(maage_client_ui *ui, maage_client_ui_rect rectangle,
    float u0, float v0, float u1, float v1, float red, float green, float blue, float alpha);
void maage_client_ui_font_draw(maage_client_ui *ui, const char *text,
    float x, float y, float red, float green, float blue);
maage_status maage_client_ui_inventory_register(maage_client_ui *ui);
/* These helpers queue exact numeric commands, never mutate a gameplay object. */
maage_status maage_client_ui_queue_command(maage_client_ui *ui,
    const maage_session_command *command);
void maage_client_ui_queue_game_mode(maage_client_ui *ui);
void maage_client_ui_queue_respawn(maage_client_ui *ui);
void maage_client_ui_gameplay_draw(maage_client_ui *ui);
/* Appends already-clipped quads, rebasing indices into the current frame. */
int maage_client_ui_append_quads(maage_client_ui *ui,
    const maage_render_ui_vertex *vertices, size_t count);
maage_client_ui_screen *maage_client_ui_screen_get(const maage_client_ui *ui,
    maage_client_ui_screen_id id);
maage_client_ui_screen_id maage_client_ui_current(const maage_client_ui *ui);
#endif
