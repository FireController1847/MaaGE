#ifndef MAAGE_CLIENT_UI_INTERNAL_H
#define MAAGE_CLIENT_UI_INTERNAL_H
#include "ui.h"
#include "maage/image.h"

/* A frame is bounded by the supplied diagnostic text plus menu/HUD geometry. */
#define MAAGE_CLIENT_UI_QUADS (2 * MAAGE_CLIENT_UI_DEBUG_LINES * MAAGE_CLIENT_UI_DEBUG_CHARACTERS + 512)
#define MAAGE_CLIENT_UI_ATLAS_EDGE 512
typedef struct maage_client_ui_screen {
    maage_client_ui_screen_draw draw;
    maage_client_ui_screen_input input;
    void *context;
} maage_client_ui_screen;
typedef struct maage_client_ui_navigation {
    maage_client_ui_screen *screens;
    maage_client_ui_screen_id *stack;
    size_t capacity, registered, depth;
    int finalized;
} maage_client_ui_navigation;
typedef struct maage_client_ui_resources {
    maage_render_texture *texture;
    unsigned char glyph_width[256];
} maage_client_ui_resources;
struct maage_client_ui {
    maage_client_ui_navigation navigation;
    maage_client_ui_resources resources;
    maage_render_ui_vertex vertices[MAAGE_CLIENT_UI_QUADS * 4];
    unsigned int indices[MAAGE_CLIENT_UI_QUADS * 6];
    maage_render_ui_data batch;
    maage_client_ui_rect clip, content;
    maage_window_insets safe_area;
    float width, height, mouse_x, mouse_y, previous_mouse_x, previous_mouse_y;
    unsigned int selected;
    int debug, overflow, overflow_reported;
};
int maage_client_ui_resources_load(maage_client_ui_resources *resources,
    maage_renderer *renderer, const char *application_data);
void maage_client_ui_font_initialize(maage_client_ui_resources *resources, const maage_image *font);
float maage_client_ui_text_width(const maage_client_ui *ui, const char *text);
void maage_client_ui_rectangle(maage_client_ui *ui, maage_client_ui_rect rectangle,
    float u0, float v0, float u1, float v1, float red, float green, float blue, float alpha);
void maage_client_ui_font_draw(maage_client_ui *ui, const char *text,
    float x, float y, float red, float green, float blue);
maage_client_ui_screen_id maage_client_ui_current(const maage_client_ui *ui);
#endif
