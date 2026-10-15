#ifndef MAAGE_CANVAS_H
#define MAAGE_CANVAS_H
#include "maage/render.h"

typedef struct maage_canvas maage_canvas;
typedef struct maage_rect { float x, y, width, height; } maage_rect;
typedef struct maage_color { unsigned char r, g, b, a; } maage_color;

/* Owned, opaque RGBA8 drawing surface. Coordinates and type sizes are logical
 * points, with a top-left origin; scale maps them to backing pixels. Images are
 * borrowed only for the draw call. Canvas pixels live until resize/destruction.
 * Native font/drawing implementation currently exists on macOS only; other
 * platforms return UNSUPPORTED. This is graphics infrastructure, not widgets. */
maage_render_result maage_canvas_create(maage_canvas **canvas);
void maage_canvas_destroy(maage_canvas *canvas);
maage_render_result maage_canvas_resize(maage_canvas *canvas, int width, int height, float scale);
void maage_canvas_clip(maage_canvas *canvas, maage_rect rect);
maage_render_result maage_canvas_rect(maage_canvas *canvas, maage_rect rect, maage_color color);
maage_render_result maage_canvas_triangle(maage_canvas *canvas, const float points[6], maage_color color);
maage_render_result maage_canvas_text(maage_canvas *canvas, float x, float y, float size,
    maage_color color, const char *utf8);
float maage_canvas_text_width(maage_canvas *canvas, float size, const char *utf8);
maage_render_result maage_canvas_image(maage_canvas *canvas, maage_rect rect, const maage_pixel_buffer *image);
const maage_pixel_buffer *maage_canvas_pixels(const maage_canvas *canvas);
#endif
