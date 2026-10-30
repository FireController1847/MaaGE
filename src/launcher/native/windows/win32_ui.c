#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0a00
#endif
#define WIN32_LEAN_AND_MEAN
#include "win32_ui.h"
#include "resources.h"
#include <uxtheme.h>
#include <dwmapi.h>
#include "maage/storage.h"
#include "maage/log.h"
#include <string.h>

int maage_launcher_win32_scale(const maage_launcher_win32_style *ui, int value) {
    return MulDiv(value, ui->dpi ? (int) ui->dpi : 96, 96);
}

COLORREF maage_launcher_win32_color(const maage_launcher_win32_style *ui, int panel) {
    return ui->dark ? (panel ? RGB(41, 41, 41) : RGB(28, 28, 28)) : GetSysColor(panel ? COLOR_BTNFACE : COLOR_WINDOW);
}
COLORREF maage_launcher_win32_text_color(const maage_launcher_win32_style *ui) {
    return ui->dark ? RGB(225, 225, 225) : GetSysColor(COLOR_WINDOWTEXT);
}

int maage_launcher_win32_same_text(HWND control, const wchar_t *text) {
    wchar_t current[MAAGE_PATH_CAPACITY];
    int length;
    if (!control) return 0;
    current[0] = 0;
    length = GetWindowTextLengthW(control);
    if (length != lstrlenW(text) || length >= MAAGE_PATH_CAPACITY) return 0;
    return GetWindowTextW(control, current, MAAGE_PATH_CAPACITY) == length && !wcscmp(current, text);
}

static void maage_launcher_win32_combo_height(HWND control, UINT dpi) {
    wchar_t kind[16];
    GetClassNameW(control, kind, 16);
    if (!lstrcmpiW(kind, L"COMBOBOX") && (GetWindowLongPtrW(control, GWL_STYLE) & CBS_OWNERDRAWFIXED)) {
        SendMessageW(control, CB_SETITEMHEIGHT, 0, MulDiv(28, (int) dpi, 96));
        SendMessageW(control, CB_SETITEMHEIGHT, (WPARAM) -1, MulDiv(28, (int) dpi, 96));
    }
}

static void maage_launcher_win32_combo_text(HWND combo, UINT item, wchar_t text[1024]) {
    LRESULT length;
    text[0] = 0;
    if (item == (UINT) -1) return;
    length = SendMessageW(combo, CB_GETLBTEXTLEN, item, 0);
    if (length >= 0 && length < 1024) SendMessageW(combo, CB_GETLBTEXT, item, (LPARAM) text);
}

LRESULT maage_launcher_win32_combo_measure(maage_launcher_win32_style *ui, LPARAM item) {
    MEASUREITEMSTRUCT *measure = (MEASUREITEMSTRUCT *) item;
    if (measure->CtlType != ODT_COMBOBOX || !ui->dark) return FALSE;
    measure->itemHeight = (UINT) maage_launcher_win32_scale(ui, 28);
    return TRUE;
}

LRESULT maage_launcher_win32_combo_draw(maage_launcher_win32_style *ui, LPARAM item) {
    DRAWITEMSTRUCT *draw = (DRAWITEMSTRUCT *) item;
    wchar_t text[1024];
    RECT rect;
    int saved, selected, disabled;
    COLORREF fill;
    if (draw->CtlType != ODT_COMBOBOX || !ui->dark) return FALSE;
    saved = SaveDC(draw->hDC);
    if (!saved) return FALSE;
    selected = (draw->itemState & ODS_SELECTED) && !(draw->itemState & ODS_COMBOBOXEDIT);
    disabled = (draw->itemState & ODS_DISABLED) != 0;
    fill = selected ? RGB(151, 57, 116) : (draw->itemState & ODS_COMBOBOXEDIT) ? RGB(52, 52, 52)
                                                                               : RGB(41, 41, 41);
    SetDCBrushColor(draw->hDC, fill);
    FillRect(draw->hDC, &draw->rcItem, (HBRUSH) GetStockObject(DC_BRUSH));
    SetTextColor(draw->hDC, disabled ? RGB(138, 138, 138) : RGB(245, 245, 245));
    SetBkMode(draw->hDC, TRANSPARENT);
    SelectObject(draw->hDC, ui->font);
    maage_launcher_win32_combo_text(draw->hwndItem, draw->itemID, text);
    rect = draw->rcItem;
    rect.left += maage_launcher_win32_scale(ui, 8);
    rect.right -= maage_launcher_win32_scale(ui, 8);
    DrawTextW(draw->hDC, text, -1, &rect, DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS | DT_NOPREFIX);
    if ((draw->itemState & ODS_FOCUS) && !(draw->itemState & (ODS_NOFOCUSRECT | ODS_COMBOBOXEDIT))) {
        rect = draw->rcItem;
        InflateRect(&rect, -2, -2);
        DrawFocusRect(draw->hDC, &rect);
    }
    RestoreDC(draw->hDC, saved);
    return TRUE;
}

