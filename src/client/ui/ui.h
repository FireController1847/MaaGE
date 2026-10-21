#ifndef MAAGE_CLIENT_UI_H
#define MAAGE_CLIENT_UI_H
#include "maage/render.h"
#include "maage/gameplay.h"
#include "maage/world.h"
#include "../options.h"

typedef struct maage_client_ui maage_client_ui;
typedef unsigned int maage_client_ui_screen_id;
#define MAAGE_CLIENT_UI_GAMEPLAY 0U
#define MAAGE_CLIENT_UI_PAUSE 1U
#define MAAGE_CLIENT_UI_OPTIONS 2U
#define MAAGE_CLIENT_UI_INVENTORY 3U
#define MAAGE_CLIENT_UI_EXIT 1U
#define MAAGE_CLIENT_UI_RESPAWN 2U
#define MAAGE_CLIENT_UI_DEBUG_LINES 32
#define MAAGE_CLIENT_UI_DEBUG_CHARACTERS 192

typedef struct maage_client_ui_input {
    int escape, f3, f11, accept, previous, next, left, right, primary_pressed, primary_down, secondary_pressed, inventory;
    float mouse_x, mouse_y; /* Top-left-origin window logical coordinates. */
} maage_client_ui_input;
typedef struct maage_client_ui_debug {
    char left[MAAGE_CLIENT_UI_DEBUG_LINES][MAAGE_CLIENT_UI_DEBUG_CHARACTERS];
    char right[MAAGE_CLIENT_UI_DEBUG_LINES][MAAGE_CLIENT_UI_DEBUG_CHARACTERS];
    unsigned int left_count, right_count;
} maage_client_ui_debug;
typedef struct maage_client_ui_rect { float x, y, width, height; } maage_client_ui_rect;
typedef enum maage_client_ui_alignment {
    MAAGE_CLIENT_UI_LEFT = 0, MAAGE_CLIENT_UI_CENTER, MAAGE_CLIENT_UI_RIGHT
} maage_client_ui_alignment;
typedef void (*maage_client_ui_screen_draw)(maage_client_ui *ui, void *context);
typedef unsigned int (*maage_client_ui_screen_input)(maage_client_ui *ui,
    const maage_client_ui_input *input, void *context);

/* Owns one GUI atlas and reusable CPU batch; destroy before the renderer.
 * Resources resolve only here, from the existing reconstructed installation. */
maage_status maage_client_ui_create(maage_renderer *renderer, maage_window *window,
    const char *application_data, const maage_pixel_buffer *block_atlas,
    maage_client_ui **output, maage_status_trace *trace);
/* Returns the first original resource failure, even after successful recovery. */
maage_status maage_client_ui_resource_status(const maage_client_ui *ui);
/* Owner-only, bounded, nonmodal and deduplicated by result. Callers log the
 * occurrence once; the brief notice cross-references its exact result code. */
void maage_client_ui_notify(maage_client_ui *ui, maage_status result, double now);
void maage_client_ui_notice_time(maage_client_ui *ui, double now);
maage_status maage_client_ui_destroy(maage_client_ui *ui);
/* Borrow finalized registries and owner-thread gameplay for HUD and inventory. */
maage_status maage_client_ui_set_gameplay(maage_client_ui *ui, const maage_item_registry *items,
    maage_gameplay *game, const maage_block_registry *blocks);
/* Registration grows direct numeric indexing before finalization. ID zero is
 * gameplay (no modal). Unknown IDs and navigation cycles fail explicitly. */
maage_status maage_client_ui_register_screen(maage_client_ui *ui, maage_client_ui_screen_id id,
    maage_client_ui_screen_draw draw, maage_client_ui_screen_input input, void *context);
maage_status maage_client_ui_finalize(maage_client_ui *ui);
maage_status maage_client_ui_open(maage_client_ui *ui, maage_client_ui_screen_id id);
maage_status maage_client_ui_back(maage_client_ui *ui);
int maage_client_ui_modal(const maage_client_ui *ui);
int maage_client_ui_debug_visible(const maage_client_ui *ui);
/* Insets are window logical units. Only GUI content avoids these obstructions;
 * the world and crosshair retain the full drawable rectangle. */
maage_status maage_client_ui_set_safe_area(maage_client_ui *ui, const maage_window_insets *insets);
/* Synchronize observed mode/completion before the frame's GUI input is routed. */
maage_status maage_client_ui_set_window_state(maage_client_ui *ui, const maage_window_state *state);
/* Client-owned persistence reads/writes these numeric option snapshots. */
maage_status maage_client_ui_apply_options(maage_client_ui *ui, const maage_client_options *options);
void maage_client_ui_get_options(const maage_client_ui *ui, maage_client_options *options);
int maage_client_ui_options_visible(const maage_client_ui *ui);
int maage_client_ui_window_mode_changing(const maage_client_ui *ui);
/* Edge-triggered input; owner controls capture and simulation on modal changes.
 * Integer GUI scale uses framebuffer pixels, with a 320x240 logical minimum. */
maage_status maage_client_ui_update(maage_client_ui *ui, const maage_client_ui_input *input,
    int framebuffer_width, int framebuffer_height, int window_width, int window_height, unsigned int *actions);
/* Build the UI after world preparation; returned CPU data is borrowed until
 * the next draw/destroy and submitted with the world in renderer_draw_frame.
 * NULL indicates an invalid UI or exhausted geometry allocation/index range;
 * no partial batch is returned. The caller treats that as a frame-build failure. */
maage_status maage_client_ui_draw(maage_client_ui *ui,
    const maage_client_ui_debug *debug, const maage_render_ui_data **output);
/* Current option values; 0 FPS means Unlimited; 0 GUI scale means Auto. */
int maage_client_ui_max_fps(const maage_client_ui *ui);
int maage_client_ui_vsync(const maage_client_ui *ui);
int maage_client_ui_gui_scale(const maage_client_ui *ui);
maage_leaves_mode maage_client_ui_leaves_mode(const maage_client_ui *ui);
float maage_client_ui_width(const maage_client_ui *ui);
float maage_client_ui_height(const maage_client_ui *ui);

/* Small immediate primitives for registered Client screens. RGBA values are
 * straight-alpha [0,1]. Geometry clips to the current logical viewport. */
void maage_client_ui_panel(maage_client_ui *ui, maage_client_ui_rect rectangle,
    float red, float green, float blue, float alpha);
/* UV coordinates reference the GUI atlas resolved during initialization. */
void maage_client_ui_rectangle(maage_client_ui *ui, maage_client_ui_rect rectangle,
    float u0, float v0, float u1, float v1, float red, float green, float blue, float alpha);
void maage_client_ui_text(maage_client_ui *ui, const char *text, float x, float y,
    maage_client_ui_alignment alignment, float red, float green, float blue);
void maage_client_ui_button(maage_client_ui *ui, maage_client_ui_rect rectangle,
    const char *label, int highlighted);
#endif
