#ifndef MAAGE_LAUNCHER_WIDGETS_H
#define MAAGE_LAUNCHER_WIDGETS_H
#include "ui.h"
#include "maage/canvas.h"

typedef struct maage_ui_theme {
    maage_color background, navigation, panel, inset, text, muted, accent, danger, hover, line, selection;
    float gap, padding, control_height, body_size, small_size, title_size, section_size, button_size;
} maage_ui_theme;
typedef struct maage_ui_layout { maage_rect rect; float cursor, gap; int horizontal; } maage_ui_layout;
struct maage_launcher_ui {
    maage_canvas *canvas;
    maage_launcher_state *app;
    maage_ui_theme theme;
    maage_input_state input;
    maage_rect clip, clips[8];
    int clip_depth, active, focus, focus_ids[128], focus_count, blocked, keyboard_focus;
    int account_menu, last_page, last_section;
    float width, height, scale, scroll[8];
    maage_rect safe, account_anchor;
};
enum { MAAGE_UI_NORMAL, MAAGE_UI_PRIMARY, MAAGE_UI_DANGER, MAAGE_UI_TAB, MAAGE_UI_CARD };
maage_rect maage_ui_rect(float x, float y, float width, float height);
int maage_ui_contains(maage_rect rect, float x, float y);
void maage_ui_fill(maage_launcher_ui *ui, maage_rect rect, maage_color color);
maage_rect maage_ui_inset(maage_rect rect, float padding);
maage_rect maage_ui_panel(maage_launcher_ui *ui, maage_rect rect);
void maage_ui_outline(maage_launcher_ui *ui, maage_rect rect, maage_color color);
int maage_ui_tab(maage_launcher_ui *ui, int id, maage_rect rect, const char *text, int selected);
void maage_ui_status(maage_launcher_ui *ui, maage_rect rect, const char *title, const char *value, int ready);
void maage_ui_paragraph_color(maage_launcher_ui *ui, maage_rect rect, float size, maage_color color, const char *text);
void maage_ui_label(maage_launcher_ui *ui, maage_rect rect, float size, maage_color color, const char *text);
void maage_ui_paragraph(maage_launcher_ui *ui, maage_rect rect, float size, const char *text);
int maage_ui_button(maage_launcher_ui *ui, int id, maage_rect rect, const char *text, int style, int enabled);
void maage_ui_slider(maage_launcher_ui *ui, int id, maage_rect rect, float *value, float minimum, float maximum, int enabled);
void maage_ui_text_input(maage_launcher_ui *ui, int id, maage_rect rect, char *text, size_t capacity, int enabled);
void maage_ui_image(maage_launcher_ui *ui, maage_rect rect, const maage_pixel_buffer *image);
float maage_ui_heading(maage_launcher_ui *ui, maage_rect rect, const char *title, const char *subtitle);
void maage_ui_empty(maage_launcher_ui *ui, maage_rect rect, const char *title, const char *message);
float maage_ui_paragraph_height(maage_launcher_ui *ui, float width, float size, const char *text);
int maage_ui_summary(maage_launcher_ui *ui, int id, maage_rect rect, const char *title, const char *subtitle, const maage_pixel_buffer *image, int selected, int enabled);
void maage_ui_clip_push(maage_launcher_ui *ui, maage_rect rect);
void maage_ui_clip_pop(maage_launcher_ui *ui);
maage_rect maage_ui_scroll(maage_launcher_ui *ui, maage_rect rect, float *offset, float content_height);
maage_rect maage_ui_take(maage_ui_layout *layout, float size);
void maage_ui_pages(maage_launcher_ui *ui, maage_rect body);
void maage_ui_account_menu(maage_launcher_ui *ui);
void maage_ui_settings_page(maage_launcher_ui *ui, maage_rect body);
#endif
