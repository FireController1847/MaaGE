#include "internal.h"

#include <stdlib.h>
#include <string.h>

/* Original minimal 5x7 glyphs keep menus/log cross-references readable when
 * the optional external ASCII font is unavailable. Lowercase reuses capitals. */
maage_status maage_client_ui_font_fallback(maage_image *font) {
    static const unsigned char letters[26][7] = {
        {14, 17, 17, 31, 17, 17, 17}, {30, 17, 17, 30, 17, 17, 30}, {14, 17, 16, 16, 16, 17, 14}, {30, 17, 17, 17, 17, 17, 30}, {31, 16, 16, 30, 16, 16, 31}, {31, 16, 16, 30, 16, 16, 16}, {14, 17, 16, 23, 17, 17, 15}, {17, 17, 17, 31, 17, 17, 17}, {14, 4, 4, 4, 4, 4, 14}, {7, 2, 2, 2, 18, 18, 12}, {17, 18, 20, 24, 20, 18, 17}, {16, 16, 16, 16, 16, 16, 31}, {17, 27, 21, 21, 17, 17, 17}, {17, 25, 21, 19, 17, 17, 17}, {14, 17, 17, 17, 17, 17, 14}, {30, 17, 17, 30, 16, 16, 16}, {14, 17, 17, 17, 21, 18, 13}, {30, 17, 17, 30, 20, 18, 17}, {15, 16, 16, 14, 1, 1, 30}, {31, 4, 4, 4, 4, 4, 4}, {17, 17, 17, 17, 17, 17, 14}, {17, 17, 17, 17, 17, 10, 4}, {17, 17, 17, 21, 21, 21, 10}, {17, 17, 10, 4, 10, 17, 17}, {17, 17, 10, 4, 4, 4, 4}, {31, 1, 2, 4, 8, 16, 31}};
    static const unsigned char digits[10][7] = {
        {14, 17, 19, 21, 25, 17, 14}, {4, 12, 4, 4, 4, 4, 14}, {14, 17, 1, 2, 4, 8, 31}, {30, 1, 1, 14, 1, 1, 30}, {2, 6, 10, 18, 31, 2, 2}, {31, 16, 16, 30, 1, 1, 30}, {14, 16, 16, 30, 17, 17, 14}, {31, 1, 2, 4, 8, 8, 8}, {14, 17, 17, 14, 17, 17, 14}, {14, 17, 17, 15, 1, 1, 14}};
    static const char punctuation[] = ".,:;-_+/\\=!?()[]<>|%#*\"'";
    static const unsigned char marks[][7] = {
        {0, 0, 0, 0, 0, 12, 12}, {0, 0, 0, 0, 4, 4, 8}, {0, 12, 12, 0, 12, 12, 0}, {0, 12, 12, 0, 4, 4, 8}, {0, 0, 0, 31, 0, 0, 0}, {0, 0, 0, 0, 0, 0, 31}, {0, 4, 4, 31, 4, 4, 0}, {1, 2, 2, 4, 8, 8, 16}, {16, 8, 8, 4, 2, 2, 1}, {0, 0, 31, 0, 31, 0, 0}, {4, 4, 4, 4, 4, 0, 4}, {14, 17, 1, 2, 4, 0, 4}, {2, 4, 8, 8, 8, 4, 2}, {8, 4, 2, 2, 2, 4, 8}, {14, 8, 8, 8, 8, 8, 14}, {14, 2, 2, 2, 2, 2, 14}, {2, 4, 8, 16, 8, 4, 2}, {8, 4, 2, 1, 2, 4, 8}, {4, 4, 4, 4, 4, 4, 4}, {25, 26, 2, 4, 8, 11, 19}, {10, 31, 10, 10, 31, 10, 0}, {0, 21, 14, 31, 14, 21, 0}, {10, 10, 0, 0, 0, 0, 0}, {4, 4, 0, 0, 0, 0, 0}};
    const unsigned char *rows;
    const char *mark;
    unsigned char *pixel;
    int character, letter, x, y;
    if (!font || font->pixels) return maage_status_make(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_CONTEXT_GUI, MAAGE_OPERATION_INITIALIZE, 0);
    font->width = font->height = 128;
    font->stride = 512;
    font->pixels = (unsigned char *) calloc(128, font->stride);
    if (!font->pixels) return maage_status_make(MAAGE_STATUS_ALLOCATION_FAILED, MAAGE_CONTEXT_GUI, MAAGE_OPERATION_INITIALIZE, 0);
    for (character = 33; character <= 126; ++character) {
        letter = character >= 'a' && character <= 'z' ? character - 'a' + 'A' : character;
        if (letter >= 'A' && letter <= 'Z') rows = letters[letter - 'A'];
        else if (letter >= '0' && letter <= '9') rows = digits[letter - '0'];
        else {
            mark = strchr(punctuation, character);
            rows = mark ? marks[mark - punctuation] : marks[11];
        }
        for (y = 0; y < 7; ++y)
            for (x = 0; x < 5; ++x)
                if (rows[y] & (16 >> x)) {
                    pixel = font->pixels + (size_t) ((character / 16) * 8 + y) * font->stride + (size_t) ((character % 16) * 8 + x) * 4;
                    memset(pixel, 255, 4);
                }
    }
    return maage_status_make(MAAGE_STATUS_SUCCESS, 0, MAAGE_OPERATION_INITIALIZE, 0);
}

