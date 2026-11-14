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

/* Let GDI fill the entire background in one operation, rather than writing
 * millions of pixels from the unoptimized Debug build on every resize. */
void maage_win32_surface_gradient_rect(maage_win32_surface *s, int width, int height, COLORREF top, COLORREF bottom) {
    TRIVERTEX vertices[2];
    GRADIENT_RECT gradient = {0, 1};
    RECT rect;
    if (width <= 0 || height <= 0) return;
    if (top == bottom) {
        SetRect(&rect, 0, 0, width, height);
        SetDCBrushColor(s->dc, top);
        FillRect(s->dc, &rect, (HBRUSH) GetStockObject(DC_BRUSH));
        return;
    }
    memset(vertices, 0, sizeof(vertices));
    vertices[0].Red = (COLOR16) (GetRValue(top) << 8);
    vertices[0].Green = (COLOR16) (GetGValue(top) << 8);
    vertices[0].Blue = (COLOR16) (GetBValue(top) << 8);
    vertices[1].x = width;
    vertices[1].y = height;
    vertices[1].Red = (COLOR16) (GetRValue(bottom) << 8);
    vertices[1].Green = (COLOR16) (GetGValue(bottom) << 8);
    vertices[1].Blue = (COLOR16) (GetBValue(bottom) << 8);
    if (!GradientFill(s->dc, vertices, 2, &gradient, 1, GRADIENT_FILL_RECT_V)) {
        SetRect(&rect, 0, 0, width, height);
        SetDCBrushColor(s->dc, top);
        FillRect(s->dc, &rect, (HBRUSH) GetStockObject(DC_BRUSH));
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
/* Only the rounded edge needs a distance calculation. Interior opaque spans
 * are written directly, and straight edges use a single coverage per row. */
void maage_win32_surface_round_f(maage_win32_surface *s, double x0, double y0, double x1, double y1, double radius, COLORREF color, double opacity, double blur) {
    double hw = (x1 - x0) * 0.5, hh = (y1 - y0) * 0.5;
    double cx = x0 + hw, cy = y0 + hh;
    double distance, coverage, dy, dx;
    int x, y, left, right, top, bottom, middle_left, middle_right, middle_end;
    DWORD pixel = maage_win32_bgra(color), *row;
    if (hw <= 0 || hh <= 0 || opacity <= 0.0) return;
    radius = maage_win32_clamp(radius, 0.0, hw < hh ? hw : hh);
    left = (int) floor(x0 - blur - 1);
    right = (int) ceil(x1 + blur + 1);
    top = (int) floor(y0 - blur - 1);
    bottom = (int) ceil(y1 + blur + 1);
    if (left < 0) left = 0;
    if (top < 0) top = 0;
    if (right > s->width) right = s->width;
    if (bottom > s->height) bottom = s->height;
    if (left >= right || top >= bottom) return;
    middle_left = (int) ceil(x0 + radius);
    middle_right = (int) floor(x1 - radius);
    if (middle_left < left) middle_left = left;
    if (middle_right > right) middle_right = right;
    GdiFlush();
    for (y = top; y < bottom; ++y) {
        int row_left = left, row_right = right, solid_left = right, solid_right = right;
        dy = fabs(y + 0.5 - cy) - (hh - radius);
        row = s->pixels + (size_t) y * s->width;
        if (opacity == 1.0 && blur == 0.0 && radius >= .5) {
            double outer = radius + .5, inner = radius - .5, offset = hw - radius, extent;
            if (dy >= outer) continue;
            extent = offset + (dy > 0 ? sqrt(outer * outer - dy * dy) : outer);
            row_left = (int) ceil(cx - extent - .5);
            row_right = (int) floor(cx + extent - .5) + 1;
            if (row_left < left) row_left = left;
            if (row_right > right) row_right = right;
            if (dy <= inner) {
                extent = offset + (dy > 0 ? sqrt(inner * inner - dy * dy) : inner);
                solid_left = (int) ceil(cx - extent - .5);
                solid_right = (int) floor(cx + extent - .5) + 1;
                if (solid_left < row_left) solid_left = row_left;
                if (solid_right > row_right) solid_right = row_right;
            }
        }
        for (x = row_left; x < row_right;) {
            if (x >= solid_left && x < solid_right) {
                while (x < solid_right) row[x++] = pixel;
                continue;
            }
            if (x >= middle_left && x < middle_right) {
                middle_end = middle_right < row_right ? middle_right : row_right;
                if (x < solid_left && middle_end > solid_left) middle_end = solid_left;
                distance = dy - radius;
            } else {
                middle_end = x + 1;
                dx = fabs(x + 0.5 - cx) - (hw - radius);
                distance = dx > 0 && dy > 0 ? sqrt(dx * dx + dy * dy) - radius : (dx > dy ? dx : dy) - radius;
            }
            coverage = blur > 0 ? maage_win32_clamp(1.0 - (distance + blur * 0.35) / blur, 0.0, 1.0) : maage_win32_clamp(0.5 - distance, 0.0, 1.0);
            if (blur > 0) coverage = coverage * coverage * (3.0 - 2.0 * coverage);
            coverage *= opacity;
            if (coverage >= 1.0) {
                while (x < middle_end) row[x++] = pixel;
            } else if (coverage > 0.0) {
                while (x < middle_end) maage_win32_blend(&row[x++], color, coverage);
            } else x = middle_end;
        }
    }
}

void maage_win32_surface_round(maage_win32_surface *s, RECT rect, double radius, COLORREF color, double opacity, double blur) {
    maage_win32_surface_round_f(s, rect.left, rect.top, rect.right, rect.bottom, radius, color, opacity, blur);
}

/* A canonical 64px-high shadow is generated once per shape. Native
 * AlphaBlend stretches only its straight middle, preserving the corners
 * and blur while the controls resize. The black source is premultiplied. */
int maage_win32_surface_make_control_shadow(maage_win32_surface *s, int pill) {
    int x, y;
    double radius = pill ? 32.0 : 16.64, opacity = pill ? .42 : .32;
    double blur = pill ? 12.0 : 11.0, dx, dy, distance, coverage;
    if (!maage_win32_surface_resize(s, NULL, 100, 96)) return 0;
    GdiFlush();
    for (y = 0; y < 96; ++y) {
        dy = fabs(y + .5 - 48.0) - (32.0 - radius);
        for (x = 0; x < 100; ++x) {
            dx = fabs(x + .5 - 50.0) - (34.0 - radius);
            distance = dx > 0 && dy > 0 ? sqrt(dx * dx + dy * dy) - radius : (dx > dy ? dx : dy) - radius;
            coverage = maage_win32_clamp(1.0 - (distance + blur * .35) / blur, 0.0, 1.0);
            coverage = coverage * coverage * (3.0 - 2.0 * coverage) * opacity;
            s->pixels[(size_t) y * 100 + x] = (DWORD) (coverage * 255.0 + .5) << 24;
        }
    }
    return 1;
}

void maage_win32_surface_control_shadow(maage_win32_surface *s, const maage_win32_surface *mask, RECT shape) {
    BLENDFUNCTION blend = {AC_SRC_OVER, 0, 255, AC_SRC_ALPHA};
    int height = shape.bottom - shape.top, width = shape.right - shape.left;
    int pad, cap, total_width, total_height, left, top, middle;
    if (!mask->dc || height <= 0 || width <= 0) return;
    pad = (height + 2) / 4;
    cap = (height + 2) / 2 + pad;
    total_width = width + 2 * pad;
    total_height = height + 2 * pad;
    left = shape.left - pad;
    top = shape.top - pad;
    if (cap > total_width / 2) cap = total_width / 2;
    middle = total_width - 2 * cap;
    if (cap <= 0) return;
    AlphaBlend(s->dc, left, top, cap, total_height, mask->dc, 0, 0, 48, 96, blend);
    if (middle) AlphaBlend(s->dc, left + cap, top, middle, total_height, mask->dc, 48, 0, 4, 96, blend);
    AlphaBlend(s->dc, left + cap + middle, top, cap, total_height, mask->dc, 52, 0, 48, 96, blend);
}

void maage_win32_surface_line(maage_win32_surface *s, double x1, double y1, double x2, double y2, double thickness, COLORREF color) {
    double dx = x2 - x1, dy = y2 - y1, length = dx * dx + dy * dy, radius = thickness * 0.5;
    int x, y, left = (int) floor((x1 < x2 ? x1 : x2) - radius - 1), right = (int) ceil((x1 > x2 ? x1 : x2) + radius + 1);
    int top = (int) floor((y1 < y2 ? y1 : y2) - radius - 1), bottom = (int) ceil((y1 > y2 ? y1 : y2) + radius + 1);
    if (left < 0) left = 0;
    if (top < 0) top = 0;
    if (right > s->width) right = s->width;
    if (bottom > s->height) bottom = s->height;
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
