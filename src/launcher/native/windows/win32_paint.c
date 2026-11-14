#define COBJMACROS
#define WIN32_LEAN_AND_MEAN
#include "win32_paint.h"
#include <objbase.h>
#include <wincodec.h>
#include <math.h>
#include <string.h>
#include <stdlib.h>

void maage_win32_surface_destroy(maage_win32_surface *s) {
    if (s->dc && s->previous) SelectObject(s->dc, s->previous);
    if (s->bitmap) DeleteObject(s->bitmap);
    if (s->dc) DeleteDC(s->dc);
    memset(s, 0, sizeof(*s));
}

int maage_win32_surface_resize(maage_win32_surface *s, HDC compatible, int width, int height) {
    maage_win32_surface next;
    BITMAPINFO info;
    if (width <= 0 || height <= 0) return 0;
    if (s->dc && s->width == width && s->height == height) return 1;
    memset(&next, 0, sizeof(next));
    memset(&info, 0, sizeof(info));
    info.bmiHeader.biSize = sizeof(info.bmiHeader);
    info.bmiHeader.biWidth = width;
    info.bmiHeader.biHeight = -height;
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;
    next.dc = CreateCompatibleDC(compatible);
    if (next.dc) next.bitmap = CreateDIBSection(next.dc, &info, DIB_RGB_COLORS, (void **) &next.pixels, NULL, 0);
    if (!next.bitmap) {
        maage_win32_surface_destroy(&next);
        return 0;
    }
    next.previous = (HBITMAP) SelectObject(next.dc, next.bitmap);
    if (!next.previous || next.previous == (HBITMAP) HGDI_ERROR) {
        next.previous = NULL;
        maage_win32_surface_destroy(&next);
        return 0;
    }
    next.width = width;
    next.height = height;
    maage_win32_surface_destroy(s);
    *s = next;
    return 1;
}

/* Decode the packaged transparent PNG with the native Windows Imaging
 * Component. The ICO is still used for the executable and window chrome. */
int maage_win32_surface_png(maage_win32_surface *s, HINSTANCE instance, int resource) {
    IWICImagingFactory *factory = NULL;
    IWICStream *stream = NULL;
    IWICBitmapDecoder *decoder = NULL;
    IWICBitmapFrameDecode *frame = NULL;
    IWICFormatConverter *converter = NULL;
    HRSRC location = FindResourceW(instance, MAKEINTRESOURCEW(resource), MAKEINTRESOURCEW(10));
    HGLOBAL data = location ? LoadResource(instance, location) : NULL;
    BYTE *bytes = data ? (BYTE *) LockResource(data) : NULL;
    DWORD length = location ? SizeofResource(instance, location) : 0;
    UINT width = 0, height = 0;
    HRESULT initialized, result;
    int success = 0;
    if (!bytes || !length) return 0;
    initialized = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    if (FAILED(initialized) && initialized != RPC_E_CHANGED_MODE) return 0;
    result = CoCreateInstance(&CLSID_WICImagingFactory, NULL, CLSCTX_INPROC_SERVER, &IID_IWICImagingFactory, (void **) &factory);
    if (SUCCEEDED(result)) result = IWICImagingFactory_CreateStream(factory, &stream);
    if (SUCCEEDED(result)) result = IWICStream_InitializeFromMemory(stream, bytes, length);
    if (SUCCEEDED(result)) result = IWICImagingFactory_CreateDecoderFromStream(factory, (IStream *) stream, NULL, WICDecodeMetadataCacheOnLoad, &decoder);
    if (SUCCEEDED(result)) result = IWICBitmapDecoder_GetFrame(decoder, 0, &frame);
    if (SUCCEEDED(result)) result = IWICImagingFactory_CreateFormatConverter(factory, &converter);
    if (SUCCEEDED(result)) result = IWICFormatConverter_Initialize(converter, (IWICBitmapSource *) frame, &GUID_WICPixelFormat32bppPBGRA, WICBitmapDitherTypeNone, NULL, 0.0, WICBitmapPaletteTypeCustom);
    if (SUCCEEDED(result)) result = IWICFormatConverter_GetSize(converter, &width, &height);
    if (SUCCEEDED(result) && width && height && width <= 2048 && height <= 2048 && maage_win32_surface_resize(s, NULL, (int) width, (int) height)) {
        GdiFlush();
        result = IWICFormatConverter_CopyPixels(converter, NULL, width * 4, width * height * 4, (BYTE *) s->pixels);
        success = SUCCEEDED(result);
    }
    if (converter) IWICFormatConverter_Release(converter);
    if (frame) IWICBitmapFrameDecode_Release(frame);
    if (decoder) IWICBitmapDecoder_Release(decoder);
    if (stream) IWICStream_Release(stream);
    if (factory) IWICImagingFactory_Release(factory);
    if (SUCCEEDED(initialized)) CoUninitialize();
    if (!success) maage_win32_surface_destroy(s);
    return success;
}

