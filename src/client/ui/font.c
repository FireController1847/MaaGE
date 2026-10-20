#include "internal.h"

void maage_client_ui_font_initialize(maage_client_ui_resources *resources, const maage_image *font)
{
    int character, x, y, width;
    const unsigned char *pixel;
    for (character = 0; character < 256; ++character) {
        width = 0;
        for (y = 0; y < 8; ++y) for (x = 0; x < 8; ++x) {
            pixel = font->pixels + (size_t)((character / 16) * 8 + y) * font->stride +
                (size_t)((character % 16) * 8 + x) * 4;
            if (pixel[3] && x + 1 > width) width = x + 1;
        }
        resources->glyph_width[character] = (unsigned char)(width + 1);
    }
    resources->glyph_width[' '] = 4;
}

float maage_client_ui_text_width(const maage_client_ui *ui, const char *text)
{
    unsigned int i, character;
    float width = 0;
    if (!ui || !text) return 0;
    for (i = 0; i + 1 < MAAGE_CLIENT_UI_DEBUG_CHARACTERS && text[i]; ++i) {
        character = (unsigned char)text[i];
        if (character < 32 || character > 126) character = '?';
        width += ui->resources.glyph_width[character];
    }
    return width;
}

void maage_client_ui_font_draw(maage_client_ui *ui, const char *text,
    float x, float y, float red, float green, float blue)
{
    maage_client_ui_rect rectangle;
    unsigned int i, character, width;
    float u, v;
    if (!text) return;
    rectangle.y = y; rectangle.height = 8;
    for (i = 0; i + 1 < MAAGE_CLIENT_UI_DEBUG_CHARACTERS && text[i]; ++i) {
        character = (unsigned char)text[i];
        if (character < 32 || character > 126) character = '?';
        width = ui->resources.glyph_width[character];
        if (character != ' ' && width > 1) {
            rectangle.x = x; rectangle.width = (float)(width - 1);
            u = (float)((character % 16) * 8) / MAAGE_CLIENT_UI_ATLAS_EDGE;
            v = (float)(256 + (character / 16) * 8) / MAAGE_CLIENT_UI_ATLAS_EDGE;
            maage_client_ui_rectangle(ui, rectangle, u, v,
                u + rectangle.width / MAAGE_CLIENT_UI_ATLAS_EDGE, v + 8.0f / MAAGE_CLIENT_UI_ATLAS_EDGE,
                red, green, blue, 1);
        }
        x += width;
    }
}