static void maage_launcher_win32_combo_paint(maage_launcher_win32_style *ui, HWND combo, HDC dc) {
    COMBOBOXINFO info;
    RECT rect, text_rect;
    POINT arrow[3];
    wchar_t text[1024];
    int disabled = !IsWindowEnabled(combo), focused = GetFocus() == combo;
    int saved = SaveDC(dc), center, y, radius;
    if (!saved) return;
    GetClientRect(combo, &rect);
    DrawThemeParentBackground(combo, dc, &rect);
    SelectObject(dc, GetStockObject(DC_BRUSH));
    SetDCBrushColor(dc, disabled ? RGB(38, 38, 38) : RGB(52, 52, 52));
    SelectObject(dc, GetStockObject(DC_PEN));
    SetDCPenColor(dc, focused ? RGB(222, 74, 165) : ui->hot_combo == combo ? RGB(115, 115, 115)
                                                                           : RGB(87, 87, 87));
    InflateRect(&rect, -1, -1);
    radius = maage_launcher_win32_scale(ui, 8);
    RoundRect(dc, rect.left, rect.top, rect.right, rect.bottom, radius, radius);
    memset(&info, 0, sizeof(info));
    info.cbSize = sizeof(info);
    if (GetComboBoxInfo(combo, &info)) center = (info.rcButton.left + info.rcButton.right) / 2;
    else
        center = rect.right - maage_launcher_win32_scale(ui, 14);
    y = (rect.top + rect.bottom) / 2;
    SetDCPenColor(dc, disabled ? RGB(138, 138, 138) : RGB(225, 225, 225));
    arrow[0].x = center - maage_launcher_win32_scale(ui, 4);
    arrow[0].y = y - maage_launcher_win32_scale(ui, 2);
    arrow[1].x = center;
    arrow[1].y = y + maage_launcher_win32_scale(ui, 2);
    arrow[2].x = center + maage_launcher_win32_scale(ui, 4);
    arrow[2].y = arrow[0].y;
    Polyline(dc, arrow, 3);
    maage_launcher_win32_combo_text(combo, (UINT) SendMessageW(combo, CB_GETCURSEL, 0, 0), text);
    text_rect = rect;
    text_rect.left += maage_launcher_win32_scale(ui, 8);
    text_rect.right = center - maage_launcher_win32_scale(ui, 12);
    SelectObject(dc, ui->font);
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, disabled ? RGB(138, 138, 138) : RGB(245, 245, 245));
    DrawTextW(dc, text, -1, &text_rect, DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS | DT_NOPREFIX);
    if (focused && !(SendMessageW(combo, WM_QUERYUISTATE, 0, 0) & UISF_HIDEFOCUS)) {
        InflateRect(&text_rect, -2, -2);
        DrawFocusRect(dc, &text_rect);
    }
    RestoreDC(dc, saved);
}

