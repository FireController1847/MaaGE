#ifndef MAAGE_CLIENT_UI_H
#define MAAGE_CLIENT_UI_H
#include "maage/render.h"

typedef struct maage_client_ui maage_client_ui;
typedef unsigned int maage_client_ui_screen_id;
#define MAAGE_CLIENT_UI_GAMEPLAY 0U
#define MAAGE_CLIENT_UI_PAUSE 1U
#define MAAGE_CLIENT_UI_OPTIONS 2U
#define MAAGE_CLIENT_UI_EXIT 1U
#define MAAGE_CLIENT_UI_DEBUG_LINES 32
#define MAAGE_CLIENT_UI_DEBUG_CHARACTERS 192

typedef struct maage_client_ui_input {
    int escape, f3, accept, previous, next, primary_pressed;
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
maage_client_ui *maage_client_ui_create(maage_renderer *renderer, const char *application_data);
void maage_client_ui_destroy(maage_client_ui *ui);
/* Registration grows direct numeric indexing before finalization. ID zero is
 * gameplay (no modal). Unknown IDs and navigation cycles fail explicitly. */
int maage_client_ui_register_screen(maage_client_ui *ui, maage_client_ui_screen_id id,
    maage_client_ui_screen_draw draw, maage_client_ui_screen_input input, void *context);
int maage_client_ui_finalize(maage_client_ui *ui);
int maage_client_ui_open(maage_client_ui *ui, maage_client_ui_screen_id id);
int maage_client_ui_back(maage_client_ui *ui);
int maage_client_ui_modal(const maage_client_ui *ui);
int maage_client_ui_debug_visible(const maage_client_ui *ui);
/* Insets are window logical units. Only GUI content avoids these obstructions;
 * the world and crosshair retain the full drawable rectangle. */
void maage_client_ui_set_safe_area(maage_client_ui *ui, const maage_window_insets *insets);
/* Edge-triggered input; owner controls capture and simulation on modal changes.
 * Integer GUI scale uses framebuffer pixels, with a 320x240 logical minimum. */
unsigned int maage_client_ui_update(maage_client_ui *ui, const maage_client_ui_input *input,
    int framebuffer_width, int framebuffer_height, int window_width, int window_height);
/* Build the UI after world preparation; returned CPU data is borrowed until
 * the next draw/destroy and submitted with the world in renderer_draw_frame.
 * NULL indicates an invalid UI or a rejected overflowing batch; no partial
 * batch is returned. The caller must treat that as a frame-build failure. */
const maage_render_ui_data *maage_client_ui_draw(maage_client_ui *ui,
    const maage_client_ui_debug *debug);
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
