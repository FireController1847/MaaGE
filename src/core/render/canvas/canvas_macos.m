#include "maage/canvas.h"
#include <CoreGraphics/CoreGraphics.h>
#include <CoreText/CoreText.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>

struct maage_canvas {
    CGContextRef context;
    CGColorSpaceRef space;
    maage_pixel_buffer buffer;
    float scale;
    maage_rect clip;
    CTFontRef fonts[8];
    float sizes[8];
    unsigned int next_font;
};
maage_render_result maage_canvas_create(maage_canvas **output)
{
    maage_canvas *canvas;
    if (!output) return MAAGE_RENDER_INVALID_ARGUMENT;
    *output = NULL;
    canvas = calloc(1, sizeof(*canvas));
    if (!canvas) return MAAGE_RENDER_OUT_OF_MEMORY;
    canvas->space = CGColorSpaceCreateDeviceRGB();
    if (!canvas->space) { free(canvas); return MAAGE_RENDER_OUT_OF_MEMORY; }
    *output = canvas;
    return MAAGE_RENDER_OK;
}
void maage_canvas_destroy(maage_canvas *canvas)
{
    unsigned int index;
    if (!canvas) return;
    if (canvas->context) CGContextRelease(canvas->context);
    for (index = 0; index < 8; ++index) if (canvas->fonts[index]) CFRelease(canvas->fonts[index]);
    CGColorSpaceRelease(canvas->space);
    free(canvas);
}
maage_render_result maage_canvas_resize(maage_canvas *canvas, int width, int height, float scale)
{
    CGContextRef context;
    if (!canvas || width <= 0 || height <= 0 || !(scale > 0 && scale <= 8) || width > INT_MAX / 4 ||
        (size_t)height > (size_t)-1 / ((size_t)width * 4)) return MAAGE_RENDER_INVALID_ARGUMENT;
    if (canvas->context && canvas->buffer.width == width && canvas->buffer.height == height && canvas->scale == scale)
        return MAAGE_RENDER_OK;
    context = CGBitmapContextCreate(NULL, width, height, 8, (size_t)width * 4, canvas->space,
        kCGImageAlphaPremultipliedLast | kCGBitmapByteOrder32Big);
    if (!context) return MAAGE_RENDER_OUT_OF_MEMORY;
    if (canvas->context) CGContextRelease(canvas->context);
    canvas->context = context;
    canvas->buffer.width = width; canvas->buffer.height = height;
    canvas->buffer.stride = (size_t)width * 4;
    canvas->buffer.pixels = CGBitmapContextGetData(context);
    canvas->scale = scale;
    /* Start opaque; source-over drawing preserves straight/premultiplied parity. */
    CGContextSetRGBFillColor(context, 0, 0, 0, 1);
    CGContextFillRect(context, CGRectMake(0, 0, width, height));
    CGContextTranslateCTM(context, 0, height);
    CGContextScaleCTM(context, scale, -scale);
    canvas->clip = (maage_rect){0, 0, width / scale, height / scale};
    return MAAGE_RENDER_OK;
}
void maage_canvas_clip(maage_canvas *canvas, maage_rect rect) { if (canvas) canvas->clip = rect; }
static void maage_canvas_begin(maage_canvas *canvas, maage_color color)
{
    CGContextSaveGState(canvas->context);
    CGContextClipToRect(canvas->context, CGRectMake(canvas->clip.x, canvas->clip.y, canvas->clip.width, canvas->clip.height));
    CGContextSetRGBFillColor(canvas->context, color.r / 255.0, color.g / 255.0, color.b / 255.0, color.a / 255.0);
}
maage_render_result maage_canvas_rect(maage_canvas *canvas, maage_rect rect, maage_color color)
{
    if (!canvas || !canvas->context) return MAAGE_RENDER_INVALID_ARGUMENT;
    maage_canvas_begin(canvas, color);
    CGContextFillRect(canvas->context, CGRectMake(rect.x, rect.y, rect.width, rect.height));
    CGContextRestoreGState(canvas->context);
    return MAAGE_RENDER_OK;
}
maage_render_result maage_canvas_triangle(maage_canvas *canvas, const float points[6], maage_color color)
{
    if (!canvas || !canvas->context || !points) return MAAGE_RENDER_INVALID_ARGUMENT;
    maage_canvas_begin(canvas, color);
    CGContextBeginPath(canvas->context);
    CGContextMoveToPoint(canvas->context, points[0], points[1]);
    CGContextAddLineToPoint(canvas->context, points[2], points[3]);
    CGContextAddLineToPoint(canvas->context, points[4], points[5]);
    CGContextClosePath(canvas->context);
    CGContextFillPath(canvas->context);
    CGContextRestoreGState(canvas->context);
    return MAAGE_RENDER_OK;
}
static CTFontRef maage_canvas_font(maage_canvas *canvas, float size)
{
    unsigned int index;
    if (!(size >= 6 && size <= 128)) return NULL;
    for (index = 0; index < 8; ++index) if (canvas->fonts[index] && canvas->sizes[index] == size) return canvas->fonts[index];
    index = canvas->next_font++ % 8;
    if (canvas->fonts[index]) CFRelease(canvas->fonts[index]);
    canvas->fonts[index] = CTFontCreateUIFontForLanguage(kCTFontUIFontSystem, size, NULL);
    canvas->sizes[index] = size;
    return canvas->fonts[index];
}
static CTLineRef maage_canvas_line(maage_canvas *canvas, float size, const char *utf8)
{
    CTFontRef font = maage_canvas_font(canvas, size);
    CFStringRef text;
    CFDictionaryRef attributes;
    CFAttributedStringRef styled;
    CTLineRef line;
    const void *keys[] = { kCTFontAttributeName, kCTForegroundColorFromContextAttributeName };
    const void *values[] = { font, kCFBooleanTrue };
    if (!font || !utf8) return NULL;
    text = CFStringCreateWithCString(NULL, utf8, kCFStringEncodingUTF8);
    if (!text) return NULL;
    attributes = CFDictionaryCreate(NULL, keys, values, 2, &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
    styled = attributes ? CFAttributedStringCreate(NULL, text, attributes) : NULL;
    line = styled ? CTLineCreateWithAttributedString(styled) : NULL;
    if (styled) CFRelease(styled);
    if (attributes) CFRelease(attributes);
    CFRelease(text);
    return line;
}
float maage_canvas_text_width(maage_canvas *canvas, float size, const char *utf8)
{
    CTLineRef line;
    float width;
    if (!canvas) return 0;
    line = maage_canvas_line(canvas, size, utf8);
    if (!line) return 0;
    width = (float)CTLineGetTypographicBounds(line, NULL, NULL, NULL);
    CFRelease(line);
    return width;
}
maage_render_result maage_canvas_text(maage_canvas *canvas, float x, float y, float size, maage_color color, const char *utf8)
{
    CTLineRef line;
    if (!canvas || !canvas->context) return MAAGE_RENDER_INVALID_ARGUMENT;
    line = maage_canvas_line(canvas, size, utf8);
    if (!line) return MAAGE_RENDER_INVALID_ARGUMENT;
    maage_canvas_begin(canvas, color);
    CGContextTranslateCTM(canvas->context, x, y + CTFontGetAscent(maage_canvas_font(canvas, size)));
    CGContextScaleCTM(canvas->context, 1, -1);
    CGContextSetTextMatrix(canvas->context, CGAffineTransformIdentity);
    CGContextSetTextPosition(canvas->context, 0, 0);
    CTLineDraw(line, canvas->context);
    CGContextRestoreGState(canvas->context);
    CFRelease(line);
    return MAAGE_RENDER_OK;
}
maage_render_result maage_canvas_image(maage_canvas *canvas, maage_rect rect, const maage_pixel_buffer *buffer)
{
    CGDataProviderRef provider;
    CGImageRef image;
    maage_color unused = {0, 0, 0, 255};
    if (!canvas || !canvas->context || !buffer || !buffer->pixels || buffer->width <= 0 || buffer->height <= 0 ||
        (size_t)buffer->width > (size_t)-1 / 4 || buffer->stride < (size_t)buffer->width * 4 ||
        buffer->stride > (size_t)-1 / (size_t)buffer->height) return MAAGE_RENDER_INVALID_ARGUMENT;
    provider = CGDataProviderCreateWithData(NULL, buffer->pixels, buffer->stride * (size_t)buffer->height, NULL);
    if (!provider) return MAAGE_RENDER_OUT_OF_MEMORY;
    image = CGImageCreate(buffer->width, buffer->height, 8, 32, buffer->stride, canvas->space,
        kCGImageAlphaLast | kCGBitmapByteOrder32Big, provider, NULL, true, kCGRenderingIntentDefault);
    CGDataProviderRelease(provider);
    if (!image) return MAAGE_RENDER_INVALID_ARGUMENT;
    maage_canvas_begin(canvas, unused);
    CGContextTranslateCTM(canvas->context, rect.x, rect.y + rect.height);
    CGContextScaleCTM(canvas->context, 1, -1);
    CGContextDrawImage(canvas->context, CGRectMake(0, 0, rect.width, rect.height), image);
    CGContextRestoreGState(canvas->context);
    CGImageRelease(image);
    return MAAGE_RENDER_OK;
}
const maage_pixel_buffer *maage_canvas_pixels(const maage_canvas *canvas)
{
    return canvas && canvas->context ? &canvas->buffer : NULL;
}