static LRESULT CALLBACK maage_launcher_win32_combo_proc(HWND combo, UINT message, WPARAM wparam, LPARAM lparam, UINT_PTR id, DWORD_PTR reference) {
    maage_launcher_win32_style *ui = (maage_launcher_win32_style *) reference;
    LRESULT result;
    switch (message) {
    case WM_PAINT: {
        PAINTSTRUCT paint;
        HDC dc = BeginPaint(combo, &paint);
        maage_launcher_win32_combo_paint(ui, combo, dc);
        EndPaint(combo, &paint);
        return 0;
    }
    case WM_PRINTCLIENT:
        maage_launcher_win32_combo_paint(ui, combo, (HDC) wparam);
        return 0;
    case WM_ERASEBKGND:
        return 1;
    case WM_CTLCOLORLISTBOX:
        SetTextColor((HDC) wparam, RGB(245, 245, 245));
        SetBkColor((HDC) wparam, RGB(41, 41, 41));
        SetDCBrushColor((HDC) wparam, RGB(41, 41, 41));
        return (LRESULT) GetStockObject(DC_BRUSH);
    case WM_MOUSEMOVE:
        if (ui->hot_combo != combo) {
            TRACKMOUSEEVENT track;
            ui->hot_combo = combo;
            memset(&track, 0, sizeof(track));
            track.cbSize = sizeof(track);
            track.dwFlags = TME_LEAVE;
            track.hwndTrack = combo;
            TrackMouseEvent(&track);
            InvalidateRect(combo, NULL, FALSE);
        }
        break;
    case WM_MOUSELEAVE:
        if (ui->hot_combo == combo) ui->hot_combo = NULL;
        InvalidateRect(combo, NULL, FALSE);
        break;
    case WM_NCDESTROY:
        if (ui->hot_combo == combo) ui->hot_combo = NULL;
        RemoveWindowSubclass(combo, maage_launcher_win32_combo_proc, id);
        break;
    }
    result = DefSubclassProc(combo, message, wparam, lparam);
    if (message == WM_SETFOCUS || message == WM_KILLFOCUS || message == WM_ENABLE || message == WM_KEYDOWN || message == WM_LBUTTONUP || message == CB_SETCURSEL || message == CB_SHOWDROPDOWN) InvalidateRect(combo, NULL, FALSE);
    return result;
}

static void maage_launcher_win32_progress_paint(HWND progress, HDC dc, const maage_launcher_control *control) {
    RECT rect, fill;
    PBRANGE range;
    int width, chunk, x, saved = SaveDC(dc);
    double fraction;
    if (!saved) return;
    GetClientRect(progress, &rect);
    DrawThemeParentBackground(progress, dc, &rect);
    SelectObject(dc, GetStockObject(DC_BRUSH));
    SelectObject(dc, GetStockObject(NULL_PEN));
    SetDCBrushColor(dc, RGB(61, 61, 61));
    RoundRect(dc, rect.left, rect.top, rect.right, rect.bottom, rect.bottom, rect.bottom);
    width = rect.right - rect.left;
    fill = rect;
    if (control->marquee) {
        chunk = width / 4;
        x = MulDiv(width + chunk, (int) control->marquee_phase, 90) - chunk;
        fill.left = x > 0 ? x : 0;
        fill.right = x + chunk < width ? x + chunk : width;
    } else {
        SendMessageW(progress, PBM_GETRANGE, FALSE, (LPARAM) &range);
        fraction = range.iHigh > range.iLow ? ((double) SendMessageW(progress, PBM_GETPOS, 0, 0) - range.iLow) / ((double) range.iHigh - range.iLow) : 0.0;
        if (fraction < 0.0) fraction = 0.0;
        if (fraction > 1.0) fraction = 1.0;
        fill.right = fill.left + (int) (width * fraction + 0.5);
    }
    if (fill.right > fill.left) {
        SetDCBrushColor(dc, RGB(222, 48, 158));
        RoundRect(dc, fill.left, fill.top, fill.right, fill.bottom, rect.bottom, rect.bottom);
    }
    RestoreDC(dc, saved);
}

