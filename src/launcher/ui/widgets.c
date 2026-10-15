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
    if (ui->focus_count < 128 && rect.y + rect.height > ui->clip.y && rect.y < ui->clip.y + ui->clip.height)
        ui->focus_ids[ui->focus_count++] = id;
    hot = maage_ui_contains(rect, ui->input.x, ui->input.y) && maage_ui_contains(ui->clip, ui->input.x, ui->input.y);
    if (hot && ui->input.pressed) { ui->active = id; ui->focus = id; }
    if (ui->input.released && ui->active == id) { clicked = hot; ui->active = 0; }
    if (ui->focus == id && ui->input.key == MAAGE_KEY_ENTER) { clicked = 1; ui->input.key = MAAGE_KEY_NONE; }
    return clicked;
}
int maage_ui_button(maage_launcher_ui *ui, int id, maage_rect rect, const char *text, int style, int enabled)
{
    maage_color color = ui->theme.navigation;
    float width, size = style == MAAGE_UI_TAB ? 16.0f : 15.0f;
    int clicked = maage_ui_hit(ui, id, rect, enabled);
    int hot = maage_ui_contains(rect, ui->input.x, ui->input.y) && maage_ui_contains(ui->clip, ui->input.x, ui->input.y);
    if (style == MAAGE_UI_CARD) color = ui->theme.panel;
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
    if (ui->focus == id && enabled)
        maage_ui_fill(ui, maage_ui_rect(rect.x, rect.y + rect.height - 2, rect.width, 2), ui->theme.text);
    width = maage_canvas_text_width(ui->canvas, size, text);
    maage_ui_label(ui, maage_ui_rect(rect.x + (rect.width > width ? (rect.width - width) / 2 : 6),
        rect.y + (rect.height - size - 3) / 2, rect.width - 8, size + 8), size,
        enabled ? ui->theme.text : ui->theme.muted, text);
    return clicked;
}
void maage_ui_slider(maage_launcher_ui *ui, int id, maage_rect rect, float *value, float minimum, float maximum)
{
    float position;
    int dragging = ui->active == id;
    if (rect.width <= 0 || maximum <= minimum) return;
    maage_ui_hit(ui, id, rect, 1);
    if (!ui->blocked && (ui->active == id || (dragging && ui->input.released))) {
        position = (ui->input.x - rect.x) / rect.width;
        if (position < 0) position = 0;
        if (position > 1) position = 1;
        *value = minimum + position * (maximum - minimum);
    }
    if (!ui->blocked && ui->focus == id) {
        if (ui->input.key == MAAGE_KEY_LEFT) *value -= (maximum - minimum) / 40;
        if (ui->input.key == MAAGE_KEY_RIGHT) *value += (maximum - minimum) / 40;
    }
    if (*value < minimum) *value = minimum;
    if (*value > maximum) *value = maximum;
    position = (*value - minimum) / (maximum - minimum);
    maage_ui_fill(ui, maage_ui_rect(rect.x, rect.y + rect.height / 2 - 2, rect.width, 4), ui->theme.navigation);
    maage_ui_fill(ui, maage_ui_rect(rect.x, rect.y + rect.height / 2 - 2, rect.width * position, 4), ui->theme.accent);
    maage_ui_fill(ui, maage_ui_rect(rect.x + (rect.width - 14) * position, rect.y + 4, 14, rect.height - 8),
        ui->focus == id ? ui->theme.text : ui->theme.accent);
}
void maage_ui_text_input(maage_launcher_ui *ui, int id, maage_rect rect, char *text, size_t capacity)
{
    size_t length = strlen(text), index = 0, count, remaining = strlen(ui->input.text);
    const unsigned char *input = (const unsigned char *)ui->input.text;
    maage_ui_hit(ui, id, rect, 1);
    if (!ui->blocked && ui->focus == id) {
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
    maage_ui_label(ui, maage_ui_rect(rect.x + 12, rect.y + 10, rect.width - 24, 24), 16, ui->theme.text, text);
    if (ui->focus == id)
        maage_ui_fill(ui, maage_ui_rect(rect.x, rect.y + rect.height - 2, rect.width, 2), ui->theme.accent);
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
    if (!ui->blocked && maage_ui_contains(rect, ui->input.x, ui->input.y)) *offset -= ui->input.scroll * 5;
    if (*offset < 0) *offset = 0;
    if (*offset > maximum) *offset = maximum;
    if (maximum > 0) {
        maage_ui_fill(ui, maage_ui_rect(rect.x + rect.width - 3, rect.y + (*offset / maximum) * (rect.height - 36), 3, 36), ui->theme.hover);
    }
    maage_ui_clip_push(ui, rect);
    rect.y -= *offset;
    rect.height = content_height;
    return rect;
}
void maage_ui_image(maage_launcher_ui *ui, maage_rect rect, int index)
{
    maage_pixel_buffer image;
    image.pixels = ui->art[index % 4]; image.width = image.height = 32; image.stride = 128;
    maage_canvas_image(ui->canvas, rect, &image);
}
void maage_ui_hero(maage_launcher_ui *ui, maage_rect rect, int variant)
{
    int index;
    float points[6], x;
    maage_color color;
    maage_ui_clip_push(ui, rect);
    for (index = 0; index < 40; ++index) {
        color.r = (unsigned char)(22 + index / 3); color.g = (unsigned char)(28 + index / 2);
        color.b = (unsigned char)(45 + index); color.a = 255;
        maage_ui_fill(ui, maage_ui_rect(rect.x, rect.y + rect.height * index / 40, rect.width, rect.height / 40 + 1), color);
    }
    color.r = 192; color.g = 65; color.b = 143;
    maage_ui_fill(ui, maage_ui_rect(rect.x + rect.width * 0.72f, rect.y + rect.height * 0.18f, 44, 44), color);
    for (index = 0; index < 7; ++index) {
        x = rect.x + rect.width * index / 6;
        points[0] = x - rect.width / 4; points[1] = rect.y + rect.height;
        points[2] = x; points[3] = rect.y + rect.height * (0.3f + ((index + variant) % 3) * 0.14f);
        points[4] = x + rect.width / 4; points[5] = rect.y + rect.height;
        color.r = (unsigned char)(18 + index * 2); color.g = (unsigned char)(25 + index * 2); color.b = (unsigned char)(34 + index * 3);
        maage_canvas_triangle(ui->canvas, points, color);
    }
    maage_ui_clip_pop(ui);
}
void maage_ui_paragraph(maage_launcher_ui *ui, maage_rect rect, float size, const char *text)
{
    char line[256];
    size_t length = 0, word;
    float y = rect.y;
    const char *cursor = text;
    while (*cursor && y + size < rect.y + rect.height) {
        word = 0;
        while (cursor[word] && cursor[word] != ' ' && cursor[word] != '\n' && word < 120) ++word;
        if (length && (length + word + 2 >= sizeof(line) || *cursor == '\n')) {
            maage_ui_label(ui, maage_ui_rect(rect.x, y, rect.width, size + 8), size, ui->theme.muted, line);
            length = 0; y += size + 10;
        }
        if (*cursor == '\n') { ++cursor; continue; }
        if (word == 0) { ++cursor; continue; }
        memcpy(line + length, cursor, word); line[length + word] = '\0';
        if (length && maage_canvas_text_width(ui->canvas, size, line) > rect.width) {
            line[length - 1] = '\0';
            maage_ui_label(ui, maage_ui_rect(rect.x, y, rect.width, size + 8), size, ui->theme.muted, line);
            y += size + 10; length = 0;
            memcpy(line, cursor, word);
        }
        length += word;
        line[length++] = ' '; line[length] = '\0';
        cursor += word;
        if (*cursor == ' ') ++cursor;
    }
    if (length) maage_ui_label(ui, maage_ui_rect(rect.x, y, rect.width, size + 8), size, ui->theme.muted, line);
}