maage_status maage_client_ui_font_initialize(maage_client_ui_resources *resources, const maage_image *font) {
    int character, x, y, width;
    const unsigned char *pixel;
    if (!resources || !font || !font->pixels || font->width != 128 || font->height != 128) return maage_status_make(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_CONTEXT_GUI, MAAGE_OPERATION_INITIALIZE, !resources ? 0 : 1);
    for (character = 0; character < 256; ++character) {
        width = 0;
        for (y = 0; y < 8; ++y)
            for (x = 0; x < 8; ++x) {
                pixel = font->pixels + (size_t) ((character / 16) * 8 + y) * font->stride + (size_t) ((character % 16) * 8 + x) * 4;
                if (pixel[3] && x + 1 > width) width = x + 1;
            }
        resources->glyph_width[character] = (unsigned char) (width + 1);
    }
    resources->glyph_width[' '] = 4;
    return maage_status_make(MAAGE_STATUS_SUCCESS, 0, MAAGE_OPERATION_INITIALIZE, 0);
}

float maage_client_ui_text_width(const maage_client_ui *ui, const char *text) {
    size_t i;
    unsigned int character;
    float width = 0;
    if (!ui || !text) return 0;
    for (i = 0; text[i]; ++i) {
        character = (unsigned char) text[i];
        if (character < 32 || character > 126) character = '?';
        width += ui->resources.glyph_width[character];
    }
    return width;
}

void maage_client_ui_font_draw(maage_client_ui *ui, const char *text, float x, float y, float red, float green, float blue) {
    maage_client_ui_rect rectangle;
    size_t i;
    unsigned int character, width;
    float u, v;
    if (!text) return;
    rectangle.y = y;
    rectangle.height = 8;
    for (i = 0; text[i]; ++i) {
        character = (unsigned char) text[i];
        if (character < 32 || character > 126) character = '?';
        width = ui->resources.glyph_width[character];
        if (character != ' ' && width > 1) {
            rectangle.x = x;
            rectangle.width = (float) (width - 1);
            u = (float) ((character % 16) * 8) / ui->resources.atlas_edge;
            v = (float) (256 + (character / 16) * 8) / ui->resources.atlas_edge;
            maage_client_ui_rectangle(ui, rectangle, u, v, u + rectangle.width / ui->resources.atlas_edge, v + 8.0f / ui->resources.atlas_edge, red, green, blue, 1);
        }
        x += width;
    }
}
