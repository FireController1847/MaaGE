#include "maage/canvas.h"

/* No substitute font renderer or successful drawing no-ops on unsupported hosts. */
maage_render_result maage_canvas_create(maage_canvas **canvas)
{
    if (!canvas) return MAAGE_RENDER_INVALID_ARGUMENT;
    *canvas = NULL;
    return MAAGE_RENDER_UNSUPPORTED;
}

void maage_canvas_destroy(maage_canvas *canvas)
{
    (void)canvas;
}

maage_render_result maage_canvas_resize(maage_canvas *canvas, int width, int height, float scale)
{
    (void)canvas; (void)width; (void)height; (void)scale;
    return MAAGE_RENDER_UNSUPPORTED;
}

void maage_canvas_clip(maage_canvas *canvas, maage_rect rect)
{
    (void)canvas; (void)rect;
}

maage_render_result maage_canvas_rect(maage_canvas *canvas, maage_rect rect, maage_color color)
{
    (void)canvas; (void)rect; (void)color;
    return MAAGE_RENDER_UNSUPPORTED;
}

maage_render_result maage_canvas_triangle(maage_canvas *canvas, const float points[6], maage_color color)
{
    (void)canvas; (void)points; (void)color;
    return MAAGE_RENDER_UNSUPPORTED;
}

maage_render_result maage_canvas_text(maage_canvas *canvas, float x, float y, float size, maage_color color, const char *utf8)
{
    (void)canvas; (void)x; (void)y; (void)size; (void)color; (void)utf8;
    return MAAGE_RENDER_UNSUPPORTED;
}

float maage_canvas_text_width(maage_canvas *canvas, float size, const char *utf8)
{
    (void)canvas; (void)size; (void)utf8;
    return 0;
}

maage_render_result maage_canvas_image(maage_canvas *canvas, maage_rect rect, const maage_pixel_buffer *image)
{
    (void)canvas; (void)rect; (void)image;
    return MAAGE_RENDER_UNSUPPORTED;
}

const maage_pixel_buffer *maage_canvas_pixels(const maage_canvas *canvas)
{
    (void)canvas;
    return NULL;
}
