#include "widgets.h"
#include <string.h>

maage_rect maage_ui_rect(float x, float y, float width, float height)
{
    maage_rect rect;
    rect.x = x; rect.y = y; rect.width = width > 0 ? width : 0; rect.height = height > 0 ? height : 0;
    return rect;
}
int maage_ui_contains(maage_rect rect, float x, float y)
{
    return x >= rect.x && y >= rect.y && x < rect.x + rect.width && y < rect.y + rect.height;
}
void maage_ui_fill(maage_launcher_ui *ui, maage_rect rect, maage_color color)
{
    maage_canvas_rect(ui->canvas, rect, color);
}
maage_rect maage_ui_inset(maage_rect rect, float padding)
{
    return maage_ui_rect(rect.x + padding, rect.y + padding, rect.width - padding * 2, rect.height - padding * 2);
}
void maage_ui_outline(maage_launcher_ui *ui, maage_rect rect, maage_color color)
{
    maage_ui_fill(ui, maage_ui_rect(rect.x, rect.y, rect.width, 1), color);
    maage_ui_fill(ui, maage_ui_rect(rect.x, rect.y + rect.height - 1, rect.width, 1), color);
    maage_ui_fill(ui, maage_ui_rect(rect.x, rect.y, 1, rect.height), color);
    maage_ui_fill(ui, maage_ui_rect(rect.x + rect.width - 1, rect.y, 1, rect.height), color);
}
maage_rect maage_ui_panel(maage_launcher_ui *ui, maage_rect rect)
{
    maage_ui_fill(ui, rect, ui->theme.panel);
    maage_ui_outline(ui, rect, ui->theme.line);
    return maage_ui_inset(rect, ui->theme.padding);
}
void maage_ui_label(maage_launcher_ui *ui, maage_rect rect, float size, maage_color color, const char *text)
{
    maage_ui_clip_push(ui, rect);
    maage_canvas_text(ui->canvas, rect.x, rect.y, size, color, text);
    maage_ui_clip_pop(ui);
}
void maage_ui_clip_push(maage_launcher_ui *ui, maage_rect rect)
{
    float right, bottom;
    if (ui->clip_depth == 8) return;
    ui->clips[ui->clip_depth++] = ui->clip;
    right = rect.x + rect.width;
    bottom = rect.y + rect.height;
    if (rect.x < ui->clip.x) rect.x = ui->clip.x;
    if (rect.y < ui->clip.y) rect.y = ui->clip.y;
    if (right > ui->clip.x + ui->clip.width) right = ui->clip.x + ui->clip.width;
    if (bottom > ui->clip.y + ui->clip.height) bottom = ui->clip.y + ui->clip.height;
    rect.width = right > rect.x ? right - rect.x : 0;
    rect.height = bottom > rect.y ? bottom - rect.y : 0;
    ui->clip = rect;
    maage_canvas_clip(ui->canvas, rect);
}
void maage_ui_clip_pop(maage_launcher_ui *ui)
{
    if (ui->clip_depth) ui->clip = ui->clips[--ui->clip_depth];
    maage_canvas_clip(ui->canvas, ui->clip);
}
static int maage_ui_hit(maage_launcher_ui *ui, int id, maage_rect rect, int enabled)
{
    int hot, clicked = 0;
    if (!enabled || ui->blocked) return 0;
    if (ui->focus_count < 128 && rect.width > 0 && rect.height > 0 && rect.x + rect.width > ui->clip.x && rect.x < ui->clip.x + ui->clip.width &&
        rect.y + rect.height > ui->clip.y && rect.y < ui->clip.y + ui->clip.height)
        ui->focus_ids[ui->focus_count++] = id;
    hot = maage_ui_contains(rect, ui->input.x, ui->input.y) && maage_ui_contains(ui->clip, ui->input.x, ui->input.y);
    if (hot && ui->input.pressed) { ui->active = id; ui->focus = id; }
    if (ui->input.released && ui->active == id) { clicked = hot; ui->active = 0; }
    if (ui->focus == id && ui->input.key == MAAGE_KEY_ENTER) { clicked = 1; ui->input.key = MAAGE_KEY_NONE; }
    return clicked;
}
int maage_ui_button(maage_launcher_ui *ui, int id, maage_rect rect, const char *text, int style, int enabled)
{
    maage_color color = ui->theme.hover;
    float width, size = ui->theme.button_size;
    int clicked = maage_ui_hit(ui, id, rect, enabled);
    int hot = maage_ui_contains(rect, ui->input.x, ui->input.y) && maage_ui_contains(ui->clip, ui->input.x, ui->input.y);
    if (style == MAAGE_UI_CARD) color = ui->theme.panel;
    if (style == MAAGE_UI_TAB) color = ui->theme.navigation;
    if (style == MAAGE_UI_PRIMARY) color = ui->theme.accent;
    if (style == MAAGE_UI_DANGER) color = ui->theme.danger;
    if (!enabled) {
        if (style == MAAGE_UI_PRIMARY || style == MAAGE_UI_DANGER) {
            color.r = (unsigned char)((color.r + ui->theme.background.r) / 2);
            color.g = (unsigned char)((color.g + ui->theme.background.g) / 2);
            color.b = (unsigned char)((color.b + ui->theme.background.b) / 2);
        } else color = ui->theme.inset;
    }
    else if (!ui->blocked && (hot || ui->active == id)) {
        color.r = (unsigned char)(color.r + (ui->active == id ? 8 : 16));
        color.g = (unsigned char)(color.g + (ui->active == id ? 8 : 16));
        color.b = (unsigned char)(color.b + (ui->active == id ? 8 : 16));
    }
    maage_ui_fill(ui, rect, color);
    if (ui->focus == id && ui->keyboard_focus && enabled && !ui->blocked)
        maage_ui_outline(ui, maage_ui_inset(rect, 3), ui->theme.text);
    width = maage_canvas_text_width(ui->canvas, size, text);
    maage_ui_label(ui, maage_ui_rect(rect.x + (rect.width > width ? (rect.width - width) / 2 : 6),
        rect.y + (rect.height - size - 3) / 2, rect.width - 8, size + 8), size,
        enabled ? ui->theme.text : ui->theme.muted, text);
    return clicked;
}
int maage_ui_tab(maage_launcher_ui *ui, int id, maage_rect rect, const char *text, int selected)
{
    int clicked, hot;
    maage_color previous = ui->theme.navigation;
    if (selected) ui->theme.navigation = ui->theme.selection;
    clicked = maage_ui_button(ui, id, rect, text, MAAGE_UI_TAB, 1);
    ui->theme.navigation = previous;
    hot = !ui->blocked && maage_ui_contains(rect, ui->input.x, ui->input.y) && maage_ui_contains(ui->clip, ui->input.x, ui->input.y);
    if (selected || hot) maage_ui_fill(ui, maage_ui_rect(rect.x + 8, rect.y + rect.height - 3, rect.width - 16, 3),
        selected ? ui->theme.accent : ui->theme.muted);
    return clicked;
}
void maage_ui_status(maage_launcher_ui *ui, maage_rect rect, const char *title, const char *value, int ready)
{
    maage_ui_fill(ui, maage_ui_rect(rect.x, rect.y + 3, 3, 43), ready ? ui->theme.accent : ui->theme.line);
    maage_ui_label(ui, maage_ui_rect(rect.x + 16, rect.y, rect.width - 16, 24), ui->theme.small_size, ui->theme.muted, title);
    maage_ui_label(ui, maage_ui_rect(rect.x + 16, rect.y + 25, rect.width - 16, 27), ui->theme.body_size, ui->theme.text, value);
}
void maage_ui_slider(maage_launcher_ui *ui, int id, maage_rect rect, float *value, float minimum, float maximum, int enabled)
{
    float position;
    int dragging = ui->active == id;
    if (rect.width <= 0 || maximum <= minimum) return;
    maage_ui_hit(ui, id, rect, enabled);
    if (enabled && !ui->blocked && (ui->active == id || (dragging && ui->input.released))) {
        position = (ui->input.x - rect.x) / rect.width;
        if (position < 0) position = 0;
        if (position > 1) position = 1;
        *value = minimum + position * (maximum - minimum);
    }
    if (enabled && !ui->blocked && ui->focus == id) {
        if (ui->input.key == MAAGE_KEY_LEFT) *value -= (maximum - minimum) / 40;
        if (ui->input.key == MAAGE_KEY_RIGHT) *value += (maximum - minimum) / 40;
    }
    if (*value < minimum) *value = minimum;
    if (*value > maximum) *value = maximum;
    position = (*value - minimum) / (maximum - minimum);
    maage_ui_fill(ui, maage_ui_rect(rect.x, rect.y + rect.height / 2 - 3, rect.width, 6), ui->theme.navigation);
    maage_ui_fill(ui, maage_ui_rect(rect.x, rect.y + rect.height / 2 - 3, rect.width * position, 6), enabled ? ui->theme.accent : ui->theme.hover);
    maage_ui_fill(ui, maage_ui_rect(rect.x + (rect.width - 18) * position, rect.y + (rect.height - 24) / 2, 18, 24),
        enabled ? ui->theme.accent : ui->theme.muted);
    if (ui->focus == id && ui->keyboard_focus && enabled && !ui->blocked) maage_ui_outline(ui, maage_ui_inset(rect, 2), ui->theme.text);
}
void maage_ui_text_input(maage_launcher_ui *ui, int id, maage_rect rect, char *text, size_t capacity, int enabled)
{
    size_t length = strlen(text), index = 0, count, remaining = strlen(ui->input.text);
    const unsigned char *input = (const unsigned char *)ui->input.text;
    maage_ui_hit(ui, id, rect, enabled);
    if (enabled && !ui->blocked && ui->focus == id) {
        if (ui->input.key == MAAGE_KEY_BACKSPACE && length) {
            --length;
            while (length && ((unsigned char)text[length] & 0xc0) == 0x80) --length;
            text[length] = '\0';
        }
        while (index < remaining) {
            count = input[index] < 0x80 ? 1 : input[index] < 0xe0 ? 2 : input[index] < 0xf0 ? 3 : 4;
            if (index + count > remaining || length + count >= capacity) break;
            if (input[index] >= 32 && input[index] != 127) { memcpy(text + length, input + index, count); length += count; }
            index += count;
        }
        text[length] = '\0';
    }
    maage_ui_fill(ui, rect, ui->theme.inset);
    maage_ui_outline(ui, rect, ui->theme.line);
    maage_ui_label(ui, maage_ui_rect(rect.x + 12, rect.y + (rect.height - ui->theme.body_size - 3) / 2, rect.width - 24, 26), ui->theme.body_size, enabled ? ui->theme.text : ui->theme.muted, text);
    if (ui->focus == id && ui->keyboard_focus && enabled && !ui->blocked) maage_ui_outline(ui, maage_ui_inset(rect, 2), ui->theme.text);
}
maage_rect maage_ui_take(maage_ui_layout *layout, float size)
{
    maage_rect rect = layout->rect;
    if (layout->horizontal) { rect.x += layout->cursor; rect.width = size; }
    else { rect.y += layout->cursor; rect.height = size; }
    layout->cursor += size + layout->gap;
    return rect;
}
maage_rect maage_ui_scroll(maage_launcher_ui *ui, maage_rect rect, float *offset, float content_height)
{
    float maximum = content_height > rect.height ? content_height - rect.height : 0;
    float thumb = rect.height < 36 ? rect.height : 36;
    if (!ui->blocked && maage_ui_contains(ui->clip, ui->input.x, ui->input.y) && maage_ui_contains(rect, ui->input.x, ui->input.y)) *offset -= ui->input.scroll * 5;
    if (*offset < 0) *offset = 0;
    if (*offset > maximum) *offset = maximum;
    if (maximum > 0) {
        maage_ui_fill(ui, maage_ui_rect(rect.x + rect.width - 3, rect.y + (*offset / maximum) * (rect.height - thumb), 3, thumb), ui->theme.hover);
    }
    maage_ui_clip_push(ui, rect);
    rect.y -= *offset;
    rect.height = content_height;
    return rect;
}
void maage_ui_image(maage_launcher_ui *ui, maage_rect rect, const maage_pixel_buffer *image)
{
    if (image) maage_canvas_image(ui->canvas, rect, image);
    else maage_ui_fill(ui, rect, ui->theme.navigation);
}
float maage_ui_heading(maage_launcher_ui *ui, maage_rect rect, const char *title, const char *subtitle)
{
    float title_height = maage_ui_paragraph_height(ui, rect.width, ui->theme.title_size, title);
    float subtitle_height = maage_ui_paragraph_height(ui, rect.width, ui->theme.body_size, subtitle);
    maage_ui_paragraph_color(ui, maage_ui_rect(rect.x, rect.y, rect.width, title_height), ui->theme.title_size, ui->theme.text, title);
    maage_ui_paragraph(ui, maage_ui_rect(rect.x, rect.y + title_height + 4, rect.width, subtitle_height), ui->theme.body_size, subtitle);
    return title_height + 4 + subtitle_height;
}
void maage_ui_empty(maage_launcher_ui *ui, maage_rect rect, const char *title, const char *message)
{
    maage_rect inside = maage_ui_panel(ui, rect);
    float height = maage_ui_paragraph_height(ui, inside.width, ui->theme.section_size, title);
    maage_ui_paragraph_color(ui, maage_ui_rect(inside.x, inside.y, inside.width, height), ui->theme.section_size, ui->theme.text, title);
    maage_ui_paragraph(ui, maage_ui_rect(inside.x, inside.y + height + 12, inside.width, inside.height - height - 12), ui->theme.body_size, message);
}
int maage_ui_summary(maage_launcher_ui *ui, int id, maage_rect rect, const char *title, const char *subtitle, const maage_pixel_buffer *image, int selected, int enabled)
{
    int clicked = maage_ui_button(ui, id, rect, "", MAAGE_UI_CARD, enabled);
    float inset = image ? 68 : 16;
    if (image) maage_ui_image(ui, maage_ui_rect(rect.x + 12, rect.y + 14, 44, 44), image);
    if (selected) maage_ui_fill(ui, maage_ui_rect(rect.x, rect.y, 3, rect.height), ui->theme.accent);
    maage_ui_label(ui, maage_ui_rect(rect.x + inset, rect.y + 10, rect.width - inset - 16, 29), 18, ui->theme.text, title);
    maage_ui_label(ui, maage_ui_rect(rect.x + inset, rect.y + 41, rect.width - inset - 16, 27), ui->theme.small_size, ui->theme.muted, subtitle);
    return clicked;
}
static float maage_ui_text_block(maage_launcher_ui *ui, maage_rect rect, float size, const char *text, int draw, maage_color color)
{
    char line[256];
    size_t length = 0, word, bytes;
    float y = rect.y, advance = size + 9;
    const unsigned char *cursor = (const unsigned char *)text;
    if (!text || rect.width <= 0) return 0;
    while (*cursor) {
        if (*cursor == '\n') {
            if (length && draw) maage_ui_label(ui, maage_ui_rect(rect.x, y, rect.width, advance), size, color, line);
            length = 0; y += advance; ++cursor;
            continue;
        }
        if (*cursor == ' ') { ++cursor; continue; }
        word = 0;
        while (cursor[word] && cursor[word] != ' ' && cursor[word] != '\n') {
            bytes = cursor[word] < 0x80 ? 1 : cursor[word] < 0xe0 ? 2 : cursor[word] < 0xf0 ? 3 : 4;
            if (word + bytes > 120) break;
            word += bytes;
        }
        if (length + word + 2 >= sizeof(line)) {
            if (draw) maage_ui_label(ui, maage_ui_rect(rect.x, y, rect.width, advance), size, color, line);
            length = 0; y += advance;
        }
        memcpy(line + length, cursor, word); line[length + word] = '\0';
        if (length && maage_canvas_text_width(ui->canvas, size, line) > rect.width) {
            line[length - 1] = '\0';
            if (draw) maage_ui_label(ui, maage_ui_rect(rect.x, y, rect.width, advance), size, color, line);
            y += advance; length = 0;
            memcpy(line, cursor, word);
        }
        length += word;
        line[length++] = ' '; line[length] = '\0';
        cursor += word;
    }
    if (length) {
        if (draw) maage_ui_label(ui, maage_ui_rect(rect.x, y, rect.width, advance), size, color, line);
        y += advance;
    }
    return y - rect.y;
}
float maage_ui_paragraph_height(maage_launcher_ui *ui, float width, float size, const char *text)
{
    return maage_ui_text_block(ui, maage_ui_rect(0, 0, width, 0), size, text, 0, ui->theme.muted);
}
void maage_ui_paragraph(maage_launcher_ui *ui, maage_rect rect, float size, const char *text)
{
    maage_ui_clip_push(ui, rect);
    maage_ui_text_block(ui, rect, size, text, 1, ui->theme.muted);
    maage_ui_clip_pop(ui);
}
void maage_ui_paragraph_color(maage_launcher_ui *ui, maage_rect rect, float size, maage_color color, const char *text)
{
    maage_ui_clip_push(ui, rect);
    maage_ui_text_block(ui, rect, size, text, 1, color);
    maage_ui_clip_pop(ui);
}