/* Two separable box-blur passes turn the PNG alpha into a soft silhouette.
 * The shadow uses the same bounds and scale as the original artwork. */
int maage_win32_surface_shadow(maage_win32_surface *s, const maage_win32_surface *source) {
    int width = source->width, height = source->height, radius = width / 32, x, y, k, pass;
    size_t count = (size_t) width * height;
    unsigned char *a, *b;
    if (!source->pixels || !count || radius < 1) return 0;
    a = (unsigned char *) malloc(count);
    b = (unsigned char *) malloc(count);
    if (!a || !b || !maage_win32_surface_resize(s, NULL, width, height)) {
        free(a);
        free(b);
        return 0;
    }
    GdiFlush();
    for (y = 0; y < height; ++y)
        for (x = 0; x < width; ++x) a[(size_t) y * width + x] = (unsigned char) (source->pixels[(size_t) y * width + x] >> 24);
    for (pass = 0; pass < 2; ++pass) {
        for (y = 0; y < height; ++y) {
            unsigned sum = 0;
            for (k = 0; k <= radius && k < width; ++k) sum += a[(size_t) y * width + k];
            for (x = 0; x < width; ++x) {
                b[(size_t) y * width + x] = (unsigned char) (sum / (2 * radius + 1));
                if (x - radius >= 0) sum -= a[(size_t) y * width + x - radius];
                if (x + radius + 1 < width) sum += a[(size_t) y * width + x + radius + 1];
            }
        }
        for (x = 0; x < width; ++x) {
            unsigned sum = 0;
            for (k = 0; k <= radius && k < height; ++k) sum += b[(size_t) k * width + x];
            for (y = 0; y < height; ++y) {
                a[(size_t) y * width + x] = (unsigned char) (sum / (2 * radius + 1));
                if (y - radius >= 0) sum -= b[(size_t) (y - radius) * width + x];
                if (y + radius + 1 < height) sum += b[(size_t) (y + radius + 1) * width + x];
            }
        }
    }
    for (y = 0; y < height; ++y)
        for (x = 0; x < width; ++x) s->pixels[(size_t) y * width + x] = (DWORD) (a[(size_t) y * width + x] * 45 / 100) << 24;
    free(a);
    free(b);
    return 1;
}

void maage_win32_surface_image(maage_win32_surface *s, const maage_win32_surface *image, RECT rect) {
    BLENDFUNCTION blend = {AC_SRC_OVER, 0, 255, AC_SRC_ALPHA};
    if (!image->dc || rect.right <= rect.left || rect.bottom <= rect.top) return;
    AlphaBlend(s->dc, rect.left, rect.top, rect.right - rect.left, rect.bottom - rect.top, image->dc, 0, 0, image->width, image->height, blend);
}

static DWORD maage_win32_bgra(COLORREF color) {
    return 0xff000000UL | ((DWORD) GetRValue(color) << 16) | ((DWORD) GetGValue(color) << 8) | GetBValue(color);
}

void maage_win32_surface_gradient(maage_win32_surface *s, COLORREF top, COLORREF bottom) {
    int x, y, d = s->height > 1 ? s->height - 1 : 1;
    GdiFlush();
    for (y = 0; y < s->height; ++y) {
        DWORD pixel = maage_win32_bgra(RGB((GetRValue(top) * (d - y) + GetRValue(bottom) * y) / d, (GetGValue(top) * (d - y) + GetGValue(bottom) * y) / d, (GetBValue(top) * (d - y) + GetBValue(bottom) * y) / d));
        DWORD *row = s->pixels + (size_t) y * s->width;
        for (x = 0; x < s->width; ++x) row[x] = pixel;
    }
}