static LRESULT CALLBACK maage_launcher_win32_progress_proc(HWND progress, UINT message, WPARAM wparam, LPARAM lparam, UINT_PTR id, DWORD_PTR reference) {
    maage_launcher_control *control = (maage_launcher_control *) reference;
    LRESULT result;
    switch (message) {
    case WM_PAINT: {
        PAINTSTRUCT paint;
        HDC dc = BeginPaint(progress, &paint);
        maage_launcher_win32_progress_paint(progress, dc, control);
        EndPaint(progress, &paint);
        return 0;
    }
    case WM_PRINTCLIENT:
        maage_launcher_win32_progress_paint(progress, (HDC) wparam, control);
        return 0;
    case WM_ERASEBKGND:
        return 1;
    case PBM_SETMARQUEE:
        if (!!wparam != control->marquee) {
            control->marquee = !!wparam;
            control->marquee_phase = 0;
            if (control->marquee) {
                if (!SetTimer(progress, 1, lparam > 0 ? (UINT) lparam : 30, NULL)) {
                    maage_status failure = maage_status_from_win32(GetLastError(), MAAGE_CONTEXT_LAUNCHER, MAAGE_OPERATION_CREATE);
                    maage_log_status(MAAGE_LOG_WARNING, "maage.launcher.ui", failure, "Progress animation is unavailable.");
                    return FALSE;
                }
            } else
                KillTimer(progress, 1);
            InvalidateRect(progress, NULL, FALSE);
        }
        return TRUE;
    case WM_TIMER:
        if (wparam == 1 && control->marquee) {
            control->marquee_phase = (control->marquee_phase + 1) % 90;
            InvalidateRect(progress, NULL, FALSE);
            return 0;
        }
        break;
    case WM_NCDESTROY:
        KillTimer(progress, 1);
        control->marquee = 0;
        RemoveWindowSubclass(progress, maage_launcher_win32_progress_proc, id);
        break;
    }
    result = DefSubclassProc(progress, message, wparam, lparam);
    if (message == PBM_SETPOS || message == PBM_SETRANGE || message == PBM_SETRANGE32) InvalidateRect(progress, NULL, FALSE);
    return result;
}

