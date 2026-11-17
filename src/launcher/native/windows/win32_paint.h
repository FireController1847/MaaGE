#ifndef MAAGE_LAUNCHER_WIN32_PAINT_H
#define MAAGE_LAUNCHER_WIN32_PAINT_H
#include <windows.h>

/* Owned top-down BGRA DIB, used only by the Launcher UI thread. */
typedef struct maage_win32_surface {
    HDC dc;
    HBITMAP bitmap, previous;
    DWORD *pixels;
    int width, height;
} maage_win32_surface;
int maage_win32_surface_resize(maage_win32_surface *s, HDC compatible, int width, int height);
void maage_win32_surface_destroy(maage_win32_surface *s);
int maage_win32_surface_resource(maage_win32_surface *s, HINSTANCE instance, int resource);
int maage_win32_surface_shadow(maage_win32_surface *s, const maage_win32_surface *source);
void maage_win32_surface_image(maage_win32_surface *s, const maage_win32_surface *image, RECT rect);
int maage_win32_surface_resample(maage_win32_surface *s, const maage_win32_surface *image, int width, int height);
int maage_win32_surface_dim(maage_win32_surface *s, const maage_win32_surface *image);
void maage_win32_surface_text_mask(maage_win32_surface *s, const maage_win32_surface *mask, RECT rect, COLORREF color, double opacity);
void maage_win32_surface_cover(maage_win32_surface *s, const maage_win32_surface *image, int width, int height, int filtered);
void maage_win32_surface_mask(maage_win32_surface *s, const maage_win32_surface *image, RECT rect, COLORREF color, double opacity);
void maage_win32_surface_gradient_rect(maage_win32_surface *s, int width, int height, COLORREF top, COLORREF bottom);
int maage_win32_surface_make_control_shadow(maage_win32_surface *s, int pill);
void maage_win32_surface_control_shadow(maage_win32_surface *s, const maage_win32_surface *mask, RECT shape);
void maage_win32_surface_round_f(maage_win32_surface *s, double x0, double y0, double x1, double y1, double radius, COLORREF color, double opacity, double blur);
void maage_win32_surface_round(maage_win32_surface *s, RECT rect, double radius, COLORREF color, double opacity, double blur);
/* Flush any pending GDI operations before starting a batch of CPU strokes. */
void maage_win32_surface_line(maage_win32_surface *s, double x1, double y1, double x2, double y2, double thickness, COLORREF color);
#endif