static void maage_win32_blend(DWORD *pixel, COLORREF color, double coverage) {
    unsigned a, inverse, r, g, b;
    DWORD old;
    if (coverage <= 0.0) return;
    if (coverage >= 1.0) {
        *pixel = maage_win32_bgra(color);
        return;
    }
    a = (unsigned) (coverage * 255.0 + 0.5);
    inverse = 255 - a;
    old = *pixel;
    r = (GetRValue(color) * a + ((old >> 16) & 255) * inverse + 127) / 255;
    g = (GetGValue(color) * a + ((old >> 8) & 255) * inverse + 127) / 255;
    b = (GetBValue(color) * a + (old & 255) * inverse + 127) / 255;
    *pixel = 0xff000000UL | ((DWORD) r << 16) | ((DWORD) g << 8) | b;
}

static double maage_win32_clamp(double v, double lo, double hi) {
    return v < lo ? lo : v > hi ? hi : v;
}

/* Signed distance gives smooth corners and soft shadows even across
 * separate native child hit targets sharing one card outline. */
void maage_win32_surface_round(maage_win32_surface *s, RECT rect, double radius, COLORREF color, double opacity, double blur) {
    double hw = (rect.right - rect.left) * 0.5, hh = (rect.bottom - rect.top) * 0.5;
    double cx = rect.left + hw, cy = rect.top + hh;
    int x, y, left, right, top, bottom, margin;
    if (hw <= 0 || hh <= 0 || opacity <= 0.0) return;
    radius = maage_win32_clamp(radius, 0.0, hw < hh ? hw : hh);
    margin = (int) ceil(blur + 1.0);
    left = rect.left - margin;
    right = rect.right + margin;
    top = rect.top - margin;
    bottom = rect.bottom + margin;
    if (left < 0) left = 0;
    if (top < 0) top = 0;
    if (right > s->width) right = s->width;
    if (bottom > s->height) bottom = s->height;
    GdiFlush();
    for (y = top; y < bottom; ++y) {
        double dy = fabs(y + 0.5 - cy) - (hh - radius);
        DWORD *row = s->pixels + (size_t) y * s->width;
        for (x = left; x < right; ++x) {
            double dx = fabs(x + 0.5 - cx) - (hw - radius);
            double distance = dx > 0 && dy > 0 ? sqrt(dx * dx + dy * dy) - radius : (dx > dy ? dx : dy) - radius;
            double coverage = blur > 0 ? maage_win32_clamp(1.0 - (distance + blur * 0.35) / blur, 0.0, 1.0) : maage_win32_clamp(0.5 - distance, 0.0, 1.0);
            if (blur > 0) coverage = coverage * coverage * (3.0 - 2.0 * coverage);
            maage_win32_blend(&row[x], color, opacity * coverage);
        }
    }
}

void maage_win32_surface_line(maage_win32_surface *s, double x1, double y1, double x2, double y2, double thickness, COLORREF color) {
    double dx = x2 - x1, dy = y2 - y1, length = dx * dx + dy * dy, radius = thickness * 0.5;
    int x, y, left = (int) floor((x1 < x2 ? x1 : x2) - radius - 1), right = (int) ceil((x1 > x2 ? x1 : x2) + radius + 1);
    int top = (int) floor((y1 < y2 ? y1 : y2) - radius - 1), bottom = (int) ceil((y1 > y2 ? y1 : y2) + radius + 1);
    if (left < 0) left = 0;
    if (top < 0) top = 0;
    if (right > s->width) right = s->width;
    if (bottom > s->height) bottom = s->height;
    GdiFlush();
    for (y = top; y < bottom; ++y) {
        DWORD *row = s->pixels + (size_t) y * s->width;
        for (x = left; x < right; ++x) {
            double px = x + 0.5 - x1, py = y + 0.5 - y1;
            double t = length > 0 ? maage_win32_clamp((px * dx + py * dy) / length, 0.0, 1.0) : 0.0;
            double sx = px - t * dx, sy = py - t * dy;
            maage_win32_blend(&row[x], color, maage_win32_clamp(radius + 0.5 - sqrt(sx * sx + sy * sy), 0.0, 1.0));
        }
    }
}
