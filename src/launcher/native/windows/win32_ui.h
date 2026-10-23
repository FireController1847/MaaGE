#ifndef MAAGE_LAUNCHER_WIN32_UI_H
#define MAAGE_LAUNCHER_WIN32_UI_H

#include <windows.h>
#include <commctrl.h>
#include "maage/status.h"

/* Launcher-private native control geometry is always stored at 96 DPI. */
typedef struct maage_launcher_control {
    HWND handle;
    int page, x, y, width, height;
    unsigned marquee_phase;
    int marquee;
} maage_launcher_control;
typedef struct maage_launcher_win32_style {
    HWND window, primary, hot_combo;
    UINT dpi;
    HFONT font, heading, brand;
    maage_launcher_control controls[72];
    unsigned count;
    int dark, high_contrast;
    maage_status result;
} maage_launcher_win32_style;

maage_status maage_launcher_win32_style_init(maage_launcher_win32_style *ui,
    HWND window, int width, int height, int dark);
maage_status maage_launcher_win32_style_destroy(maage_launcher_win32_style *ui);
HWND maage_launcher_win32_control(maage_launcher_win32_style *ui, int page,
    const wchar_t *kind, const wchar_t *text, DWORD style,
    int x, int y, int width, int height, int id);
int maage_launcher_win32_scale(const maage_launcher_win32_style *ui, int value);
COLORREF maage_launcher_win32_color(const maage_launcher_win32_style *ui, int panel);
COLORREF maage_launcher_win32_text_color(const maage_launcher_win32_style *ui);
void maage_launcher_win32_layout(maage_launcher_win32_style *ui);
maage_status maage_launcher_win32_dpi_changed(maage_launcher_win32_style *ui,
    WPARAM dpi, LPARAM bounds);
LRESULT maage_launcher_win32_button(maage_launcher_win32_style *ui, LPARAM notification);
LRESULT maage_launcher_win32_combo_measure(maage_launcher_win32_style *ui, LPARAM item);
LRESULT maage_launcher_win32_combo_draw(maage_launcher_win32_style *ui, LPARAM item);
int maage_launcher_win32_same_text(HWND control, const wchar_t *text);

#endif