static maage_status maage_launcher_win32_fonts(maage_launcher_win32_style *ui, UINT dpi) {
    HFONT fonts[3], old[3];
    const int sizes[3] = {16, 27, 34};
    unsigned i;
    for (i = 0; i < 3; ++i) {
        fonts[i] = CreateFontW(-MulDiv(sizes[i], (int) dpi, 96), 0, 0, 0, i ? FW_SEMIBOLD : FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
        if (!fonts[i]) {
            while (i)
                DeleteObject(fonts[--i]);
            return maage_status_make(MAAGE_STATUS_ALLOCATION_FAILED, MAAGE_CONTEXT_LAUNCHER, MAAGE_OPERATION_CREATE, 0);
        }
    }
    old[0] = ui->font;
    old[1] = ui->heading;
    old[2] = ui->brand;
    for (i = 0; i < ui->count; ++i) {
        HFONT current = (HFONT) SendMessageW(ui->controls[i].handle, WM_GETFONT, 0, 0);
        SendMessageW(ui->controls[i].handle, WM_SETFONT,
                     (WPARAM) (current == old[2] ? fonts[2] : current == old[1] ? fonts[1]
                                                                                : fonts[0]),
                     TRUE);
        maage_launcher_win32_combo_height(ui->controls[i].handle, dpi);
    }
    ui->font = fonts[0];
    ui->heading = fonts[1];
    ui->brand = fonts[2];
    ui->dpi = dpi;
    for (i = 0; i < 3; ++i)
        if (old[i]) DeleteObject(old[i]);
    return maage_status_make(MAAGE_STATUS_SUCCESS, 0, MAAGE_OPERATION_CREATE, 0);
}

maage_status maage_launcher_win32_style_init(maage_launcher_win32_style *ui, HWND window, int width, int height, int dark) {
    INITCOMMONCONTROLSEX common;
    HIGHCONTRASTW contrast;
    RECT bounds;
    UINT dpi = GetDpiForWindow(window);
    BOOL immersive;
    int icon_index;
    ui->window = window;
    memset(&contrast, 0, sizeof(contrast));
    contrast.cbSize = sizeof(contrast);
    if (SystemParametersInfoW(SPI_GETHIGHCONTRAST, sizeof(contrast), &contrast, 0)) ui->high_contrast = (contrast.dwFlags & HCF_HIGHCONTRASTON) != 0;
    ui->dark = dark && !ui->high_contrast;
    common.dwSize = sizeof(common);
    common.dwICC = ICC_STANDARD_CLASSES | ICC_LISTVIEW_CLASSES | ICC_PROGRESS_CLASS;
    if (!dpi || !InitCommonControlsEx(&common)) return maage_status_make(MAAGE_STATUS_ERROR, MAAGE_CONTEXT_LAUNCHER, MAAGE_OPERATION_INITIALIZE, 0);
    ui->result = maage_launcher_win32_fonts(ui, dpi);
    if (maage_status_failed(ui->result)) return ui->result;
    bounds.left = bounds.top = 0;
    bounds.right = maage_launcher_win32_scale(ui, width);
    bounds.bottom = maage_launcher_win32_scale(ui, height);
    if (!AdjustWindowRectExForDpi(&bounds, (DWORD) GetWindowLongPtrW(window, GWL_STYLE), FALSE, (DWORD) GetWindowLongPtrW(window, GWL_EXSTYLE), dpi) || !SetWindowPos(window, NULL, 0, 0, bounds.right - bounds.left, bounds.bottom - bounds.top, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE)) return maage_status_from_win32(GetLastError(), MAAGE_CONTEXT_LAUNCHER, MAAGE_OPERATION_RESIZE);
    immersive = ui->dark;
    /* Optional non-client decoration; unsupported Windows versions retain their frame. */
    DwmSetWindowAttribute(window, DWMWA_USE_IMMERSIVE_DARK_MODE, &immersive, sizeof(immersive));
    for (icon_index = 0; icon_index < 2; ++icon_index) {
        ui->icons[icon_index] = (HICON) LoadImageW(GetModuleHandleW(NULL), MAKEINTRESOURCEW(MAAGE_LAUNCHER_ICON_RESOURCE), IMAGE_ICON, GetSystemMetricsForDpi(icon_index ? SM_CXSMICON : SM_CXICON, dpi), GetSystemMetricsForDpi(icon_index ? SM_CYSMICON : SM_CYICON, dpi), 0);
        if (ui->icons[icon_index]) SendMessageW(window, WM_SETICON, icon_index ? ICON_SMALL : ICON_BIG, (LPARAM) ui->icons[icon_index]);
        else {
            maage_log_status(MAAGE_LOG_WARNING, "maage.launcher.ui", maage_status_from_win32(GetLastError(), MAAGE_CONTEXT_LAUNCHER, MAAGE_OPERATION_LOAD), "Launcher window icon is unavailable.");
            ui->result = maage_status_make(MAAGE_STATUS_PARTIAL, MAAGE_CONTEXT_LAUNCHER, MAAGE_OPERATION_INITIALIZE, 0);
        }
    }
    return ui->result;
}

maage_status maage_launcher_win32_style_destroy(maage_launcher_win32_style *ui) {
    int failed = 0;
    if (ui->font && !DeleteObject(ui->font)) failed = 1;
    if (ui->heading && !DeleteObject(ui->heading)) failed = 1;
    if (ui->brand && !DeleteObject(ui->brand)) failed = 1;
    if (ui->icons[0] && !DestroyIcon(ui->icons[0])) failed = 1;
    if (ui->icons[1] && !DestroyIcon(ui->icons[1])) failed = 1;
    ui->font = ui->heading = ui->brand = NULL;
    ui->icons[0] = ui->icons[1] = NULL;
    return maage_status_make(failed ? MAAGE_STATUS_ERROR : MAAGE_STATUS_SUCCESS, MAAGE_CONTEXT_LAUNCHER, MAAGE_OPERATION_DESTROY, 0);
}

HWND maage_launcher_win32_control(maage_launcher_win32_style *ui, int page, const wchar_t *kind, const wchar_t *text, DWORD style, int x, int y, int width, int height, int id) {
    maage_launcher_control *control;
    int combo = ui->dark && !lstrcmpiW(kind, L"COMBOBOX") && (style & 3) == CBS_DROPDOWNLIST;
    if (maage_status_failed(ui->result)) return NULL;
    if (ui->count == sizeof(ui->controls) / sizeof(ui->controls[0])) {
        ui->result = maage_status_make(MAAGE_STATUS_BUFFER_OVERFLOW, MAAGE_CONTEXT_LAUNCHER, MAAGE_OPERATION_CREATE, 0);
        return NULL;
    }
    control = &ui->controls[ui->count];
    control->page = page;
    control->x = x;
    control->y = y;
    control->width = width;
    control->height = height;
    if (combo) style |= CBS_OWNERDRAWFIXED | CBS_HASSTRINGS;
    control->handle = CreateWindowExW(!wcscmp(kind, L"EDIT") ? WS_EX_CLIENTEDGE : 0,
                                      kind, text, WS_CHILD | WS_VISIBLE | style,
                                      maage_launcher_win32_scale(ui, x), maage_launcher_win32_scale(ui, y),
                                      maage_launcher_win32_scale(ui, width > 0 ? width : 1),
                                      maage_launcher_win32_scale(ui, height > 0 ? height : 1), ui->window,
                                      (HMENU) (INT_PTR) id, GetModuleHandleW(NULL), NULL);
    if (!control->handle) {
        ui->result = maage_status_from_win32(GetLastError(), MAAGE_CONTEXT_LAUNCHER, MAAGE_OPERATION_CREATE);
        return NULL;
    }
    ++ui->count;
    SendMessageW(control->handle, WM_SETFONT, (WPARAM) ui->font, TRUE);
    if (combo) {
        maage_launcher_win32_combo_height(control->handle, ui->dpi);
        if (!SetWindowSubclass(control->handle, maage_launcher_win32_combo_proc, 1, (DWORD_PTR) ui)) ui->result = maage_status_make(MAAGE_STATUS_ERROR, MAAGE_CONTEXT_LAUNCHER, MAAGE_OPERATION_INITIALIZE, 0);
    }
    if (ui->dark && !lstrcmpiW(kind, PROGRESS_CLASSW)) {
        SetWindowTheme(control->handle, L"", L"");
        SendMessageW(control->handle, PBM_SETBARCOLOR, 0, RGB(222, 48, 158));
        SendMessageW(control->handle, PBM_SETBKCOLOR, 0, RGB(61, 61, 61));
        if (!SetWindowSubclass(control->handle, maage_launcher_win32_progress_proc, 1, (DWORD_PTR) control)) ui->result = maage_status_make(MAAGE_STATUS_ERROR, MAAGE_CONTEXT_LAUNCHER, MAAGE_OPERATION_INITIALIZE, 0);
    }
    return control->handle;
}

void maage_launcher_win32_layout(maage_launcher_win32_style *ui) {
    unsigned i;
    for (i = 0; i < ui->count; ++i) {
        maage_launcher_control *c = &ui->controls[i];
        SetWindowPos(c->handle, NULL, maage_launcher_win32_scale(ui, c->x), maage_launcher_win32_scale(ui, c->y), maage_launcher_win32_scale(ui, c->width), maage_launcher_win32_scale(ui, c->height), SWP_NOZORDER | SWP_NOACTIVATE);
    }
}

maage_status maage_launcher_win32_dpi_changed(maage_launcher_win32_style *ui, WPARAM dpi, LPARAM bounds) {
    RECT *rect = (RECT *) bounds;
    maage_status result = maage_launcher_win32_fonts(ui, HIWORD(dpi));
    if (maage_status_failed(result)) return result;
    if (!SetWindowPos(ui->window, NULL, rect->left, rect->top, rect->right - rect->left, rect->bottom - rect->top, SWP_NOZORDER | SWP_NOACTIVATE)) return maage_status_from_win32(GetLastError(), MAAGE_CONTEXT_LAUNCHER, MAAGE_OPERATION_RESIZE);
    InvalidateRect(ui->window, NULL, TRUE);
    return result;
}

LRESULT maage_launcher_win32_button(maage_launcher_win32_style *ui, LPARAM notification) {
    NMCUSTOMDRAW *draw = (NMCUSTOMDRAW *) notification;
    wchar_t kind[16], text[256];
    HWND button = draw->hdr.hwndFrom;
    RECT rect;
    COLORREF fill, border, foreground;
    int primary, disabled, pressed, hot, saved, radius;
    UINT flags = DT_CENTER | DT_VCENTER | DT_SINGLELINE;
    if (draw->hdr.code != NM_CUSTOMDRAW || ui->high_contrast || (!ui->dark && button != ui->primary)) return CDRF_DODEFAULT;
    GetClassNameW(button, kind, 16);
    if (lstrcmpiW(kind, L"Button") || draw->dwDrawStage != CDDS_PREPAINT) return CDRF_DODEFAULT;
    primary = button == ui->primary;
    disabled = !IsWindowEnabled(button);
    pressed = (draw->uItemState & CDIS_SELECTED) != 0;
    hot = (draw->uItemState & CDIS_HOT) != 0;
    fill = primary ? (pressed ? RGB(121, 42, 92) : hot ? RGB(174, 66, 135)
                                                       : RGB(151, 57, 116))
                   : (pressed ? RGB(43, 43, 43) : hot ? RGB(67, 67, 67)
                                                      : RGB(52, 52, 52));
    if (disabled) fill = ui->dark ? RGB(38, 38, 38) : GetSysColor(COLOR_BTNFACE);
    border = disabled ? (ui->dark ? RGB(59, 59, 59) : GetSysColor(COLOR_3DSHADOW)) : hot || (GetWindowLongPtrW(button, GWL_STYLE) & BS_TYPEMASK) == BS_DEFPUSHBUTTON ? RGB(222, 74, 165)
                                                                                                                                                                     : RGB(87, 87, 87);
    foreground = disabled ? (ui->dark ? RGB(138, 138, 138) : GetSysColor(COLOR_GRAYTEXT)) : RGB(245, 245, 245);
    saved = SaveDC(draw->hdc);
    if (!saved) return CDRF_DODEFAULT;
    rect = draw->rc;
    DrawThemeParentBackground(button, draw->hdc, &rect);
    SelectObject(draw->hdc, GetStockObject(DC_BRUSH));
    SetDCBrushColor(draw->hdc, fill);
    SelectObject(draw->hdc, GetStockObject(DC_PEN));
    SetDCPenColor(draw->hdc, border);
    InflateRect(&rect, -1, -1);
    radius = maage_launcher_win32_scale(ui, 8);
    RoundRect(draw->hdc, rect.left, rect.top, rect.right, rect.bottom, radius, radius);
    SetTextColor(draw->hdc, foreground);
    SetBkMode(draw->hdc, TRANSPARENT);
    SelectObject(draw->hdc, ui->font);
    GetWindowTextW(button, text, 256);
    if (SendMessageW(button, WM_QUERYUISTATE, 0, 0) & UISF_HIDEACCEL) flags |= DT_HIDEPREFIX;
    DrawTextW(draw->hdc, text, -1, &rect, flags);
    if ((draw->uItemState & CDIS_FOCUS) && !(SendMessageW(button, WM_QUERYUISTATE, 0, 0) & UISF_HIDEFOCUS)) {
        InflateRect(&rect, -maage_launcher_win32_scale(ui, 4), -maage_launcher_win32_scale(ui, 4));
        DrawFocusRect(draw->hdc, &rect);
    }
    RestoreDC(draw->hdc, saved);
    return CDRF_SKIPDEFAULT;
}
