#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <windowsx.h>
#include <commctrl.h>
#include <shellapi.h>
#include <commdlg.h>
#include <dwmapi.h>
#include "launcher.h"
#include "../native/windows/win32_ui.h"
#include "../native/windows/resources.h"
#include "../native/windows/win32_paint.h"
#include "maage/launcher_version.h"
#include "maage/log.h"
#include "maage/time.h"
#include <string.h>
#include <stdio.h>
#include <wchar.h>
#include <math.h>
#define MAAGE_UI_ACCOUNT 110
#define MAAGE_UI_LIST 112
#define MAAGE_UI_NAME 113
#define MAAGE_UI_ARGUMENTS 114
#define MAAGE_UI_VERIFY 121
#define MAAGE_UI_REPAIR 122
#define MAAGE_UI_PROJECT 123
#define MAAGE_UI_STATUS 124
#define MAAGE_UI_NEW 130
#define MAAGE_UI_EDIT 131
#define MAAGE_UI_DUPLICATE 132
#define MAAGE_UI_REMOVE 133
#define MAAGE_UI_DEFAULT 134
#define MAAGE_UI_OPEN_FOLDER 135
#define MAAGE_UI_CANCEL_NEW 136
#define MAAGE_UI_ACTIVE 137
#define MAAGE_UI_CLIENTS 140
#define MAAGE_UI_CLIENT_CHOICE 141
#define MAAGE_UI_CLIENT_IMPORT 142
#define MAAGE_UI_CLIENT_REMOVE 143
#define MAAGE_UI_CLIENT_REPLACE 144
#define MAAGE_UI_ABOUT 125
#define MAAGE_UI_BACK_SETTINGS 126
#define MAAGE_UI_SIGN_IN 160
#define MAAGE_UI_DEVICE_SIGN_IN 161
#define MAAGE_UI_SIGN_OUT 162
#define MAAGE_UI_REMOVE_ACCOUNT 163
#define MAAGE_UI_CANCEL_ACCOUNT 164
#define MAAGE_UI_COPY_CODE 165
#define MAAGE_UI_OPEN_DEVICE 166
#define MAAGE_UI_PROFILES 167
#define MAAGE_UI_VERIFY_RESOURCES 170
#define MAAGE_UI_RETRY_RESOURCES 171
#define MAAGE_UI_CANCEL_RESOURCES 172
#define MAAGE_UI_APPEARANCE 173
#define MAAGE_UI_HOME_CHOOSE 180
#define MAAGE_UI_HOME_MANAGE 184
#define MAAGE_UI_HOME_SETTINGS 188
#define MAAGE_UI_SPLASH_SKIP 189
#define MAAGE_UI_SPLASH_RETRY 190
#define MAAGE_UI_SPLASH_SIGN_IN 191
#define MAAGE_UI_SPLASH_DEVICE_SIGN_IN 192
#define MAAGE_UI_SPLASH_CANCEL_ACCOUNT 193
#define MAAGE_UI_SPLASH_COPY 194
#define MAAGE_UI_SPLASH_OPEN 195
#define MAAGE_UI_HOME_STATUS 196
#define MAAGE_UI_HOME (-2)
#define MAAGE_UI_SPLASH (-3)
#define MAAGE_SPLASH_SIGN_IN 1
#define MAAGE_SPLASH_CANCEL 2
#define MAAGE_SPLASH_DEVICE 4
#define MAAGE_SPLASH_RETRY 8
#define MAAGE_SPLASH_SKIP 16
#define MAAGE_SPLASH_PROGRESS 32
#define MAAGE_UI_ANIMATION_TIMER 2
#define MAAGE_UI_ANIMATION_INTERVAL 15
#define MAAGE_UI_PAGE_FADE_SECONDS 0.19
#define MAAGE_UI_PROGRESS_SECONDS 0.22
#define MAAGE_UI_HOME_FONTS 48
#define MAAGE_UI_HOME_BUTTONS 11
#define MAAGE_UI_HOME_HOVER_SECONDS 0.17
typedef struct maage_launcher_win32 {
    maage_launcher_ui *state;
    HWND window, navigation[5], account, list, name, arguments, status, resource, account_status, verify, repair, error, clients, client_choice, play;
    HWND clients_label, clients_import, clients_replace, clients_remove, installations_label, list_hint, new_button, editor_heading, editor_hint, name_label, client_label, arguments_label;
    HWND active_summary, authentication, client_status, profiles, sign_in, device_sign_in, sign_out, remove_account, cancel_account, copy_code, open_device, verify_resources, retry_resources, cancel_resources, progress, appearance;
    HWND save_button, duplicate_button, remove_button, default_button, active_button, cancel_button, folder_button;
    HWND home_choose[4], home_manage[4], home_settings, home_status;
    HWND splash_message, splash_detail, splash_sign_in, splash_device_sign_in, splash_cancel, splash_code, splash_copy, splash_open, splash_retry, splash_skip, splash_activity;
    HICON artwork;
    maage_win32_surface backdrop, button_surface, logo, logo_shadow, play_shadow, card_shadow, transition;
    maage_win32_surface home_background, home_icons[MAAGE_LAUNCHER_HOME_ICON_COUNT], footer_surface;
    double home_rem;
    double home_hover_started[MAAGE_UI_HOME_BUTTONS], home_hover_from[MAAGE_UI_HOME_BUTTONS];
    int home_fira;
    HWND transition_window;
    double transition_started, progress_from, progress_started;
    int backdrop_width, backdrop_height, live_resize, transition_active;
    HFONT splash_title_font, splash_detail_font, splash_log_font;
    struct { HFONT handle; int pixels, bold; } home_fonts[MAAGE_UI_HOME_FONTS];
    unsigned home_font_next;
    RECT home_logo, home_heading, home_tagline, home_cards[4], home_version, home_notice, splash_logo;
    HWND hot_button, splash_summary, home_tooltip;
    wchar_t home_tooltip_text[384];
    int backdrop_dirty, splash_percent, laying_out, caption_home;
    double splash_started;
    maage_launcher_win32_style native;
    HBRUSH background, sidebar;
    unsigned revision, client_revision;
    int onboarding_decided, onboarding_required, splash_dismissed, splash_visible, splash_flags, splash_preparing, menu_open;
    int splash_stage, splash_step, splash_logo_size, animate_splash;
    wchar_t splash_log[768];
    int updating, creating, previous_row, editor_row, scroll[5], scrollbar_visible, displayed_appearance, marquee, progress_value, controls_ready;
} maage_launcher_win32;
static void maage_ui_text(HWND control, const char *text) {
    wchar_t wide[MAAGE_PATH_CAPACITY];
    if (MultiByteToWideChar(CP_UTF8, 0, text, -1, wide, MAAGE_PATH_CAPACITY) && !maage_launcher_win32_same_text(control, wide)) SetWindowTextW(control, wide);
}
static HWND maage_ui_control(maage_launcher_win32 *ui, int page, const wchar_t *klass, const wchar_t *title, DWORD style, int x, int y, int width, int height, int id) {
    return maage_launcher_win32_control(&ui->native, page, klass, title, style, x, y, width, height, id);
}
static HWND maage_ui_label(maage_launcher_win32 *ui, int page, const wchar_t *title, int x, int y, int width, int height) {
    return maage_ui_control(ui, page, L"STATIC", title, 0, x, y, width, height, 0);
}
static HWND maage_ui_button(maage_launcher_win32 *ui, int page, const wchar_t *title, int x, int y, int width, int id) {
    return maage_ui_control(ui, page, L"BUTTON", title, WS_TABSTOP | (page == MAAGE_UI_HOME ? BS_OWNERDRAW : BS_PUSHBUTTON), x, y, width, 32, id);
}

/* Geometry remains logical until placement; the native hit targets and the
 * painted shapes share the same physical rectangles at every DPI. */
static int maage_ui_scale(maage_launcher_win32 *ui, double value) {
    return (int) (value * (ui->native.dpi ? ui->native.dpi : 96) / 96.0 + 0.5);
}

static RECT maage_ui_rect(maage_launcher_win32 *ui, double x, double y, double width, double height) {
    RECT r;
    r.left = maage_ui_scale(ui, x);
    r.top = maage_ui_scale(ui, y);
    r.right = maage_ui_scale(ui, x + width);
    r.bottom = maage_ui_scale(ui, y + height);
    return r;
}

static RECT maage_ui_child_rect(maage_launcher_win32 *ui, HWND control) {
    RECT rect;
    GetWindowRect(control, &rect);
    MapWindowPoints(NULL, ui->window, (POINT *) &rect, 2);
    return rect;
}

static void maage_ui_place(HWND control, RECT rect) {
    RECT current;
    GetWindowRect(control, &current);
    MapWindowPoints(NULL, GetParent(control), (POINT *) &current, 2);
    if (!EqualRect(&rect, &current)) SetWindowPos(control, NULL, rect.left, rect.top, rect.right - rect.left, rect.bottom - rect.top, SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOREDRAW);
}

static void maage_ui_visible(HWND control, int visible) {
    if (!!(GetWindowLongPtrW(control, GWL_STYLE) & WS_VISIBLE) != !!visible) ShowWindow(control, visible ? SW_SHOW : SW_HIDE);
}

static int maage_ui_scratch(maage_win32_surface *surface, HDC dc, int width, int height) {
    if (width <= 0 || height <= 0) return 0;
    if (surface->width >= width && surface->height >= height && surface->dc) return 1;
    if (surface->width > width) width = surface->width;
    if (surface->height > height) height = surface->height;
    return maage_win32_surface_resize(surface, dc, (width + 63) / 64 * 64, (height + 63) / 64 * 64);
}

/* Windows 11 can tint its native caption; older versions simply retain
 * their system frame. High contrast always keeps the system colors. */
static void maage_ui_caption(maage_launcher_win32 *ui) {
    enum { MAAGE_DWM_CAPTION_COLOR = 35, MAAGE_DWM_TEXT_COLOR = 36 };
    int home = !ui->splash_visible && ui->state->page == MAAGE_PAGE_PLAY && !ui->native.high_contrast;
    DWORD caption = home ? (ui->native.dark ? RGB(83, 114, 129) : RGB(97, 133, 150)) : 0xffffffffUL;
    DWORD text = home ? RGB(255, 255, 255) : 0xffffffffUL;
    int caption_style = home ? (ui->native.dark ? 2 : 1) : 0;
    if (ui->caption_home == caption_style) return;
    ui->caption_home = caption_style;
    DwmSetWindowAttribute(ui->window, MAAGE_DWM_CAPTION_COLOR, &caption, sizeof(caption));
    DwmSetWindowAttribute(ui->window, MAAGE_DWM_TEXT_COLOR, &text, sizeof(text));
}

static double maage_ui_min(double a, double b) { return a < b ? a : b; }
static double maage_ui_max(double a, double b) { return a > b ? a : b; }
static int maage_ui_text_width(maage_launcher_win32 *ui, HDC dc, const wchar_t *text, int pixels, int bold);
static int maage_ui_home_ready(maage_launcher_win32 *ui) {
    return ui->state->can_play && !*ui->state->error;
}
static double maage_ui_home_measure(maage_launcher_win32 *ui, HDC dc, const wchar_t *text, double size, int bold) {
    return maage_ui_text_width(ui, dc, text, maage_ui_scale(ui, size), bold) * 96.0 / (ui->native.dpi ? ui->native.dpi : 96);
}
static void maage_ui_home_layout(maage_launcher_win32 *ui, int width, int height) {
    double rem = maage_ui_min(maage_ui_max(21.0, .021 * width), .044 * width), card_width, card_height, gap, row_gap, group, actions, total, footer, footer_height, heading, tagline, play_width, config_width, top, x, y, fit, center = width * .5;
    double logo, play, config, manage, label_width;
    int columns = 4, rows;
    unsigned i;
    HDC dc;
    if (width < 40 || height < 40) return;
    dc = GetDC(ui->window);
    if (!dc) return;
    /* Match the AppKit common rem and wrap before reducing it to fit. */
    for (;;) {
        label_width = maage_ui_max(maage_ui_home_measure(ui, dc, L"Default Installation", .5625 * rem, 1), maage_ui_home_measure(ui, dc, L"Default Playspace", .5625 * rem, 1));
        card_width = maage_ui_max(10.25 * rem, ceil(label_width + 4.8375 * rem + 5));
        card_height = 1.99375 * rem;
        gap = .84375 * rem;
        row_gap = .5 * rem;
        group = 2 * card_width + gap;
        if (width - 2 * rem <= 700 || 2 * group + gap > width - 2 * rem) columns = 2;
        rows = 4 / columns;
        actions = rows * card_height + (rows - 1) * row_gap;
        logo = 8.4375 * rem;
        heading = 2.55 * rem + 3;
        tagline = 1.10 * rem + 3;
        play = 2.7 * rem;
        config = 1.35 * rem;
        play_width = ceil(maage_ui_home_measure(ui, dc, L"Play MaaGE", 1.125 * rem, 1) + 7.70625 * rem);
        config_width = ceil(maage_ui_home_measure(ui, dc, L"Launcher Configuration", .675 * rem, 1) + 1.9125 * rem);
        footer_height = .57 * rem + 3;
        footer = .3375 * rem + 2 * footer_height + .075 * rem;
        total = logo + heading + .225 * rem + tagline + .84375 * rem + play + 1.25 * rem + actions + 1.06875 * rem + config;
        fit = maage_ui_min(height / (total + footer + 1.5 * rem), width / (group + 2 * rem));
        if (fit >= 1 || rem <= 1) break;
        rem *= maage_ui_min(fit, .995);
    }
    ReleaseDC(ui->window, dc);
    ui->home_rem = rem;
    top = maage_ui_max(rem, maage_ui_min((height - total) * .5, height - footer - .5 * rem - total));
    ui->home_logo = maage_ui_rect(ui, center - logo / 2, top, logo, logo);
    y = top + logo;
    ui->home_heading = maage_ui_rect(ui, rem, y, width - 2 * rem, heading);
    y += heading + .225 * rem;
    ui->home_tagline = maage_ui_rect(ui, rem, y, width - 2 * rem, tagline);
    y += tagline + .84375 * rem;
    maage_ui_place(ui->play, maage_ui_rect(ui, center - play_width / 2, y, play_width, play));
    y += play;
    maage_ui_place(ui->home_status, maage_ui_rect(ui, rem, y, width - 2 * rem, 1.25 * rem));
    y += 1.25 * rem;
    for (i = 0; i < 4; ++i) {
        double card_y = y + (i / columns) * (card_height + row_gap);
        manage = 1.6875 * rem;
        x = center - (columns * card_width + (columns - 1) * gap) / 2 + (i % columns) * (card_width + gap);
        ui->home_cards[i] = maage_ui_rect(ui, x, card_y, card_width, card_height);
        maage_ui_place(ui->home_choose[i], maage_ui_rect(ui, x, card_y, card_width - manage, card_height));
        maage_ui_place(ui->home_manage[i], maage_ui_rect(ui, x + card_width - manage, card_y, manage, card_height));
    }
    y += actions + 1.06875 * rem;
    maage_ui_place(ui->home_settings, maage_ui_rect(ui, center - config_width / 2, y, config_width, config));
    ui->home_notice = maage_ui_rect(ui, .675 * rem, height - .3375 * rem - footer_height, width - 1.35 * rem, footer_height);
    ui->home_version = maage_ui_rect(ui, .675 * rem, height - footer, width - 1.35 * rem, footer_height);
}

static void maage_ui_splash_place(maage_launcher_win32 *ui, HWND control, int x, int y, int w, int h, int visible) {
    if (visible) maage_ui_place(control, maage_ui_rect(ui, x, y, w, h));
    maage_ui_visible(control, visible);
}

static void maage_ui_splash_layout(maage_launcher_win32 *ui, int width, int height) {
    int center = width / 2, flags = ui->splash_flags, preparing = ui->splash_preparing, logo = 144;
    int total, y, available = height - (preparing ? 137 : 38), text_width = width - 48;
    if (text_width > 600) text_width = 600;
    total = logo + 16 + 28 + 6 + 40;
    if (flags & (MAAGE_SPLASH_SIGN_IN | MAAGE_SPLASH_CANCEL | MAAGE_SPLASH_RETRY)) total += 12 + 34;
    if (flags & MAAGE_SPLASH_DEVICE) total += 8 + 30 + 8 + 32;
    if (flags & MAAGE_SPLASH_SKIP) total += 12 + 32;
    if (total > available && logo > 96) {
        int reduction = total - available;
        if (reduction > logo - 96) reduction = logo - 96;
        logo -= reduction;
        total -= reduction;
    }
    y = (available - total) / 2;
    if (y < 4) y = 4;
    ui->splash_logo = maage_ui_rect(ui, center - logo / 2, y, logo, logo);
    y += logo + 16;
    maage_ui_splash_place(ui, ui->splash_message, center - text_width / 2, y, text_width, 28, 1);
    y += 34;
    maage_ui_splash_place(ui, ui->splash_detail, center - text_width / 2, y, text_width, 40, 1);
    y += 52;
    maage_ui_splash_place(ui, ui->splash_sign_in, center - 198, y, 220, 34, flags & MAAGE_SPLASH_SIGN_IN);
    maage_ui_splash_place(ui, ui->splash_device_sign_in, center + 32, y, 166, 34, flags & MAAGE_SPLASH_SIGN_IN);
    maage_ui_splash_place(ui, ui->splash_cancel, center - 85, y, 170, 34, flags & MAAGE_SPLASH_CANCEL);
    maage_ui_splash_place(ui, ui->splash_retry, center - 145, y, 290, 34, flags & MAAGE_SPLASH_RETRY);
    if (flags & (MAAGE_SPLASH_SIGN_IN | MAAGE_SPLASH_CANCEL | MAAGE_SPLASH_RETRY)) y += 42;
    maage_ui_splash_place(ui, ui->splash_code, center - 230, y, 460, 30, flags & MAAGE_SPLASH_DEVICE);
    maage_ui_splash_place(ui, ui->splash_copy, center - 190, y + 38, 155, 32, flags & MAAGE_SPLASH_DEVICE);
    maage_ui_splash_place(ui, ui->splash_open, center - 25, y + 38, 215, 32, flags & MAAGE_SPLASH_DEVICE);
    if (flags & MAAGE_SPLASH_DEVICE) y += 82;
    maage_ui_splash_place(ui, ui->splash_skip, center - 125, y, 250, 32, flags & MAAGE_SPLASH_SKIP);
    maage_ui_splash_place(ui, ui->progress, 28, height - 22, width - 56, 6, flags & MAAGE_SPLASH_PROGRESS);
    maage_ui_splash_place(ui, ui->splash_summary, 28, height - 46, width - 56, 18, preparing);
    maage_ui_splash_place(ui, ui->splash_activity, 28, height - 132, (width - 56) * 3 / 4, 80, preparing);
}

static HFONT maage_ui_home_font(int pixels, int bold) {
    if (pixels < 1) pixels = 1;
    return CreateFontW(-pixels, 0, 0, 0, bold ? FW_SEMIBOLD : FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, ANTIALIASED_QUALITY, DEFAULT_PITCH, L"Segoe UI");
}

static void maage_ui_splash_fonts(maage_launcher_win32 *ui) {
    HFONT title = maage_ui_home_font(maage_ui_scale(ui, 19), 1);
    HFONT detail = maage_ui_home_font(maage_ui_scale(ui, 14), 0);
    HFONT log = CreateFontW(-maage_ui_scale(ui, 12), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, ANTIALIASED_QUALITY, FIXED_PITCH | FF_MODERN, L"Consolas");
    SendMessageW(ui->splash_message, WM_SETFONT, (WPARAM) (title ? title : ui->native.font), FALSE);
    SendMessageW(ui->splash_code, WM_SETFONT, (WPARAM) (title ? title : ui->native.font), FALSE);
    SendMessageW(ui->splash_detail, WM_SETFONT, (WPARAM) (detail ? detail : ui->native.font), FALSE);
    SendMessageW(ui->splash_summary, WM_SETFONT, (WPARAM) (log ? log : ui->native.font), FALSE);
    SendMessageW(ui->splash_activity, WM_SETFONT, (WPARAM) (log ? log : ui->native.font), FALSE);
    if (ui->splash_title_font) DeleteObject(ui->splash_title_font);
    if (ui->splash_detail_font) DeleteObject(ui->splash_detail_font);
    if (ui->splash_log_font) DeleteObject(ui->splash_log_font);
    ui->splash_title_font = title;
    ui->splash_detail_font = detail;
    ui->splash_log_font = log;
}

/* The home uses only a handful of font sizes at any one window size.
 * Reuse GDI font handles during live resize and repeated owner-draw. */
static HFONT maage_ui_cached_font(maage_launcher_win32 *ui, int pixels, int bold) {
    unsigned i, slot;
    HFONT font;
    if (pixels < 1) pixels = 1;
    for (i = 0; i < MAAGE_UI_HOME_FONTS; ++i)
        if (ui->home_fonts[i].handle && ui->home_fonts[i].pixels == pixels && ui->home_fonts[i].bold == bold) return ui->home_fonts[i].handle;
    font = CreateFontW(-pixels, 0, 0, 0, bold == 2 ? FW_BOLD : bold == 3 ? FW_LIGHT : bold ? FW_SEMIBOLD : FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, ANTIALIASED_QUALITY, DEFAULT_PITCH, ui->home_fira ? L"Fira Sans" : L"Segoe UI");
    if (!font) return NULL;
    slot = ui->home_font_next++ % MAAGE_UI_HOME_FONTS;
    if (ui->home_fonts[slot].handle) DeleteObject(ui->home_fonts[slot].handle);
    ui->home_fonts[slot].handle = font;
    ui->home_fonts[slot].pixels = pixels;
    ui->home_fonts[slot].bold = bold;
    return font;
}

static int maage_ui_text_width(maage_launcher_win32 *ui, HDC dc, const wchar_t *text, int pixels, int bold) {
    HFONT font = maage_ui_cached_font(ui, pixels, bold), previous;
    SIZE size = {0, 0};
    if (!font) return 0;
    previous = (HFONT) SelectObject(dc, font);
    GetTextExtentPoint32W(dc, text, lstrlenW(text), &size);
    SelectObject(dc, previous);
    return size.cx;
}

static void maage_ui_home_text(maage_launcher_win32 *ui, HDC dc, RECT rect, const wchar_t *text, int pixels, int bold, COLORREF color, UINT flags, int fit, int shadow) {
    HFONT font, previous;
    int width = rect.right - rect.left, length = lstrlenW(text), needed;
    if (width <= 0 || rect.bottom <= rect.top) return;
    needed = fit ? maage_ui_text_width(ui, dc, text, pixels, bold) : 0;
    if (needed > width) {
        pixels = MulDiv(pixels, width, needed);
        if (pixels < fit) pixels = fit;
    }
    font = maage_ui_cached_font(ui, pixels, bold);
    if (!font) return;
    previous = (HFONT) SelectObject(dc, font);
    SetBkMode(dc, TRANSPARENT);
    if (shadow) {
        RECT offset = rect;
        OffsetRect(&offset, 0, shadow);
        SetTextColor(dc, RGB(34, 63, 69));
        DrawTextW(dc, text, length, &offset, flags | DT_NOPREFIX);
    }
    SetTextColor(dc, color);
    DrawTextW(dc, text, length, &rect, flags | DT_NOPREFIX);
    SelectObject(dc, previous);
}

/* Composite the footer against the actual artwork at the same 55% opacity
 * as AppKit. Reuse a small scratch strip instead of fading the entire frame. */
static void maage_ui_home_footer(maage_launcher_win32 *ui, RECT rect, const wchar_t *text, COLORREF foreground, int shadow) {
    HDC dc = ui->backdrop.dc;
    maage_win32_surface *surface = &ui->footer_surface;
    int width = rect.right - rect.left, height = rect.bottom - rect.top;
    int pixels = maage_ui_scale(ui, .45 * ui->home_rem);
    UINT flags = DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS;
    BLENDFUNCTION blend = {AC_SRC_OVER, 0, 140, 0};
    RECT local;
    if (width <= 0 || height <= 0) return;
    if (!ui->native.high_contrast && maage_ui_scratch(surface, dc, width, height) && BitBlt(surface->dc, 0, 0, width, height, dc, rect.left, rect.top, SRCCOPY)) {
        SetRect(&local, 0, 0, width, height);
        maage_ui_home_text(ui, surface->dc, local, text, pixels, 0, foreground, flags, 1, shadow);
        if (AlphaBlend(dc, rect.left, rect.top, width, height, surface->dc, 0, 0, width, height, blend)) return;
    }
    maage_ui_home_text(ui, dc, rect, text, pixels, 0, foreground, flags, 1, shadow);
}

static COLORREF maage_ui_backdrop_color(maage_launcher_win32 *ui) {
    return maage_launcher_win32_color(&ui->native, 0);
}

static void maage_ui_logo_paint(maage_launcher_win32 *ui, RECT rect, int shadow) {
    if (ui->logo.dc) {
        if (shadow && !ui->native.high_contrast && ui->logo_shadow.dc) {
            RECT shade = rect;
            OffsetRect(&shade, 0, (rect.bottom - rect.top) / 28);
            maage_win32_surface_image(&ui->backdrop, &ui->logo_shadow, shade);
        }
        maage_win32_surface_image(&ui->backdrop, &ui->logo, rect);
    } else if (ui->artwork || ui->native.icons[0]) DrawIconEx(ui->backdrop.dc, rect.left, rect.top, ui->artwork ? ui->artwork : ui->native.icons[0], rect.right - rect.left, rect.bottom - rect.top, 0, NULL, DI_NORMAL);
}

/* Both the parent and its owner-drawn native buttons read the same cached
 * backdrop. The visible window is never cleared before this frame is copied. */
static int maage_ui_backdrop(maage_launcher_win32 *ui, HDC compatible) {
    RECT client, rect;
    HDC dc;
    int height, width, size, i, shadow;
    COLORREF foreground, background;
    UINT centered = DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS;
    GetClientRect(ui->window, &client);
    width = client.right;
    height = client.bottom;
    if (ui->backdrop_width != width || ui->backdrop_height != height) {
        ui->backdrop_dirty = 1;
        ui->backdrop_width = width;
        ui->backdrop_height = height;
    }
    if (!maage_ui_scratch(&ui->backdrop, compatible, width, height)) return 0;
    if (!ui->backdrop_dirty) return 1;
    dc = ui->backdrop.dc;
    background = maage_ui_backdrop_color(ui);
    foreground = ui->native.high_contrast ? GetSysColor(COLOR_WINDOWTEXT) : RGB(255, 255, 255);
    if (ui->splash_visible || ui->native.high_contrast) maage_win32_surface_gradient_rect(&ui->backdrop, width, height, background, background);
    else if (ui->home_background.dc) maage_win32_surface_cover(&ui->backdrop, &ui->home_background, width, height, ui->native.dark);
    else maage_win32_surface_gradient_rect(&ui->backdrop, width, height, ui->native.dark ? RGB(83, 114, 129) : RGB(97, 133, 150), ui->native.dark ? RGB(35, 59, 66) : RGB(41, 69, 77));
    if (ui->splash_visible) {
        rect = ui->splash_logo;
        size = (rect.right - rect.left) * ui->splash_logo_size / 144;
        InflateRect(&rect, (size - (rect.right - rect.left)) / 2, (size - (rect.bottom - rect.top)) / 2);
        maage_ui_logo_paint(ui, rect, 0);
    } else {
        int rem = maage_ui_scale(ui, ui->home_rem), font = maage_ui_scale(ui, .9 * ui->home_rem);
        int rule = maage_ui_scale(ui, 1.51875 * ui->home_rem), gap = maage_ui_scale(ui, .9 * ui->home_rem);
        int available = ui->home_tagline.right - ui->home_tagline.left - 2 * (rule + gap), text_width, mid, left, right;
        const wchar_t *tagline = L"MINECRAFT AS A GAME ENGINE";
        maage_ui_logo_paint(ui, ui->home_logo, 1);
        shadow = ui->native.high_contrast ? 0 : maage_ui_scale(ui, 1);
        maage_ui_home_text(ui, dc, ui->home_heading, L"MaaGE", maage_ui_scale(ui, 2.19375 * ui->home_rem), 2, foreground, centered, 1, shadow);
        text_width = maage_ui_text_width(ui, dc, tagline, font, 3);
        while (font > 1 && text_width > available) text_width = maage_ui_text_width(ui, dc, tagline, --font, 3);
        maage_ui_home_text(ui, dc, ui->home_tagline, tagline, font, 3, foreground, DT_CENTER | DT_VCENTER | DT_SINGLELINE, 1, shadow);
        mid = (ui->home_tagline.top + ui->home_tagline.bottom) / 2;
        left = (width - text_width) / 2 - gap;
        right = (width + text_width) / 2 + gap;
        maage_win32_surface_line(&ui->backdrop, left - rule, mid, left, mid, maage_ui_max(1, .0703125 * rem), foreground);
        maage_win32_surface_line(&ui->backdrop, right, mid, right + rule, mid, maage_ui_max(1, .0703125 * rem), foreground);
        if (!ui->native.high_contrast) {
            rect = maage_ui_child_rect(ui, ui->play);
            OffsetRect(&rect, 0, maage_ui_scale(ui, 5));
            maage_win32_surface_control_shadow(&ui->backdrop, &ui->play_shadow, rect);
            for (i = 0; i < 4; ++i) {
                rect = ui->home_cards[i];
                OffsetRect(&rect, 0, maage_ui_scale(ui, 4));
                maage_win32_surface_control_shadow(&ui->backdrop, &ui->card_shadow, rect);
            }
        }
        maage_ui_home_footer(ui, ui->home_version, L"v" MAAGE_LAUNCHER_VERSION, foreground, shadow);
        maage_ui_home_footer(ui, ui->home_notice, L"MaaGE is NOT an official Minecraft project and is NOT approved by or associated with Mojang.", foreground, shadow);
    }
    ui->backdrop_dirty = 0;
    return 1;
}

static void maage_ui_line(maage_win32_surface *surface, RECT icon, double x1, double y1, double x2, double y2, COLORREF color) {
    double size = icon.right - icon.left;
    maage_win32_surface_line(surface, icon.left + x1 * size, icon.top + y1 * size, icon.left + x2 * size, icon.top + y2 * size, size * 0.085, color);
}

/* Draw independent vector equivalents of AppKit's symbol choices. */
static void maage_ui_icon_arc(maage_win32_surface *surface, RECT r, double cx, double cy, double rx, double ry, double start, double end, COLORREF ink) {
    int i, segments = (int) ceil(fabs(end - start) * 5);
    for (i = 0; i < segments; ++i) {
        double a = start + (end - start) * i / segments, b = start + (end - start) * (i + 1) / segments;
        maage_ui_line(surface, r, cx + rx * cos(a), cy + ry * sin(a), cx + rx * cos(b), cy + ry * sin(b), ink);
    }
}

static void maage_ui_icon_path(maage_win32_surface *surface, RECT r, const double *points, unsigned count, COLORREF ink) {
    unsigned i;
    for (i = 1; i < count; ++i)
        maage_ui_line(surface, r, points[(i - 1) * 2], points[(i - 1) * 2 + 1], points[i * 2], points[i * 2 + 1], ink);
}

static void maage_ui_icon_disk(maage_win32_surface *surface, RECT r, double cx, double cy, double radius, COLORREF ink) {
    double size = r.right - r.left;
    maage_win32_surface_round_f(surface, r.left + (cx - radius) * size, r.top + (cy - radius) * size, r.left + (cx + radius) * size, r.top + (cy + radius) * size, radius * size, ink, 1, 0);
}

static void maage_ui_icon(maage_win32_surface *surface, RECT r, int kind, COLORREF ink) {
    double s = r.right - r.left;
    const double pi = 3.141592653589793;
    COLORREF cutout = (GetRValue(ink) * 299 + GetGValue(ink) * 587 + GetBValue(ink) * 114 >= 150000) ? RGB(30, 60, 44) : RGB(255, 255, 255);
    int i;
    if (s < 2) return;
    GdiFlush();
    switch (kind) {
    case 0: { /* cube */
        const double outline[] = {.50,.08, .86,.28, .86,.72, .50,.92, .14,.72, .14,.28, .50,.08};
        const double folds[] = {.14,.28, .50,.49, .86,.28};
        maage_ui_icon_path(surface, r, outline, 7, ink);
        maage_ui_icon_path(surface, r, folds, 3, ink);
        maage_ui_line(surface, r, .50, .49, .50, .92, ink);
        break;
    }
    case 1: { /* arrow.triangle.2.circlepath */
        const double upper[] = {.92,.25, .84,.45, .64,.36};
        const double lower[] = {.08,.75, .16,.55, .36,.64};
        maage_ui_icon_arc(surface, r, .50, .50, .35, .35, -2.9, -.15, ink);
        maage_ui_icon_arc(surface, r, .50, .50, .35, .35, .24, 2.99, ink);
        maage_ui_icon_path(surface, r, upper, 3, ink);
        maage_ui_icon_path(surface, r, lower, 3, ink);
        break;
    }
    case 2: /* person.2 */
        maage_ui_icon_arc(surface, r, .35, .30, .135, .155, 0, 2*pi, ink);
        maage_ui_icon_arc(surface, r, .73, .34, .115, .135, 0, 2*pi, ink);
        maage_ui_icon_arc(surface, r, .35, .83, .28, .265, pi, 2*pi, ink);
        maage_ui_line(surface, r, .07, .83, .63, .83, ink);
        maage_ui_icon_arc(surface, r, .73, .83, .225, .22, -1.95, 0, ink);
        maage_ui_line(surface, r, .72, .83, .955, .83, ink);
        break;
    case 3: { /* wrench.and.screwdriver */
        const double wrench[] = {.18,.91, .09,.82, .48,.43, .45,.31, .49,.17, .59,.10, .71,.09, .61,.23, .72,.34, .86,.24, .86,.37, .77,.48, .64,.50, .25,.90, .18,.91};
        const double shaft[] = {.35,.45, .10,.20, .10,.10, .20,.10, .45,.35};
        const double handle[] = {.66,.53, .89,.76, .90,.84, .84,.90, .76,.89, .53,.66};
        maage_ui_icon_path(surface, r, wrench, 15, ink);
        maage_ui_icon_path(surface, r, shaft, 5, ink);
        maage_ui_icon_path(surface, r, handle, 6, ink);
        break;
    }
    case 4: {
        const double points[] = {.16,.35, .50,.68, .84,.35};
        maage_ui_icon_path(surface, r, points, 3, ink);
        break;
    }
    case 5: {
        const double points[] = {.34,.16, .67,.50, .34,.84};
        maage_ui_icon_path(surface, r, points, 3, ink);
        break;
    }
    case 6: { /* checkmark.circle.fill */
        const double check[] = {.25,.49, .43,.67, .75,.33};
        maage_ui_icon_disk(surface, r, .50, .50, .46, ink);
        maage_ui_icon_path(surface, r, check, 3, cutout);
        break;
    }
    case 7: /* clock.fill */
        maage_ui_icon_disk(surface, r, .50, .50, .46, ink);
        maage_ui_line(surface, r, .50, .23, .50, .52, cutout);
        maage_ui_line(surface, r, .50, .52, .69, .64, cutout);
        break;
    case 8: /* gearshape */
        for (i = 0; i < 8; ++i) {
            double a = i * pi / 4, b = a + pi / 4;
            double points[8];
            points[0] = .50 + .35*cos(a-.16);
            points[1] = .50 + .35*sin(a-.16);
            points[2] = .50 + .46*cos(a-.12);
            points[3] = .50 + .46*sin(a-.12);
            maage_ui_line(surface, r, points[0], points[1], points[2], points[3], ink);
            maage_ui_line(surface, r, points[2], points[3], .50 + .46*cos(a+.12), .50 + .46*sin(a+.12), ink);
            maage_ui_line(surface, r, .50 + .46*cos(a+.12), .50 + .46*sin(a+.12), .50 + .35*cos(a+.16), .50 + .35*sin(a+.16), ink);
            maage_ui_icon_arc(surface, r, .50, .50, .35, .35, a+.16, b-.16, ink);
        }
        maage_ui_icon_arc(surface, r, .50, .50, .15, .15, 0, 2*pi, ink);
        break;
    case 9: /* play.fill */
        for (i = 0; i < (int) s; ++i) {
            double y = (i + .5) / s, length = .76 * (1.0 - fabs(y - .50) / .43);
            if (length > 0) maage_win32_surface_line(surface, r.left + .12*s, r.top + i + .5, r.left + (.12 + length)*s, r.top + i + .5, 1.0, ink);
        }
        break;
    case 10: /* pause.circle.fill */
        maage_ui_icon_disk(surface, r, .50, .50, .46, ink);
        maage_win32_surface_line(surface, r.left + .38*s, r.top + .29*s, r.left + .38*s, r.top + .71*s, .13*s, cutout);
        maage_win32_surface_line(surface, r.left + .62*s, r.top + .29*s, r.left + .62*s, r.top + .71*s, .13*s, cutout);
        break;
    }
}

static void maage_ui_home_icon(maage_launcher_win32 *ui, maage_win32_surface *surface, RECT rect, int kind, COLORREF color) {
    int asset = kind <= 6 ? kind : kind == 8 ? 7 : kind == 9 ? 8 : -1;
    if (asset >= 0 && ui->home_icons[asset].dc) maage_win32_surface_mask(surface, &ui->home_icons[asset], rect, color, 1);
    else maage_ui_icon(surface, rect, kind, color);
}

static RECT maage_ui_icon_rect(int x, int center, int size) {
    RECT rect;
    rect.left = x;
    rect.top = center - size / 2;
    rect.right = x + size;
    rect.bottom = rect.top + size;
    return rect;
}

static void maage_ui_inline(maage_launcher_win32 *ui, maage_win32_surface *surface, RECT rect, const wchar_t *text, int pixels, int icon_size, int gap, int icon, COLORREF ink, COLORREF symbol) {
    int width = rect.right - rect.left, center = (rect.top + rect.bottom) / 2;
    int needed = maage_ui_text_width(ui, surface->dc, text, pixels, 1), x;
    RECT glyph, caption = rect;
    if (needed > width - icon_size - gap - 2) needed = width - icon_size - gap - 2;
    if (needed < 1) return;
    x = rect.left + (width - icon_size - gap - needed) / 2;
    glyph = maage_ui_icon_rect(x, center, icon_size);
    caption.left = glyph.right + gap;
    caption.right = caption.left + needed + 2;
    maage_ui_home_icon(ui, surface, glyph, icon, symbol);
    maage_ui_home_text(ui, surface->dc, caption, text, pixels, 1, ink, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS, maage_ui_scale(ui, 9), 0);
}

static int maage_ui_is_home_button(maage_launcher_win32 *ui, HWND button) {
    int id = GetDlgCtrlID(button);
    return button == ui->play || button == ui->home_status || button == ui->home_settings || (id >= MAAGE_UI_HOME_CHOOSE && id < MAAGE_UI_HOME_MANAGE + 4);
}

static int maage_ui_home_button_index(maage_launcher_win32 *ui, HWND button) {
    int id;
    if (!button) return -1;
    if (button == ui->play) return 8;
    if (button == ui->home_settings) return 9;
    if (button == ui->home_status) return 10;
    id = GetDlgCtrlID(button);
    if (id >= MAAGE_UI_HOME_CHOOSE && id < MAAGE_UI_HOME_MANAGE + 4) return id - MAAGE_UI_HOME_CHOOSE;
    return -1;
}
static HWND maage_ui_home_button_at(maage_launcher_win32 *ui, int index) {
    if (index < 4) return ui->home_choose[index];
    if (index < 8) return ui->home_manage[index - 4];
    return index == 8 ? ui->play : index == 9 ? ui->home_settings : ui->home_status;
}
static double maage_ui_home_hover_value(maage_launcher_win32 *ui, HWND button, double now) {
    int index = maage_ui_home_button_index(ui, button);
    double target = ui->hot_button == button ? 1 : 0, t;
    if (index < 0 || !ui->animate_splash || !ui->home_hover_started[index]) return target;
    t = (now - ui->home_hover_started[index]) / MAAGE_UI_HOME_HOVER_SECONDS;
    if (t >= 1) return target;
    if (t < 0) t = 0;
    return target + (ui->home_hover_from[index] - target) * (1 - t) * (1 - t);
}
static COLORREF maage_ui_home_color_mix(COLORREF a, COLORREF b, double t) {
    return RGB((int) (GetRValue(a) * (1 - t) + GetRValue(b) * t + .5), (int) (GetGValue(a) * (1 - t) + GetGValue(b) * t + .5), (int) (GetBValue(a) * (1 - t) + GetBValue(b) * t + .5));
}
static int maage_ui_draw_home_button(maage_launcher_win32 *ui, const DRAWITEMSTRUCT *draw) {
    HWND button = draw->hwndItem;
    maage_win32_surface *surface = &ui->button_surface;
    RECT rect, bounds, shape, glyph, caption, tile;
    int id = GetDlgCtrlID(button), width, height, choose, manage, hot, pressed, enabled, center, pixels, card, inset;
    COLORREF fill, ink, foreground = RGB(255, 255, 255), symbol, outline;
    wchar_t text[384], *value, *verified;
    double radius, hover, rem = ui->home_rem * (ui->native.dpi ? ui->native.dpi : 96) / 96.0;
    if (draw->CtlType != ODT_BUTTON || !maage_ui_is_home_button(ui, button)) return 0;
    if (!ui->controls_ready || ui->laying_out || ui->splash_visible || ui->state->page != MAAGE_PAGE_PLAY) return 1;
    bounds = maage_ui_child_rect(ui, button);
    width = bounds.right - bounds.left;
    height = bounds.bottom - bounds.top;
    rect = draw->rcItem;
    if (!maage_ui_backdrop(ui, draw->hDC) || !maage_ui_scratch(surface, draw->hDC, width, height)) {
        FillRect(draw->hDC, &rect, GetSysColorBrush(COLOR_BTNFACE));
        GetWindowTextW(button, text, 384);
        SetTextColor(draw->hDC, GetSysColor(COLOR_BTNTEXT));
        SetBkMode(draw->hDC, TRANSPARENT);
        DrawTextW(draw->hDC, text, -1, &rect, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
        return 1;
    }
    BitBlt(surface->dc, 0, 0, width, height, ui->backdrop.dc, bounds.left, bounds.top, SRCCOPY);
    SetRect(&rect, 0, 0, width, height);
    shape = rect;
    center = height / 2;
    choose = id >= MAAGE_UI_HOME_CHOOSE && id < MAAGE_UI_HOME_CHOOSE + 4;
    manage = id >= MAAGE_UI_HOME_MANAGE && id < MAAGE_UI_HOME_MANAGE + 4;
    hot = ui->hot_button == button;
    pressed = (draw->itemState & ODS_SELECTED) != 0;
    enabled = IsWindowEnabled(button);
    if (!enabled) hot = 0;
    hover = enabled ? maage_ui_home_hover_value(ui, button, maage_time_monotonic_seconds()) : 0;
    GetWindowTextW(button, text, 384);
    if (choose || manage) {
        card = id - (choose ? MAAGE_UI_HOME_CHOOSE : MAAGE_UI_HOME_MANAGE);
        shape = ui->home_cards[card];
        OffsetRect(&shape, -bounds.left, -bounds.top);
        radius = .5625 * rem;
        fill = ui->native.dark ? RGB(48, 54, 48) : RGB(255, 255, 255);
        ink = ui->native.dark ? RGB(240, 240, 240) : RGB(48, 48, 48);
        symbol = ui->native.dark ? RGB(148, 204, 117) : RGB(54, 89, 37);
        outline = ui->native.dark ? RGB(209, 209, 209) : RGB(80, 80, 80);
        if (!enabled) ink = ui->native.dark ? RGB(160, 160, 160) : RGB(115, 115, 115);
        if (ui->native.high_contrast) {
            fill = GetSysColor(hot ? COLOR_HIGHLIGHT : COLOR_BTNFACE);
            ink = GetSysColor(!enabled ? COLOR_GRAYTEXT : hot ? COLOR_HIGHLIGHTTEXT : COLOR_BTNTEXT);
            symbol = outline = ink;
        }
        maage_win32_surface_round(surface, shape, radius, fill, ui->native.high_contrast ? 1 : ui->native.dark ? .88 : .65, 0);
        if (!ui->native.high_contrast && (hover > 0 || pressed))
            maage_win32_surface_round(surface, shape, radius, RGB(255, 255, 255), pressed ? (ui->native.dark ? .08 : .18) : (ui->native.dark ? .16 : .35) * hover, 0);
        if (choose) {
            int tile_size = (int) (1.2375 * rem + .5), icon_size = (int) (.9 * rem + .5), arrow_size = (int) (.7875 * rem + .5);
            int padding = (int) (.3375 * rem + .5);
            tile = maage_ui_icon_rect(padding, center, tile_size);
            if (!ui->native.high_contrast) maage_win32_surface_round(surface, tile, tile_size * .5, RGB(103, 161, 77), ui->native.dark ? .26 : .2, 0);
            glyph = maage_ui_icon_rect(tile.left + (tile_size - icon_size) / 2, center, icon_size);
            maage_ui_home_icon(ui, surface, glyph, card, enabled ? symbol : ink);
            glyph = maage_ui_icon_rect(width - arrow_size - padding, center, arrow_size);
            maage_ui_home_icon(ui, surface, glyph, 4, ink);
            caption = rect;
            caption.left = tile.right + padding;
            caption.right = glyph.left - (int) (.1125 * rem + .5);
            value = wcsstr(text, L": ");
            value = value ? value + 2 : text;
            verified = card == 2 ? wcsstr(value, L" ✓") : NULL;
            pixels = (int) (.5625 * rem + .5);
            if (verified) {
                int check_size = (int) (.62 * rem + .5), badge_gap = (int) (.12 * rem + .5), available, needed;
                *verified = 0;
                available = caption.right - caption.left - check_size - badge_gap;
                needed = maage_ui_text_width(ui, surface->dc, value, pixels, 1) + 2;
                caption.right = caption.left + (int) maage_ui_max(0, maage_ui_min(needed, available));
                glyph = maage_ui_icon_rect(caption.right + badge_gap, center, check_size);
                if (!ui->native.high_contrast && ui->home_icons[6].dc) {
                    RECT shade = glyph;
                    OffsetRect(&shade, 0, 1);
                    maage_win32_surface_mask(surface, &ui->home_icons[6], shade, RGB(0, 0, 0), .22);
                }
                maage_ui_home_icon(ui, surface, glyph, 6, ui->native.high_contrast ? ink : ui->native.dark ? RGB(99, 204, 110) : RGB(43, 158, 66));
            }
            maage_ui_home_text(ui, surface->dc, caption, value, pixels, 1, ink, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS, 0, 0);
        } else {
            RECT divider = rect;
            divider.right = 1;
            maage_win32_surface_round(surface, divider, 0, ui->native.high_contrast ? outline : ui->native.dark ? RGB(255, 255, 255) : RGB(0, 0, 0), ui->native.high_contrast ? 1 : ui->native.dark ? .16 : .1, 0);
            pixels = (int) (1.06875 * rem + .5);
            glyph = maage_ui_icon_rect((width - pixels) / 2, center, pixels);
            maage_ui_home_icon(ui, surface, glyph, 5, outline);
        }
    } else if (button == ui->play) {
        fill = !enabled ? RGB(97, 122, 99) : pressed ? RGB(87, 139, 64) : maage_ui_home_color_mix(RGB(103, 161, 77), RGB(121, 180, 94), hover);
        ink = enabled ? foreground : RGB(220, 230, 219);
        if (ui->native.high_contrast) {
            fill = GetSysColor(hot ? COLOR_HIGHLIGHT : COLOR_BTNFACE);
            ink = GetSysColor(!enabled ? COLOR_GRAYTEXT : hot ? COLOR_HIGHLIGHTTEXT : COLOR_BTNTEXT);
        }
        maage_win32_surface_round(surface, rect, height * .5, fill, 1, 0);
        maage_ui_inline(ui, surface, rect, text, (int) (1.125 * rem + .5), (int) (1.8 * rem + .5), (int) (.28125 * rem + .5), 9, ink, ink);
    } else {
        ink = ui->native.high_contrast ? GetSysColor(COLOR_WINDOWTEXT) : foreground;
        symbol = ink;
        if ((hover > 0 || pressed) && enabled) maage_win32_surface_round(surface, rect, .39375 * rem, ui->native.high_contrast ? GetSysColor(COLOR_HIGHLIGHT) : RGB(255, 255, 255), ui->native.high_contrast ? 1 : pressed ? .05 : .1 * hover, 0);
        if (hot && ui->native.high_contrast) ink = symbol = GetSysColor(COLOR_HIGHLIGHTTEXT);
        if (button == ui->home_status) {
            value = !wcsncmp(text, L"✓ ", 2) ? text + 2 : text;
            if (ui->state->can_play && !*ui->state->error && !ui->native.high_contrast) symbol = RGB(122, 207, 89);
            maage_ui_inline(ui, surface, rect, value, (int) (.5625 * rem + .5), (int) (.5625 * rem + .5), (int) (.15 * rem + .5), ui->state->can_play && !*ui->state->error ? 6 : ui->state->client_process ? 10 : 7, ink, symbol);
        } else {
            if (!ui->native.high_contrast) ink = maage_ui_home_color_mix(RGB(192, 192, 192), foreground, hover);
            maage_ui_inline(ui, surface, rect, text, (int) (.675 * rem + .5), (int) (.9 * rem + .5), (int) (.3375 * rem + .5), 8, ink, ink);
        }
    }
    if ((draw->itemState & ODS_FOCUS) && !(SendMessageW(button, WM_QUERYUISTATE, 0, 0) & UISF_HIDEFOCUS)) {
        inset = maage_ui_scale(ui, 4);
        InflateRect(&rect, -inset, -inset);
        DrawFocusRect(surface->dc, &rect);
    }
    BitBlt(draw->hDC, 0, 0, width, height, surface->dc, 0, 0, SRCCOPY);
    return 1;
}

static double maage_ui_progress_value(maage_launcher_win32 *ui, double now) {
    double t;
    if (ui->splash_percent < 0) return -1;
    if (!ui->animate_splash) return ui->splash_percent;
    t = (now - ui->progress_started) / MAAGE_UI_PROGRESS_SECONDS;
    if (t >= 1.0) return ui->splash_percent;
    if (t < 0) t = 0;
    t = 1.0 - (1.0 - t) * (1.0 - t) * (1.0 - t);
    return ui->progress_from + (ui->splash_percent - ui->progress_from) * t;
}

static void maage_ui_progress_paint(maage_launcher_win32 *ui, HWND control, HDC dc) {
    maage_win32_surface *surface = &ui->button_surface;
    RECT rect;
    int width, height;
    double phase, left = 0, right, length, value, now = maage_time_monotonic_seconds();
    COLORREF track = ui->native.high_contrast ? GetSysColor(COLOR_3DSHADOW) : ui->native.dark ? RGB(66, 66, 66) : RGB(218, 222, 218);
    COLORREF color = ui->native.high_contrast ? GetSysColor(COLOR_HIGHLIGHT) : RGB(103, 161, 77);
    GetClientRect(control, &rect);
    width = rect.right;
    height = rect.bottom;
    if (!maage_ui_scratch(surface, dc, width, height)) {
        FillRect(dc, &rect, ui->background);
        return;
    }
    FillRect(surface->dc, &rect, ui->background);
    maage_win32_surface_round(surface, rect, height * .5, track, 1, 0);
    value = maage_ui_progress_value(ui, now);
    if (value >= 0) right = width * value / 100.0;
    else {
        length = width * .25;
        /* Continuous velocity at both ends, with fractional-pixel positioning. */
        phase = ui->animate_splash ? .5 - .5 * cos((now - ui->splash_started) * 3.141592653589793 / 1.65) : .5;
        left = (width - length) * phase;
        right = left + length;
    }
    maage_win32_surface_round_f(surface, left, 0, right, height, height * .5, color, 1, 0);
    BitBlt(dc, 0, 0, width, height, surface->dc, 0, 0, SRCCOPY);
}

/* The controls still own native input, focus and accessibility. */
static void maage_ui_home_hover(maage_launcher_win32 *ui) {
    POINT point;
    HWND target = NULL, previous = ui->hot_button;
    int old_index, new_index;
    double now;
    if (ui->controls_ready && !ui->splash_visible && !ui->menu_open && !ui->transition_active && ui->state->page == MAAGE_PAGE_PLAY && GetForegroundWindow() == ui->window && GetCursorPos(&point)) {
        HWND hit = WindowFromPoint(point);
        if (hit && GetParent(hit) == ui->window && maage_ui_is_home_button(ui, hit) && IsWindowEnabled(hit) && IsWindowVisible(hit)) target = hit;
    }
    if (previous == target) return;
    now = maage_time_monotonic_seconds();
    old_index = maage_ui_home_button_index(ui, previous);
    new_index = maage_ui_home_button_index(ui, target);
    if (old_index >= 0) {
        ui->home_hover_from[old_index] = maage_ui_home_hover_value(ui, previous, now);
        ui->home_hover_started[old_index] = ui->animate_splash ? now : 0;
    }
    if (new_index >= 0) {
        ui->home_hover_from[new_index] = maage_ui_home_hover_value(ui, target, now);
        ui->home_hover_started[new_index] = ui->animate_splash ? now : 0;
    }
    ui->hot_button = target;
    if (ui->animate_splash && (old_index >= 0 || new_index >= 0)) {
        if (!SetTimer(ui->window, MAAGE_UI_ANIMATION_TIMER, MAAGE_UI_ANIMATION_INTERVAL, NULL)) {
            if (old_index >= 0) ui->home_hover_started[old_index] = 0;
            if (new_index >= 0) ui->home_hover_started[new_index] = 0;
        }
    }
    if (previous) InvalidateRect(previous, NULL, FALSE);
    if (target) InvalidateRect(target, NULL, FALSE);
}

static LRESULT CALLBACK maage_ui_painted_control(HWND control, UINT message, WPARAM wparam, LPARAM lparam, UINT_PTR id, DWORD_PTR reference) {
    maage_launcher_win32 *ui = (maage_launcher_win32 *) reference;
    if (message == WM_NCDESTROY) {
        if (ui->hot_button == control) ui->hot_button = NULL;
        RemoveWindowSubclass(control, maage_ui_painted_control, id);
    } else if (control == ui->progress) {
        if (ui->splash_visible && message == WM_ERASEBKGND) return 1;
        if (ui->splash_visible && message == WM_PAINT) {
            PAINTSTRUCT paint;
            HDC dc = BeginPaint(control, &paint);
            maage_ui_progress_paint(ui, control, dc);
            EndPaint(control, &paint);
            return 0;
        }
        if (ui->splash_visible && message == WM_PRINTCLIENT) {
            maage_ui_progress_paint(ui, control, (HDC) wparam);
            return 0;
        }
    } else if (message == WM_ERASEBKGND) return 1;
    else if (message == WM_MOUSEMOVE) {
        TRACKMOUSEEVENT track;
        memset(&track, 0, sizeof(track));
        track.cbSize = sizeof(track);
        track.dwFlags = TME_LEAVE;
        track.hwndTrack = control;
        TrackMouseEvent(&track);
        maage_ui_home_hover(ui);
    } else if (message == WM_MOUSELEAVE || message == WM_SHOWWINDOW || message == WM_ENABLE || message == WM_LBUTTONUP || message == WM_CAPTURECHANGED) {
        maage_ui_home_hover(ui);
    } else if (message == WM_SETCURSOR && IsWindowEnabled(control)) {
        SetCursor(LoadCursorW(NULL, IDC_HAND));
        return TRUE;
    }
    return DefSubclassProc(control, message, wparam, lparam);
}

/* Installation management uses a two-pane editor when space allows. On compact
 * windows it stacks the panes and scrolls the content; the native list keeps its
 * own keyboard, accessibility and selection behavior. All positions are 96-DPI. */
static void maage_ui_installation_place(maage_launcher_win32 *ui, HWND control, int x, int y, int width, int height, int offset, int bottom) {
    int shown_height, visible;
    if (!control) return;
    y += 52;
    y -= offset;
    shown_height = control == ui->list ? height : (control == ui->clients || control == ui->client_choice) ? 32 : height;
    visible = y >= 130 && y + shown_height <= bottom;
    if (control == ui->list) {
        if (y < 130) {
            height -= 130 - y;
            y = 130;
        }
        if (y + height > bottom) height = bottom - y;
        visible = height >= 48;
    }
    if (control == ui->cancel_button && !ui->creating) visible = 0;
    if (control == ui->remove_button && ui->creating) visible = 0;
    if (visible) maage_ui_place(control, maage_ui_rect(ui, x, y, width, height));
    maage_ui_visible(control, visible);
}

static void maage_ui_installations_layout(maage_launcher_win32 *ui, int width, int height) {
    int left = 198, full = width - 230, stacked = full < 680, bottom = height - 120;
    int list_width = stacked ? full : full * 42 / 100;
    int editor_x = stacked ? left : left + list_width + 24;
    int editor_width = stacked ? full : full - list_width - 24;
    int editor_y = stacked ? 580 : 226;
    int offset = ui->scroll[MAAGE_PAGE_INSTALLATIONS];
    int action_width = (editor_width - 20) / 3, secondary_width = (editor_width - 10) / 2;
    int client_width = stacked ? (full - 20) / 3 : 182;
    maage_ui_text(ui->list_hint, stacked ? "Select to edit, then scroll down." : "Select to edit; double-click to activate.");
    maage_ui_installation_place(ui, ui->clients_label, left, 78, full, 26, offset, bottom);
    maage_ui_installation_place(ui, ui->clients, left, 110, full, 240, offset, bottom);
    maage_ui_installation_place(ui, ui->clients_import, left, 151, client_width, 32, offset, bottom);
    maage_ui_installation_place(ui, ui->clients_replace, left + client_width + 10, 151, client_width, 32, offset, bottom);
    maage_ui_installation_place(ui, ui->clients_remove, left + (ui->clients_replace ? 2 * (client_width + 10) : 0), 151, client_width, 32, offset, bottom);
    maage_ui_installation_place(ui, ui->client_status, left, 188, full, 30, offset, bottom);
    maage_ui_installation_place(ui, ui->installations_label, left, 224, list_width - 120, 32, offset, bottom);
    maage_ui_installation_place(ui, ui->new_button, left + list_width - 116, 224, 116, 32, offset, bottom);
    maage_ui_installation_place(ui, ui->list_hint, left, 263, list_width, 28, offset, bottom);
    maage_ui_installation_place(ui, ui->list, left, 299, list_width, stacked ? 235 : 280, offset, bottom);
    maage_ui_installation_place(ui, ui->editor_heading, editor_x, editor_y, editor_width, 32, offset, bottom);
    maage_ui_installation_place(ui, ui->editor_hint, editor_x, editor_y + 38, editor_width, 42, offset, bottom);
    maage_ui_installation_place(ui, ui->name_label, editor_x, editor_y + 88, editor_width, 24, offset, bottom);
    maage_ui_installation_place(ui, ui->name, editor_x, editor_y + 112, editor_width, 32, offset, bottom);
    maage_ui_installation_place(ui, ui->client_label, editor_x, editor_y + 158, editor_width, 24, offset, bottom);
    maage_ui_installation_place(ui, ui->client_choice, editor_x, editor_y + 184, editor_width, 220, offset, bottom);
    maage_ui_installation_place(ui, ui->arguments_label, editor_x, editor_y + 232, editor_width, 24, offset, bottom);
    maage_ui_installation_place(ui, ui->arguments, editor_x, editor_y + 258, editor_width, 32, offset, bottom);
    maage_ui_installation_place(ui, ui->save_button, editor_x, editor_y + 311, action_width, 32, offset, bottom);
    maage_ui_installation_place(ui, ui->duplicate_button, editor_x + action_width + 10, editor_y + 311, action_width, 32, offset, bottom);
    maage_ui_installation_place(ui, ui->remove_button, editor_x + 2 * (action_width + 10), editor_y + 311, action_width, 32, offset, bottom);
    maage_ui_installation_place(ui, ui->cancel_button, editor_x + 2 * (action_width + 10), editor_y + 311, action_width, 32, offset, bottom);
    maage_ui_installation_place(ui, ui->default_button, editor_x, editor_y + 353, secondary_width, 32, offset, bottom);
    maage_ui_installation_place(ui, ui->active_button, editor_x + secondary_width + 10, editor_y + 353, secondary_width, 32, offset, bottom);
    maage_ui_installation_place(ui, ui->folder_button, editor_x, editor_y + 396, editor_width < 245 ? editor_width : 245, 32, offset, bottom);
    {
        int first = maage_launcher_win32_scale(&ui->native, list_width - 132), second = maage_launcher_win32_scale(&ui->native, 120);
        if (ListView_GetColumnWidth(ui->list, 0) != first) ListView_SetColumnWidth(ui->list, 0, first);
        if (ListView_GetColumnWidth(ui->list, 1) != second) ListView_SetColumnWidth(ui->list, 1, second);
    }
}

/* Only the main content scrolls. Sidebar navigation and the status footer
 * remain visible, even when a compact window needs additional page height. */
/* The page beneath a fade is always the real live native UI. The outgoing
 * client-area frame is captured through native printing, not screen capture;
 * its owned layered surface is noninteractive and never takes focus. */
static void maage_ui_paint_client(maage_launcher_win32 *ui, HDC dc, RECT rect) {
    if (ui->controls_ready && (ui->splash_visible || ui->state->page == MAAGE_PAGE_PLAY)) {
        if (maage_ui_backdrop(ui, dc)) BitBlt(dc, rect.left, rect.top, rect.right - rect.left, rect.bottom - rect.top, ui->backdrop.dc, rect.left, rect.top, SRCCOPY);
        else FillRect(dc, &rect, ui->background);
    } else if (ui->background) {
        RECT sidebar = rect;
        FillRect(dc, &rect, ui->background);
        if (sidebar.right > maage_launcher_win32_scale(&ui->native, 170)) sidebar.right = maage_launcher_win32_scale(&ui->native, 170);
        if (sidebar.right > sidebar.left) FillRect(dc, &sidebar, ui->sidebar);
    }
}

static void maage_ui_transition_cancel(maage_launcher_win32 *ui) {
    ui->transition_active = 0;
    if (ui->transition_window && IsWindowVisible(ui->transition_window)) ShowWindow(ui->transition_window, SW_HIDE);
    maage_win32_surface_destroy(&ui->transition);
}

static LRESULT CALLBACK maage_ui_transition_proc(HWND window, UINT message, WPARAM wparam, LPARAM lparam, UINT_PTR id, DWORD_PTR reference) {
    maage_launcher_win32 *ui = (maage_launcher_win32 *) reference;
    switch (message) {
    case WM_NCHITTEST:
        return HTTRANSPARENT;
    case WM_MOUSEACTIVATE:
        return MA_NOACTIVATE;
    case WM_ERASEBKGND:
        return 1;
    case WM_PAINT: {
        PAINTSTRUCT paint;
        HDC dc = BeginPaint(window, &paint);
        RECT rect;
        GetClientRect(window, &rect);
        if (ui->transition.dc) {
            if (rect.right == ui->transition.width && rect.bottom == ui->transition.height) BitBlt(dc, 0, 0, rect.right, rect.bottom, ui->transition.dc, 0, 0, SRCCOPY);
            else StretchBlt(dc, 0, 0, rect.right, rect.bottom, ui->transition.dc, 0, 0, ui->transition.width, ui->transition.height, SRCCOPY);
        }
        EndPaint(window, &paint);
        return 0;
    }
    case WM_NCDESTROY:
        RemoveWindowSubclass(window, maage_ui_transition_proc, id);
        ui->transition_window = NULL;
        break;
    }
    return DefSubclassProc(window, message, wparam, lparam);
}

static int maage_ui_transition_position(maage_launcher_win32 *ui) {
    RECT client;
    POINT origin = {0, 0};
    GetClientRect(ui->window, &client);
    ClientToScreen(ui->window, &origin);
    return SetWindowPos(ui->transition_window, HWND_TOP, origin.x, origin.y, client.right, client.bottom, SWP_NOACTIVATE | SWP_SHOWWINDOW | SWP_NOOWNERZORDER);
}

static void maage_ui_transition_capture(maage_launcher_win32 *ui) {
    RECT client;
    unsigned i;
    HDC dc;
    maage_ui_transition_cancel(ui);
    if (!ui->transition_window || !ui->animate_splash || ui->native.high_contrast || !ui->controls_ready || ui->live_resize || !IsWindowVisible(ui->window) || IsIconic(ui->window) || GetActiveWindow() != ui->window) return;
    GetClientRect(ui->window, &client);
    if (!maage_win32_surface_resize(&ui->transition, NULL, client.right, client.bottom)) return;
    dc = ui->transition.dc;
    maage_ui_paint_client(ui, dc, client);
    for (i = 0; i < ui->native.count; ++i) {
        HWND child = ui->native.controls[i].handle;
        RECT bounds, local, visible;
        int saved;
        if (!child || !IsWindowVisible(child)) continue;
        bounds = maage_ui_child_rect(ui, child);
        if (!IntersectRect(&visible, &bounds, &client)) continue;
        saved = SaveDC(dc);
        if (!saved) continue;
        SetViewportOrgEx(dc, bounds.left, bounds.top, NULL);
        GetClientRect(child, &local);
        IntersectClipRect(dc, 0, 0, bounds.right - bounds.left, bounds.bottom - bounds.top);
        if (!ui->splash_visible && ui->state->page == MAAGE_PAGE_PLAY && maage_ui_is_home_button(ui, child)) {
            DRAWITEMSTRUCT draw;
            memset(&draw, 0, sizeof(draw));
            draw.CtlType = ODT_BUTTON;
            draw.CtlID = GetDlgCtrlID(child);
            draw.itemAction = ODA_DRAWENTIRE;
            draw.itemState = (!IsWindowEnabled(child) ? ODS_DISABLED : 0) | (GetFocus() == child ? ODS_FOCUS : 0);
            draw.hwndItem = child;
            draw.hDC = dc;
            draw.rcItem = local;
            maage_ui_draw_home_button(ui, &draw);
        } else SendMessageW(child, WM_PRINT, (WPARAM) dc, PRF_CLIENT | PRF_NONCLIENT | PRF_ERASEBKGND | PRF_CHILDREN);
        RestoreDC(dc, saved);
    }
    GdiFlush();
    if (!SetLayeredWindowAttributes(ui->transition_window, 0, 255, LWA_ALPHA) ||
        !maage_ui_transition_position(ui)) {
        maage_ui_transition_cancel(ui);
        return;
    }
    RedrawWindow(ui->transition_window, NULL, NULL, RDW_INVALIDATE | RDW_UPDATENOW | RDW_NOERASE);
}

static void maage_ui_transition_start(maage_launcher_win32 *ui) {
    if (!ui->transition.dc || !ui->transition_window) return;
    if (GetActiveWindow() != ui->window || !maage_ui_transition_position(ui) ||
        !SetTimer(ui->window, MAAGE_UI_ANIMATION_TIMER, MAAGE_UI_ANIMATION_INTERVAL, NULL)) {
        maage_ui_transition_cancel(ui);
        return;
    }
    RedrawWindow(ui->window, NULL, NULL, RDW_UPDATENOW | RDW_ALLCHILDREN);
    ui->transition_started = maage_time_monotonic_seconds();
    ui->transition_active = 1;
    InvalidateRect(ui->transition_window, NULL, FALSE);
}

static void maage_ui_layout(maage_launcher_win32 *ui) {
    RECT rect;
    SCROLLINFO scroll;
    unsigned i;
    int width, height, x, y, w, h, page;
    int top = 124, bottom, content_bottom = 0, max_scroll, visible_scroll, visible;
    if (!ui->controls_ready || ui->laying_out || IsIconic(ui->window)) return;
    ui->laying_out = 1;
    ui->backdrop_dirty = 1;
    GetClientRect(ui->window, &rect);
    width = MulDiv(rect.right, 96, ui->native.dpi ? (int) ui->native.dpi : 96);
    height = MulDiv(rect.bottom, 96, ui->native.dpi ? (int) ui->native.dpi : 96);
    page = (int) ui->state->page;
    bottom = height - 120;
    if (ui->splash_visible) content_bottom = 0;
    else if (page == MAAGE_PAGE_INSTALLATIONS) content_bottom = width - 230 < 680 ? 1072 : 707;
    else if (page == MAAGE_PAGE_ACCOUNTS) content_bottom = 610;
    else if (page == MAAGE_PAGE_SETTINGS) content_bottom = 566;
    else if (page == MAAGE_PAGE_ABOUT) content_bottom = 584;
    max_scroll = content_bottom - bottom;
    if (max_scroll < 0) max_scroll = 0;
    if (ui->splash_visible || page == MAAGE_PAGE_PLAY) max_scroll = 0;
    if (ui->scroll[page] > max_scroll) ui->scroll[page] = max_scroll;
    visible_scroll = max_scroll > 0;
    if (ui->scrollbar_visible != visible_scroll) {
        ui->scrollbar_visible = visible_scroll;
        ShowScrollBar(ui->window, SB_VERT, visible_scroll);
        GetClientRect(ui->window, &rect);
        width = MulDiv(rect.right, 96, ui->native.dpi ? (int) ui->native.dpi : 96);
        if (!ui->splash_visible && page == MAAGE_PAGE_INSTALLATIONS) {
            content_bottom = width - 230 < 680 ? 1072 : 707;
            max_scroll = content_bottom - bottom;
            if (max_scroll < 0) max_scroll = 0;
            if (ui->scroll[page] > max_scroll) ui->scroll[page] = max_scroll;
        }
    }
    memset(&scroll, 0, sizeof(scroll));
    scroll.cbSize = sizeof(scroll);
    scroll.fMask = SIF_RANGE | SIF_PAGE | SIF_POS;
    scroll.nMin = 0;
    scroll.nMax = max_scroll + bottom - top - 1;
    scroll.nPage = (UINT) (bottom > top ? bottom - top : 1);
    scroll.nPos = ui->scroll[page];
    if (!ui->splash_visible && page != MAAGE_PAGE_PLAY) SetScrollInfo(ui->window, SB_VERT, &scroll, TRUE);
    for (i = 0; i < ui->native.count; ++i) {
        maage_launcher_control *c = &ui->native.controls[i];
        x = c->x;
        y = c->y;
        w = c->width;
        h = c->height;
        visible = !ui->splash_visible && page != MAAGE_PAGE_PLAY && (c->page == -1 || c->page == page);
        if (c->page <= MAAGE_UI_HOME) {
            if (c->page == MAAGE_UI_SPLASH && ui->splash_visible) continue;
            if (c->page == MAAGE_UI_HOME && !ui->splash_visible && page == MAAGE_PAGE_PLAY) continue;
            maage_ui_visible(c->handle, 0);
            continue;
        }
        if (c->handle == ui->progress) {
            if (ui->splash_visible) continue;
            if (page == MAAGE_PAGE_PLAY) {
                maage_ui_visible(c->handle, 0);
                continue;
            }
            visible = ui->state->progress.busy || ui->state->accounts.busy;
        }
        if (c->page == MAAGE_PAGE_INSTALLATIONS && c->y != 80) {
            if (page != MAAGE_PAGE_INSTALLATIONS || ui->splash_visible) maage_ui_visible(c->handle, 0);
            continue;
        }
        if (y < 0) y += height;
        if (x < 0) x += width;
        if (w < 0) w += width;
        if (h < 0) h += height;
        /* Both account-action rows must fit the shared compact content width,
         * even after the native vertical scrollbar reduces the client area. */
        if (c->page == MAAGE_PAGE_ACCOUNTS && width < 800 && (c->y == 382 || c->y == 426)) {
            int available = width - 230, first = (available - 20) * 36 / 100, second = (available - 20 - first) / 2;
            int index = c->handle == ui->sign_in || c->handle == ui->sign_out ? 0 : c->handle == ui->device_sign_in || c->handle == ui->remove_account ? 1 : 2;
            x = index == 0 ? 198 : index == 1 ? 198 + first + 10 : 198 + first + second + 20;
            w = index == 0 ? first : index == 1 ? second : available - first - second - 20;
        }
        if (c->page >= MAAGE_PAGE_INSTALLATIONS && c->page == page && c->y != 80) {
            int shown_height = c->handle == ui->appearance ? 32 : h;
            y -= ui->scroll[page];
            if (y < top || y + shown_height > bottom) visible = 0;
        }
        if (visible) maage_ui_place(c->handle, maage_ui_rect(ui, x, y, w, h));
        maage_ui_visible(c->handle, visible);
    }
    if (ui->splash_visible) maage_ui_splash_layout(ui, width, height);
    else if (page == MAAGE_PAGE_PLAY) {
        maage_ui_home_layout(ui, width, height);
        for (i = 0; i < ui->native.count; ++i)
            if (ui->native.controls[i].page == MAAGE_UI_HOME) maage_ui_visible(ui->native.controls[i].handle, ui->native.controls[i].handle != ui->home_status || !maage_ui_home_ready(ui));
    }
    else if (page == MAAGE_PAGE_INSTALLATIONS) maage_ui_installations_layout(ui, width, height);
    if (!ui->splash_visible && page == MAAGE_PAGE_ACCOUNTS) {
        int first = maage_launcher_win32_scale(&ui->native, (width - 230) * 36 / 100), second = maage_launcher_win32_scale(&ui->native, (width - 230) * 60 / 100);
        if (ListView_GetColumnWidth(ui->profiles, 0) != first) ListView_SetColumnWidth(ui->profiles, 0, first);
        if (ListView_GetColumnWidth(ui->profiles, 1) != second) ListView_SetColumnWidth(ui->profiles, 1, second);
    }
    if (!ui->splash_visible && page != MAAGE_PAGE_PLAY)
        for (i = 0; i < 5; ++i) {
            LRESULT checked = i == (unsigned) page ? BST_CHECKED : BST_UNCHECKED;
            if (SendMessageW(ui->navigation[i], BM_GETCHECK, 0, 0) != checked) SendMessageW(ui->navigation[i], BM_SETCHECK, checked, 0);
        }
    ui->laying_out = 0;
    maage_ui_home_hover(ui);
    ui->backdrop_dirty = 1;
    maage_ui_caption(ui);
    RedrawWindow(ui->window, NULL, NULL, RDW_INVALIDATE | RDW_ALLCHILDREN | RDW_NOERASE);
}
static void maage_ui_navigate(maage_launcher_win32 *ui, maage_launcher_page page) {
    if (ui->state->page == page) return;
    if (!ui->splash_visible) maage_ui_transition_capture(ui);
    ui->hot_button = NULL;
    ui->state->page = page;
    maage_ui_layout(ui);
    maage_ui_transition_start(ui);
}

static int maage_ui_selected(maage_launcher_win32 *ui) {
    int row = ListView_GetNextItem(ui->list, -1, LVNI_SELECTED);
    if (ui->creating) return -1;
    if (row >= 0 && (unsigned) row < ui->state->count) return row;
    return ui->editor_row >= 0 && (unsigned) ui->editor_row < ui->state->count ? ui->editor_row : -1;
}
static void maage_ui_client_choices(maage_launcher_win32 *ui, int row);
static void maage_ui_editor_bind(maage_launcher_win32 *ui);
static void maage_ui_installations_reload(maage_launcher_win32 *ui, int preferred) {
    unsigned i;
    int active = -1, row;
    wchar_t wide[160];
    LVITEMW item;
    ui->updating = 1;
    ListView_DeleteAllItems(ui->list);
    for (i = 0; i < ui->state->count; ++i) {
        maage_installation *entry = &ui->state->installations[i];
        MultiByteToWideChar(CP_UTF8, 0, entry->name, -1, wide, 160);
        memset(&item, 0, sizeof(item));
        item.mask = LVIF_TEXT;
        item.iItem = (int) i;
        item.pszText = wide;
        SendMessageW(ui->list, LVM_INSERTITEMW, 0, (LPARAM) &item);
        item.iSubItem = 1;
        item.pszText = entry->id == ui->state->selected && entry->id == ui->state->default_id ? L"Active / Default"
                       : entry->id == ui->state->selected                                     ? L"Active"
                       : entry->id == ui->state->default_id                                   ? L"Default"
                                                                                              : L"";
        SendMessageW(ui->list, LVM_SETITEMTEXTW, i, (LPARAM) &item);
        if (entry->id == ui->state->selected) {
            active = (int) i;
        }
    }
    ui->editor_row = -1;
    if (!ui->creating && ui->state->count) {
        row = preferred >= 0 && (unsigned) preferred < ui->state->count ? preferred : active >= 0 ? active
                                                                                                  : 0;
        ui->editor_row = row;
        ListView_SetItemState(ui->list, row, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
        ListView_EnsureVisible(ui->list, row, FALSE);
    }
    ui->updating = 0;
    if (!ui->creating) maage_ui_editor_bind(ui);
    InvalidateRect(ui->list, NULL, FALSE);
}
static void maage_ui_client_choices(maage_launcher_win32 *ui, int row) {
    unsigned i;
    wchar_t wide[384];
    char label[384];
    const char *selected = row >= 0 ? ui->state->installations[row].client : "";
    SendMessageW(ui->client_choice, CB_RESETCONTENT, 0, 0);
    SendMessageW(ui->client_choice, CB_ADDSTRING, 0, (LPARAM) L"Client selection required");
    SendMessageW(ui->client_choice, CB_SETCURSEL, 0, 0);
    for (i = 0; i < ui->state->clients->count; ++i) {
        maage_client_build *b = &ui->state->clients->builds[i];
        maage_client_build_label(b, label);
        MultiByteToWideChar(CP_UTF8, 0, label, -1, wide, 384);
        SendMessageW(ui->client_choice, CB_ADDSTRING, 0, (LPARAM) wide);
        if (!strcmp(selected, b->id)) SendMessageW(ui->client_choice, CB_SETCURSEL, i + 1, 0);
    }
}

/* Highlighted row means editing, not activation. Activation and default are
 * separate persisted choices and are never changed just by inspecting a row. */
static void maage_ui_enabled(HWND control, int enabled) {
    if (IsWindowEnabled(control) != !!enabled) EnableWindow(control, enabled);
}

static void maage_ui_editor_status(maage_launcher_win32 *ui) {
    int row = maage_ui_selected(ui);
    int editing = !ui->creating && row >= 0 && (unsigned) row < ui->state->count;
    int busy = !ui->state->catalog_ready || ui->state->client_job != NULL;
    const char *hint = "Select a row on the left to edit its configuration.";
    if (ui->creating) {
        maage_ui_text(ui->editor_heading, "Create installation");
        hint = "Enter a name and optional Client build, then choose Create.";
    } else if (editing) {
        maage_installation *entry = &ui->state->installations[row];
        maage_ui_text(ui->editor_heading, "Edit installation");
        if (entry->id == ui->state->selected && entry->id == ui->state->default_id) hint = "Active for Play and Default. Save changes before switching rows.";
        else if (entry->id == ui->state->selected) hint = "Active for Play. Save changes before switching rows.";
        else if (entry->id == ui->state->default_id) hint = "Default installation. Not active for Play. Save changes before switching rows.";
        else hint = "Selected for editing only. Use Make Active for Play to launch this one.";
    } else maage_ui_text(ui->editor_heading, "Select an installation");
    maage_ui_text(ui->editor_hint, hint);
    maage_ui_text(ui->save_button, ui->creating ? "Create" : "Save Changes");
    maage_ui_enabled(ui->name, (editing || ui->creating) && !busy);
    maage_ui_enabled(ui->client_choice, (editing || ui->creating) && !busy);
    maage_ui_enabled(ui->arguments, (editing || ui->creating) && !busy);
    maage_ui_enabled(ui->new_button, !busy && !ui->creating);
    maage_ui_enabled(ui->save_button, (editing || ui->creating) && !busy);
    maage_ui_enabled(ui->duplicate_button, editing && !busy);
    maage_ui_enabled(ui->remove_button, editing && ui->state->count > 1 && !busy);
    maage_ui_enabled(ui->default_button, editing && ui->state->installations[row].id != ui->state->default_id && !busy);
    maage_ui_enabled(ui->active_button, editing && ui->state->installations[row].id != ui->state->selected && !busy);
    maage_ui_enabled(ui->folder_button, editing);
    maage_ui_enabled(ui->cancel_button, ui->creating);
}

/* A role change must not erase edits currently staged in the detail pane. */
static void maage_ui_installations_roles(maage_launcher_win32 *ui) {
    unsigned i;
    LVITEMW item;
    memset(&item, 0, sizeof(item));
    ui->updating = 1;
    for (i = 0; i < ui->state->count; ++i) {
        maage_installation *entry = &ui->state->installations[i];
        item.iItem = (int) i;
        item.iSubItem = 1;
        item.pszText = entry->id == ui->state->selected && entry->id == ui->state->default_id ? L"Active / Default"
                       : entry->id == ui->state->selected                                     ? L"Active"
                       : entry->id == ui->state->default_id                                   ? L"Default"
                                                                                              : L"";
        SendMessageW(ui->list, LVM_SETITEMTEXTW, i, (LPARAM) &item);
    }
    ui->updating = 0;
    maage_ui_editor_status(ui);
}

static void maage_ui_editor_bind(maage_launcher_win32 *ui) {
    int row = maage_ui_selected(ui);
    if (ui->creating) return;
    if (row >= 0 && (unsigned) row < ui->state->count) {
        maage_ui_text(ui->name, ui->state->installations[row].name);
        maage_ui_text(ui->arguments, ui->state->installations[row].arguments);
    } else {
        maage_ui_text(ui->name, "");
        maage_ui_text(ui->arguments, "");
        row = -1;
    }
    maage_ui_client_choices(ui, row);
    maage_ui_editor_status(ui);
}

/* Win32 can lose the native selection when a ListView is shown after an
 * initially hidden page. Keep the active/edited row explicit and restore the
 * native selection whenever the Installations page becomes visible. */
static void maage_ui_installations_ensure_selection(maage_launcher_win32 *ui) {
    int row = ui->editor_row, native_row;
    unsigned i;
    if (ui->creating || !ui->state->count) return;
    if (row < 0 || (unsigned) row >= ui->state->count) {
        row = 0;
        for (i = 0; i < ui->state->count; ++i)
            if (ui->state->installations[i].id == ui->state->selected) {
                row = (int) i;
                break;
            }
    }
    native_row = ListView_GetNextItem(ui->list, -1, LVNI_SELECTED);
    if (native_row != row) {
        ui->updating = 1;
        ListView_SetItemState(ui->list, -1, 0, LVIS_SELECTED | LVIS_FOCUSED);
        ListView_SetItemState(ui->list, row, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
        ui->updating = 0;
    }
    if (ui->editor_row != row) {
        ui->editor_row = row;
        maage_ui_editor_bind(ui);
    }
    ListView_EnsureVisible(ui->list, row, FALSE);
    InvalidateRect(ui->list, NULL, FALSE);
}

/* Render a persistently selected row with system highlight colors, including
 * when focus moves into the editor. The native ListView still owns hit testing,
 * keyboard navigation, accessibility and high-contrast system colors. */
static LRESULT maage_ui_installations_draw(maage_launcher_win32 *ui, NMLVCUSTOMDRAW *draw) {
    RECT rect, cell;
    LVITEMW item;
    wchar_t text[160];
    HDC dc;
    int row, column, saved, inset;
    if (draw->nmcd.dwDrawStage == CDDS_PREPAINT) return CDRF_NOTIFYITEMDRAW;
    if (draw->nmcd.dwDrawStage != CDDS_ITEMPREPAINT || ui->creating) return CDRF_DODEFAULT;
    row = (int) draw->nmcd.dwItemSpec;
    if (row < 0 || row != ui->editor_row || (unsigned) row >= ui->state->count) return CDRF_DODEFAULT;
    dc = draw->nmcd.hdc;
    saved = SaveDC(dc);
    if (!saved) return CDRF_DODEFAULT;
    rect = draw->nmcd.rc;
    GetClientRect(ui->list, &cell);
    rect.left = cell.left;
    rect.right = cell.right;
    FillRect(dc, &rect, GetSysColorBrush(COLOR_HIGHLIGHT));
    SetTextColor(dc, GetSysColor(COLOR_HIGHLIGHTTEXT));
    SetBkMode(dc, TRANSPARENT);
    SelectObject(dc, ui->native.font);
    inset = maage_launcher_win32_scale(&ui->native, 6);
    for (column = 0; column < 2; ++column) {
        if (!ListView_GetSubItemRect(ui->list, row, column, LVIR_BOUNDS, &cell)) continue;
        if (!column) cell.right = cell.left + ListView_GetColumnWidth(ui->list, 0);
        cell.left += inset;
        cell.right -= inset;
        memset(&item, 0, sizeof(item));
        item.iItem = row;
        item.iSubItem = column;
        item.pszText = text;
        item.cchTextMax = (int) (sizeof(text) / sizeof(text[0]));
        text[0] = 0;
        SendMessageW(ui->list, LVM_GETITEMTEXTW, row, (LPARAM) &item);
        DrawTextW(dc, text, -1, &cell, DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS | DT_NOPREFIX);
    }
    if (GetFocus() == ui->list && !(SendMessageW(ui->list, WM_QUERYUISTATE, 0, 0) & UISF_HIDEFOCUS)) {
        InflateRect(&rect, -inset / 2, -inset / 2);
        DrawFocusRect(dc, &rect);
    }
    RestoreDC(dc, saved);
    return CDRF_SKIPDEFAULT;
}

static void maage_ui_appearance(maage_launcher_win32 *ui) {
    HBRUSH background, sidebar;
    maage_status result = maage_launcher_win32_appearance(&ui->native, ui->state->appearance);
    if (maage_status_failed(result)) {
        maage_log_status(MAAGE_LOG_WARNING, "maage.launcher.ui", result, "Could not update Launcher appearance.");
        return;
    }
    background = CreateSolidBrush(maage_launcher_win32_color(&ui->native, 0));
    sidebar = CreateSolidBrush(maage_launcher_win32_color(&ui->native, 1));
    if (!background || !sidebar) {
        DeleteObject(background);
        DeleteObject(sidebar);
        strcpy(ui->state->error, "Could not allocate Launcher appearance colors.");
        return;
    }
    DeleteObject(ui->background);
    DeleteObject(ui->sidebar);
    ui->background = background;
    ui->sidebar = sidebar;
    ui->displayed_appearance = ui->state->appearance;
    maage_ui_caption(ui);
    ui->backdrop_dirty = 1;
    RedrawWindow(ui->window, NULL, NULL, RDW_INVALIDATE | RDW_ALLCHILDREN | RDW_NOERASE);
    {
        BOOL animation = FALSE;
        ui->animate_splash = SystemParametersInfoW(SPI_GETCLIENTAREAANIMATION, 0, &animation, 0) && animation && !ui->native.high_contrast;
        if (!ui->animate_splash) {
            maage_ui_transition_cancel(ui);
            KillTimer(ui->window, MAAGE_UI_ANIMATION_TIMER);
            memset(ui->home_hover_started, 0, sizeof(ui->home_hover_started));
            ui->splash_logo_size = 144;
        } else if (ui->splash_visible && IsWindowVisible(ui->window)) SetTimer(ui->window, MAAGE_UI_ANIMATION_TIMER, MAAGE_UI_ANIMATION_INTERVAL, NULL);
    }
}
static void maage_ui_copy_code(maage_launcher_win32 *ui) {
    wchar_t code[33];
    HGLOBAL storage;
    void *bytes;
    size_t length;
    if (!*ui->state->accounts.device_code || !MultiByteToWideChar(CP_UTF8, 0, ui->state->accounts.device_code, -1, code, 33)) return;
    length = (wcslen(code) + 1) * sizeof(wchar_t);
    storage = GlobalAlloc(GMEM_MOVEABLE, length);
    if (!storage) return;
    bytes = GlobalLock(storage);
    if (!bytes) {
        GlobalFree(storage);
        return;
    }
    memcpy(bytes, code, length);
    GlobalUnlock(storage);
    if (OpenClipboard(ui->window)) {
        if (EmptyClipboard() && SetClipboardData(CF_UNICODETEXT, storage)) storage = NULL;
        CloseClipboard();
    }
    if (storage) GlobalFree(storage);
}

static void maage_ui_home_refresh(maage_launcher_win32 *ui) {
    maage_launcher_ui *state = ui->state;
    const char *installation = state->catalog_ready ? "Choose Installation" : "Loading installation…";
    const char *version = state->clients_valid ? "Select Client build" : "Client registry unavailable";
    char build_label[384], account_label[256], readiness[512], card_label[512];
    unsigned i;
    int active = 0;
    strcpy(account_label, "No account selected");
    for (i = 0; i < state->count; ++i) {
        maage_installation *entry = &state->installations[i];
        if (entry->id != state->selected) continue;
        installation = entry->name;
        active = 1;
        if (*entry->client && state->clients) {
            int index = maage_clients_find(state->clients, entry->client);
            if (index >= 0) {
                maage_client_build *build = &state->clients->builds[index];
                sprintf(build_label, "%s · %s", build->version, build->build_type);
                version = build_label;
            } else version = "Referenced Client build unavailable";
        }
        break;
    }
    for (i = 0; i < state->accounts.count; ++i) {
        maage_access_profile *profile = &state->accounts.profiles[i];
        if (strcmp(state->account, profile->uuid)) continue;
        sprintf(account_label, "%s%s", profile->name, profile->state == MAAGE_ACCESS_VERIFIED ? " ✓" : "");
        break;
    }
    sprintf(card_label, "Installation: %s", installation);
    maage_ui_text(ui->home_choose[0], card_label);
    sprintf(card_label, "Version: %s", version);
    maage_ui_text(ui->home_choose[1], card_label);
    sprintf(card_label, "Account: %s", account_label);
    maage_ui_text(ui->home_choose[2], card_label);
    maage_ui_enabled(ui->home_choose[0], state->catalog_ready && !state->client_job);
    maage_ui_enabled(ui->home_choose[1], active && state->catalog_ready && state->clients_valid && !state->client_job && !state->launch_pending);
    maage_ui_enabled(ui->home_choose[2], state->accounts.metadata_ready && !state->accounts.busy);
    sprintf(readiness, "%s%s", state->can_play ? "✓ " : "", state->can_play ? "Ready to Play" : state->readiness ? state->readiness : "Initializing…");
    maage_ui_text(ui->home_status, *state->error ? state->error : readiness);
    if (!ui->splash_visible && state->page == MAAGE_PAGE_PLAY) maage_ui_visible(ui->home_status, !maage_ui_home_ready(ui));
}

static void maage_ui_refresh_splash(maage_launcher_win32 *ui) {
    maage_launcher_ui *state = ui->state;
    int preparing, account_step, splash, flags = 0, step, percent = -1;
    char summary[256];
    const char *message = "Preparing the Launcher…", *detail = "Loading installations and saved accounts.";
    const char *phase = "Preparing Minecraft resources";
    wchar_t entry[180], updated[768], wide[100];
    SYSTEMTIME now;
    if (!ui->onboarding_decided && state->catalog_finished && state->accounts.restored) {
        ui->onboarding_decided = 1;
        ui->onboarding_required = state->accounts.count == 0;
    }
    preparing = ui->onboarding_required && state->accounts.allowed;
    account_step = ui->onboarding_required && !state->accounts.allowed;
    if (preparing && state->progress.stage == MAAGE_SETUP_COMPLETE && !state->progress.busy && !state->resources_pending) ui->splash_dismissed = 1;
    splash = !ui->splash_dismissed && (!state->catalog_finished || state->accounts.busy || (ui->onboarding_required && (account_step || state->resources_pending || state->progress.stage != MAAGE_SETUP_COMPLETE || state->progress.busy)));
    if (!splash) ui->splash_dismissed = 1;
    if (splash && account_step) {
        if (state->accounts.busy) {
            message = *state->accounts.device_code ? "Waiting for Microsoft sign-in…" : "Verifying Minecraft Java Edition access…";
            detail = "Your account is checked securely in the background.";
            flags |= MAAGE_SPLASH_CANCEL | MAAGE_SPLASH_PROGRESS;
            if (*state->accounts.device_code && state->accounts.device_deadline > maage_time_monotonic_seconds()) flags |= MAAGE_SPLASH_DEVICE;
        } else {
            message = !state->services_ready ? "Account services unavailable" : maage_status_failed(state->accounts.result) ? "Sign-in was not completed" : "Connect your Minecraft account";
            detail = !state->services_ready ? "Continue to Launcher settings or reopen MaaGE." : "Sign in with Microsoft to verify Minecraft Java Edition access.";
            flags |= MAAGE_SPLASH_SIGN_IN;
        }
    } else if (splash && preparing) {
        message = "Preparing Minecraft resources…";
        detail = "Downloading and checking the files needed to play.";
        switch (state->progress.stage) {
        case MAAGE_SETUP_DOWNLOADING: phase = "Downloading verified assets"; break;
        case MAAGE_SETUP_CLIENT_ARCHIVE: phase = "Preparing client archive"; break;
        case MAAGE_SETUP_RECONSTRUCTING: phase = "Reconstructing resources"; break;
        case MAAGE_SETUP_CLIENT_EXTRACT: phase = "Preparing client resources"; break;
        case MAAGE_SETUP_VERIFYING: phase = "Checking file integrity"; break;
        case MAAGE_SETUP_COMPLETE: phase = "Resources ready"; break;
        case MAAGE_SETUP_FAILED:
        case MAAGE_SETUP_CANCELLED: phase = "Resource preparation needs attention"; break;
        default: break;
        }
        if (!state->services_ready || state->progress.stage == MAAGE_SETUP_FAILED || state->progress.stage == MAAGE_SETUP_CANCELLED) {
            message = "Resource preparation needs attention";
            detail = "Retry preparation or continue to Launcher settings.";
            if (!state->progress.busy && !state->resources_pending) flags |= MAAGE_SPLASH_RETRY;
        }
        if (state->progress.busy || state->resources_pending) flags |= MAAGE_SPLASH_PROGRESS;
        if (state->progress.busy && state->services_ready) detail = phase;
        if (state->progress.busy) {
            maage_setup_progress *p = &state->progress;
            if (p->stage == MAAGE_SETUP_DOWNLOADING && p->total_objects) {
                percent = (int) (100.0 * p->verified_objects / p->total_objects);
                sprintf(summary, "%s · %lu / %lu objects · %.1f MiB downloaded", phase, p->verified_objects, p->total_objects, (p->downloaded_bytes + p->client_downloaded_bytes) / 1048576.0);
            } else if (p->stage == MAAGE_SETUP_CLIENT_ARCHIVE && !p->client_archive_ready) {
                if (p->client_total_bytes > 0 && p->client_downloaded_bytes > 0) percent = (int) (100.0 * p->client_downloaded_bytes / p->client_total_bytes);
                sprintf(summary, "%s · %.1f / %.1f MiB transferred", phase, p->client_downloaded_bytes / 1048576.0, p->client_total_bytes / 1048576.0);
            } else if (p->stage == MAAGE_SETUP_RECONSTRUCTING && p->total_logical_assets) {
                percent = (int) (100.0 * p->reconstructed_files / p->total_logical_assets);
                sprintf(summary, "%s · %lu / %lu files", phase, p->reconstructed_files, p->total_logical_assets);
            } else if (p->stage == MAAGE_SETUP_CLIENT_EXTRACT && p->client_resource_count) {
                unsigned long done = p->total_logical_assets && p->reconstructed_files >= p->total_logical_assets ? p->client_resources_processed : p->client_resources_checked;
                percent = (int) (100.0 * done / p->client_resource_count);
                sprintf(summary, "%s · %lu / %lu files", phase, done, p->client_resource_count);
            } else {
                percent = p->percent;
                sprintf(summary, "%s · %.1f MiB downloaded", phase, (p->downloaded_bytes + p->client_downloaded_bytes) / 1048576.0);
            }
        } else {
            strcpy(summary, phase);
            if (state->progress.stage == MAAGE_SETUP_COMPLETE) percent = 100;
        }
        if (percent > 100) percent = 100;
        maage_ui_text(ui->splash_summary, summary);
        step = percent < 0 ? -1 : percent / 5;
        if (ui->splash_stage != (int) state->progress.stage || ui->splash_step != step) {
            const wchar_t *previous = ui->splash_log;
            unsigned lines = 0;
            const wchar_t *p;
            ui->splash_stage = (int) state->progress.stage;
            ui->splash_step = step;
            for (p = previous; *p; ++p) if (*p == L'\n') ++lines;
            if (lines >= 4) {
                p = wcschr(previous, L'\n');
                if (p) previous = p + 1;
            }
            GetLocalTime(&now);
            MultiByteToWideChar(CP_UTF8, 0, phase, -1, wide, 100);
            if (step < 0) wsprintfW(entry, L"%02u:%02u:%02u  %s", now.wHour, now.wMinute, now.wSecond, wide);
            else wsprintfW(entry, L"%02u:%02u:%02u  %s (%d%%)", now.wHour, now.wMinute, now.wSecond, wide, percent);
            if (wcslen(previous) + wcslen(entry) + 2 < sizeof(updated) / sizeof(updated[0])) {
                wcscpy(updated, previous);
                if (*updated) wcscat(updated, L"\n");
                wcscat(updated, entry);
                wcscpy(ui->splash_log, updated);
            } else wcscpy(ui->splash_log, entry);
            SetWindowTextW(ui->splash_activity, ui->splash_log);
        }
    } else if (splash) {
        flags |= MAAGE_SPLASH_PROGRESS;
        if (state->accounts.busy) {
            message = "Restoring saved accounts…";
            detail = "Checking your existing Minecraft account information.";
        }
    }
    if (splash && (ui->onboarding_required || (state->catalog_finished && !state->services_ready))) flags |= MAAGE_SPLASH_SKIP;
    if (ui->splash_visible && !splash) maage_ui_transition_capture(ui);
    if (ui->splash_percent != percent) {
        double now = maage_time_monotonic_seconds();
        ui->progress_from = maage_ui_progress_value(ui, now);
        if (ui->progress_from < 0 || percent < ui->progress_from) ui->progress_from = percent;
        ui->progress_started = now;
        ui->splash_percent = percent;
        if (splash) {
            SendMessageW(ui->progress, PBM_SETPOS, percent < 0 ? 0 : percent, 0);
            InvalidateRect(ui->progress, NULL, FALSE);
        }
    }
    maage_ui_text(ui->splash_message, message);
    maage_ui_text(ui->splash_detail, detail);
    maage_ui_text(ui->splash_code, state->accounts.device_code);
    maage_ui_enabled(ui->splash_sign_in, state->services_ready && state->accounts.restored);
    maage_ui_enabled(ui->splash_device_sign_in, state->services_ready && state->accounts.restored);
    maage_ui_enabled(ui->splash_retry, state->services_ready);
    if (ui->splash_visible != splash || ui->splash_flags != flags || ui->splash_preparing != preparing) {
        int was_splash = ui->splash_visible;
        ui->splash_visible = splash;
        if (!splash && !ui->transition.dc) KillTimer(ui->window, MAAGE_UI_ANIMATION_TIMER);
        ui->splash_flags = flags;
        ui->splash_preparing = preparing;
        maage_ui_layout(ui);
        if (was_splash && !splash) {
            SetFocus(state->page == MAAGE_PAGE_PLAY ? (state->can_play ? ui->play : ui->home_settings) : ui->navigation[state->page]);
            maage_ui_transition_start(ui);
        }
        else if (splash && (!GetFocus() || !IsWindowVisible(GetFocus()))) {
            if (flags & MAAGE_SPLASH_SIGN_IN && IsWindowEnabled(ui->splash_sign_in)) SetFocus(ui->splash_sign_in);
            else if (flags & MAAGE_SPLASH_CANCEL) SetFocus(ui->splash_cancel);
            else if (flags & MAAGE_SPLASH_SKIP) SetFocus(ui->splash_skip);
        }
    }
}

static void maage_ui_refresh(maage_launcher_win32 *ui) {
    maage_launcher_ui *state = ui->state;
    unsigned i;
    int selected = -1, added = 0;
    if (ui->menu_open) return;
    maage_launcher_ui_refresh(state);
    if (ui->displayed_appearance != state->appearance) {
        maage_ui_appearance(ui);
        SendMessageW(ui->appearance, CB_SETCURSEL, state->appearance, 0);
    }
    maage_ui_enabled(ui->appearance, state->catalog_ready);
    if (ui->revision != state->accounts.revision || SendMessageW(ui->account, CB_GETCOUNT, 0, 0) == 0) {
        wchar_t wide[256];
        char title[256];
        LVITEMW item;
        ui->updating = 1;
        SendMessageW(ui->account, CB_RESETCONTENT, 0, 0);
        ListView_DeleteAllItems(ui->profiles);
        for (i = 0; i < state->accounts.count; ++i) {
            maage_access_profile *p = &state->accounts.profiles[i];
            if (!p->connected) continue;
            wsprintfA(title, "%s - %s", p->name, maage_access_state_name(p->state));
            MultiByteToWideChar(CP_UTF8, 0, title, -1, wide, 256);
            SendMessageW(ui->account, CB_ADDSTRING, 0, (LPARAM) wide);
            SendMessageW(ui->account, CB_SETITEMDATA, added, i);
            if (!strcmp(p->uuid, state->account)) selected = added;
            memset(&item, 0, sizeof(item));
            item.mask = LVIF_TEXT | LVIF_PARAM;
            item.iItem = added;
            item.pszText = wide;
            item.lParam = (LPARAM) i;
            SendMessageW(ui->profiles, LVM_INSERTITEMW, 0, (LPARAM) &item);
            MultiByteToWideChar(CP_UTF8, 0, maage_access_state_name(p->state), -1, wide, 256);
            item.iSubItem = 1;
            item.pszText = wide;
            SendMessageW(ui->profiles, LVM_SETITEMTEXTW, added, (LPARAM) &item);
            if (!strcmp(p->uuid, state->account)) ListView_SetItemState(ui->profiles, added, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
            ++added;
        }
        if (!added) {
            LRESULT row = SendMessageW(ui->account, CB_ADDSTRING, 0, (LPARAM) L"No connected Minecraft account");
            SendMessageW(ui->account, CB_SETITEMDATA, row, (LPARAM) -1);
        }
        {
            LRESULT row = SendMessageW(ui->account, CB_ADDSTRING, 0, (LPARAM) L"Manage Microsoft Accounts...");
            SendMessageW(ui->account, CB_SETITEMDATA, row, (LPARAM) -2);
        }
        SendMessageW(ui->account, CB_SETCURSEL, selected < 0 ? 0 : selected, 0);
        ui->revision = state->accounts.revision;
        ui->updating = 0;
    }
    if (ui->client_revision != state->client_revision) {
        wchar_t wide[384];
        char label[384];
        SendMessageW(ui->clients, CB_RESETCONTENT, 0, 0);
        for (i = 0; i < state->clients->count; ++i) {
            maage_client_build_label(&state->clients->builds[i], label);
            MultiByteToWideChar(CP_UTF8, 0, label, -1, wide, 384);
            SendMessageW(ui->clients, CB_ADDSTRING, 0, (LPARAM) wide);
        }
        SendMessageW(ui->clients, CB_SETCURSEL, 0, 0);
        ui->client_revision = state->client_revision;
        maage_ui_installations_reload(ui, maage_ui_selected(ui));
        if (ui->creating) maage_ui_client_choices(ui, -1);
    }
#if MAAGE_LAUNCHER_DEVELOPMENT
    if (state->client_confirmation) {
        char message[MAAGE_PATH_CAPACITY + 1024];
        wchar_t wide[MAAGE_PATH_CAPACITY + 1024];
        maage_client_build *b = &state->client_candidate.build;
        state->client_confirmation = 0;
        sprintf(message, "Client %s + %s\n%s | %s | %s\n%s\n\nImport copies only the Client executable and its manifest. Only import code you trust. The original is preserved.%s", b->version, b->build, b->build_type, b->platform, b->architecture, state->client_candidate.source, *state->replacing_client ? "\n\nAll installations using the selected Client will switch to this build, then the old managed package will be removed." : "");
        if (MultiByteToWideChar(CP_UTF8, 0, message, -1, wide, MAAGE_PATH_CAPACITY + 1024) && MessageBoxW(ui->window, wide, *state->replacing_client ? L"Replace Client?" : L"Import Local Client?", MB_OKCANCEL | MB_ICONQUESTION) == IDOK) maage_launcher_client_import(state);
        else
            maage_launcher_client_cancel_import(state);
    }
#endif
    maage_ui_text(ui->active_summary, state->active_summary);
    maage_ui_text(ui->client_status, state->client_status);
    maage_ui_text(ui->authentication, state->authentication_status);
    maage_ui_enabled(ui->play, state->can_play);
    maage_ui_home_refresh(ui);
    maage_ui_refresh_splash(ui);
    maage_ui_enabled(ui->account, state->accounts.metadata_ready);
    maage_ui_enabled(ui->verify, !state->accounts.busy && (*state->account || !state->accounts.restored));
    maage_ui_enabled(ui->sign_in, state->accounts.restored && !state->accounts.busy);
    maage_ui_enabled(ui->device_sign_in, state->accounts.restored && !state->accounts.busy);
    maage_ui_enabled(ui->sign_out, state->accounts.restored && !state->accounts.busy && *state->account);
    maage_ui_enabled(ui->remove_account, state->accounts.restored && !state->accounts.busy && *state->account);
    maage_ui_enabled(ui->cancel_account, state->accounts.busy);
    maage_ui_enabled(ui->copy_code, *state->accounts.device_code && state->accounts.device_deadline > maage_time_monotonic_seconds());
    maage_ui_enabled(ui->open_device, IsWindowEnabled(ui->copy_code));
    maage_ui_enabled(ui->repair, !state->progress.busy && !state->client_process && state->client_operation != 4 && !state->launch_pending);
    maage_ui_enabled(ui->verify_resources, IsWindowEnabled(ui->repair));
    maage_ui_enabled(ui->retry_resources, IsWindowEnabled(ui->repair));
    maage_ui_enabled(ui->cancel_resources, state->progress.busy || state->resources_pending);
    maage_ui_text(ui->status, state->readiness);
    maage_ui_text(ui->resource, state->resource_status);
    maage_ui_text(ui->account_status, state->account_status);
    maage_ui_text(ui->error, *state->error ? state->error : state->launch_status);
    if (!ui->splash_visible && state->page != MAAGE_PAGE_PLAY && (state->progress.busy || state->accounts.busy)) {
        int marquee = !state->progress.busy || state->progress.percent < 0;
        if (ui->marquee != marquee) {
            LONG_PTR style = GetWindowLongPtrW(ui->progress, GWL_STYLE);
            ui->marquee = marquee;
            SetWindowLongPtrW(ui->progress, GWL_STYLE, marquee ? style | PBS_MARQUEE : style & ~PBS_MARQUEE);
            SendMessageW(ui->progress, PBM_SETMARQUEE, marquee, 30);
        }
        if (!marquee && ui->progress_value != state->progress.percent) {
            ui->progress_value = state->progress.percent;
            SendMessageW(ui->progress, PBM_SETPOS, state->progress.percent, 0);
        }
        if (!IsWindowVisible(ui->progress)) ShowWindow(ui->progress, SW_SHOW);
    } else {
        if (ui->marquee) SendMessageW(ui->progress, PBM_SETMARQUEE, FALSE, 0);
        ui->marquee = 0;
        if (!ui->splash_visible && IsWindowVisible(ui->progress)) ShowWindow(ui->progress, SW_HIDE);
    }
    maage_ui_editor_status(ui);
}
static void maage_ui_open_installation(maage_launcher_win32 *ui) {
    int row = maage_ui_selected(ui);
    char path[MAAGE_PATH_CAPACITY];
    wchar_t wide[MAAGE_PATH_CAPACITY];
    if (row < 0 || maage_status_failed(maage_launcher_installation_directory(ui->state, (unsigned) row, path))) return;
    if (!MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, path, -1, wide, MAAGE_PATH_CAPACITY) || (INT_PTR) ShellExecuteW(ui->window, L"open", wide, NULL, NULL, SW_SHOWNORMAL) <= 32) strcpy(ui->state->error, "Windows could not open the installation folder in File Explorer.");
}

static void maage_ui_installation_action(maage_launcher_win32 *ui, int id) {
    int row = maage_ui_selected(ui), preferred = row, creating = ui->creating;
    wchar_t wide[128];
    char name[512];
    wchar_t wide_arguments[MAAGE_INSTANCE_ARGUMENTS_CAPACITY];
    char arguments[MAAGE_INSTANCE_ARGUMENTS_CAPACITY], client_id[65];
    const char *client = "";
    LRESULT selected;
    maage_status result;
    if (!ui->state->catalog_ready || ui->state->client_job) return;
    if (id == MAAGE_UI_NEW) {
        ui->previous_row = row;
        ui->creating = 1;
        ui->updating = 1;
        ListView_SetItemState(ui->list, -1, 0, LVIS_SELECTED | LVIS_FOCUSED);
        ui->updating = 0;
        maage_ui_text(ui->name, "New Installation");
        maage_ui_text(ui->arguments, "");
        maage_ui_client_choices(ui, -1);
        maage_ui_editor_status(ui);
        ui->scroll[MAAGE_PAGE_INSTALLATIONS] = 470;
        maage_ui_layout(ui);
        SetFocus(ui->name);
        SendMessageW(ui->name, EM_SETSEL, 0, -1);
        return;
    }
    if (id == MAAGE_UI_CANCEL_NEW) {
        if (!ui->creating) return;
        ui->creating = 0;
        ui->scroll[MAAGE_PAGE_INSTALLATIONS] = 0;
        maage_ui_installations_reload(ui, ui->previous_row);
        maage_ui_layout(ui);
        return;
    }
    if (id == MAAGE_UI_ACTIVE) {
        if (row < 0 || creating) return;
        result = maage_launcher_installation_select(ui->state, (unsigned) row);
        if (!maage_status_failed(result)) maage_ui_installations_roles(ui);
        maage_ui_refresh(ui);
        return;
    }
    if ((!creating && row < 0) || (creating && id != MAAGE_UI_EDIT)) return;
    if (id == MAAGE_UI_REMOVE) {
        if (MessageBoxW(ui->window, L"Remove this MaaGE instance? Its gameplay folder will be archived, preserving worlds and private files. Shared resources and accounts are preserved.", L"Remove Installation", MB_OKCANCEL | MB_ICONQUESTION) != IDOK) return;
        result = maage_launcher_installation_remove(ui->state, (unsigned) row);
        if ((unsigned) preferred >= ui->state->count) preferred = (int) ui->state->count - 1;
    } else if (id == MAAGE_UI_DEFAULT) result = maage_launcher_installation_default(ui->state, (unsigned) row);
    else {
        GetWindowTextW(ui->name, wide, 128);
        if (!WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, wide, -1, name, sizeof(name), NULL, NULL)) {
            strcpy(ui->state->error, "Installation name must contain valid UTF-8.");
            maage_ui_refresh(ui);
            return;
        }
        if (id == MAAGE_UI_DUPLICATE) {
            strcpy(name, ui->state->installations[row].name);
            strcat(name, " Copy");
        }
        selected = SendMessageW(ui->client_choice, CB_GETCURSEL, 0, 0);
        if (selected > 0 && (unsigned) selected <= ui->state->clients->count) client = ui->state->clients->builds[selected - 1].id;
        if (id == MAAGE_UI_DUPLICATE) client = ui->state->installations[row].client;
        GetWindowTextW(ui->arguments, wide_arguments, MAAGE_INSTANCE_ARGUMENTS_CAPACITY);
        if (!WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, wide_arguments, -1, arguments, sizeof(arguments), NULL, NULL)) {
            strcpy(ui->state->error, "Client arguments must be valid UTF-8 within 4095 bytes.");
            maage_ui_refresh(ui);
            return;
        }
        if (id == MAAGE_UI_DUPLICATE) strcpy(arguments, ui->state->installations[row].arguments);
        strcpy(client_id, client);
        result = maage_launcher_installation_write(ui->state, id == MAAGE_UI_EDIT && !creating ? row : -1, name, client_id, arguments);
        if (id == MAAGE_UI_DUPLICATE || creating) preferred = (int) ui->state->count - 1;
    }
    if (!maage_status_failed(result)) {
        ui->creating = 0;
        if (creating) ui->scroll[MAAGE_PAGE_INSTALLATIONS] = 0;
        if (id == MAAGE_UI_DEFAULT) maage_ui_installations_roles(ui);
        else maage_ui_installations_reload(ui, preferred);
        if (creating) maage_ui_layout(ui);
    }
    maage_ui_refresh(ui);
}
#if MAAGE_LAUNCHER_DEVELOPMENT
static void maage_ui_client_import(maage_launcher_win32 *ui, int replacing) {
    OPENFILENAMEW picker;
    wchar_t wide[MAAGE_PATH_CAPACITY];
    char path[MAAGE_PATH_CAPACITY];
    LRESULT row = SendMessageW(ui->clients, CB_GETCURSEL, 0, 0);
    if (!ui->state->catalog_ready || ui->state->client_job || (replacing && row < 0)) return;
    memset(&picker, 0, sizeof(picker));
    wide[0] = 0;
    picker.lStructSize = sizeof(picker);
    picker.hwndOwner = ui->window;
    picker.lpstrTitle = replacing ? L"Replace Client" : L"Import Local Client";
    picker.lpstrFilter = L"MaaGE Client executable\0maage-client.exe\0\0";
    picker.lpstrFile = wide;
    picker.nMaxFile = MAAGE_PATH_CAPACITY;
    picker.Flags = OFN_EXPLORER | OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
    if (GetOpenFileNameW(&picker)) {
        if (WideCharToMultiByte(CP_UTF8, 0, wide, -1, path, sizeof(path), NULL, NULL)) {
            if (replacing) maage_launcher_client_replace(ui->state, (unsigned) row, path);
            else
                maage_launcher_client_probe(ui->state, path);
        }
    } else if (CommDlgExtendedError()) strcpy(ui->state->error, "Windows could not open the Client executable picker.");
}
#endif
static void maage_ui_home_manage(maage_launcher_win32 *ui, unsigned card) {
    if (card == 3) return;
    maage_ui_navigate(ui, card == 2 ? MAAGE_PAGE_ACCOUNTS : MAAGE_PAGE_INSTALLATIONS);
    if (card != 2) maage_ui_installations_ensure_selection(ui);
    SetFocus(ui->navigation[ui->state->page]);
}

static void maage_ui_home_menu(maage_launcher_win32 *ui, unsigned card) {
    maage_launcher_ui *state = ui->state;
    HMENU menu = CreatePopupMenu();
    RECT rect;
    unsigned i, command;
    wchar_t wide[384];
    char label[384];
    int enabled;
    if (!menu) return;
    if (card == 0) {
        for (i = 0; i < state->count; ++i) {
            maage_installation *entry = &state->installations[i];
            if (!MultiByteToWideChar(CP_UTF8, 0, entry->name, -1, wide, 384)) continue;
            AppendMenuW(menu, MF_STRING | (entry->id == state->selected ? MF_CHECKED : 0), i + 2, wide);
        }
    } else if (card == 1 && state->clients) {
        for (i = 0; i < state->clients->count; ++i) {
            unsigned j;
            int checked = 0;
            maage_client_build *build = &state->clients->builds[i];
            maage_client_build_label(build, label);
            if (!MultiByteToWideChar(CP_UTF8, 0, label, -1, wide, 384)) continue;
            for (j = 0; j < state->count; ++j)
                if (state->installations[j].id == state->selected && !strcmp(state->installations[j].client, build->id)) checked = 1;
            AppendMenuW(menu, MF_STRING | (checked ? MF_CHECKED : 0), i + 2, wide);
        }
    } else if (card == 2) {
        for (i = 0; i < state->accounts.count; ++i) {
            maage_access_profile *profile = &state->accounts.profiles[i];
            if (!profile->connected) continue;
            sprintf(label, "%s%s", profile->name, profile->state == MAAGE_ACCESS_VERIFIED ? " ✓" : "");
            if (!MultiByteToWideChar(CP_UTF8, 0, label, -1, wide, 384)) continue;
            AppendMenuW(menu, MF_STRING | (!strcmp(state->account, profile->uuid) ? MF_CHECKED : 0), i + 2, wide);
        }
    } else if (card == 3) AppendMenuW(menu, MF_STRING | MF_GRAYED | MF_CHECKED, 2, L"Default Playspace");
    if (!GetMenuItemCount(menu)) AppendMenuW(menu, MF_STRING | MF_GRAYED, 0, L"No selections available");
    AppendMenuW(menu, MF_SEPARATOR, 0, NULL);
    AppendMenuW(menu, MF_STRING | (card == 3 ? MF_GRAYED : 0), 1, card == 0 ? L"Manage Installations…" : card == 1 ? L"Manage Client Builds…" : card == 2 ? L"Manage Accounts…" : L"Manage Playspaces (Coming Soon)");
    GetWindowRect(ui->home_choose[card], &rect);
    /* Keep the copied UI snapshot stable while the native menu runs its modal loop. */
    ui->menu_open = 1;
    maage_ui_home_hover(ui);
    command = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_NONOTIFY | TPM_LEFTALIGN | TPM_TOPALIGN, rect.left, rect.bottom, 0, ui->window, NULL);
    ui->menu_open = 0;
    DestroyMenu(menu);
    maage_ui_home_hover(ui);
    if (!command) return;
    if (command == 1) {
        maage_ui_home_manage(ui, card);
        return;
    }
    i = command - 2;
    enabled = state->catalog_ready && !state->client_job;
    if (card == 0 && enabled && i < state->count) {
        if (!maage_status_failed(maage_launcher_installation_select(state, i))) {
            ui->creating = 0;
            maage_ui_installations_reload(ui, (int) i);
        }
    } else if (card == 1 && enabled && state->clients_valid && !state->launch_pending && state->clients && i < state->clients->count) {
        unsigned j;
        for (j = 0; j < state->count; ++j) {
            maage_installation entry = state->installations[j];
            if (entry.id != state->selected) continue;
            if (strcmp(entry.client, state->clients->builds[i].id) && !maage_status_failed(maage_launcher_installation_write(state, (int) j, entry.name, state->clients->builds[i].id, entry.arguments))) maage_ui_installations_reload(ui, maage_ui_selected(ui));
            break;
        }
    } else if (card == 2 && state->accounts.metadata_ready && !state->accounts.busy && i < state->accounts.count && state->accounts.profiles[i].connected) {
        if (!maage_status_failed(maage_launcher_account_select(state, state->accounts.profiles[i].uuid))) ui->revision = 0;
    }
}

static LRESULT CALLBACK maage_ui_proc(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
    maage_launcher_win32 *ui = (maage_launcher_win32 *) GetWindowLongPtrW(window, GWLP_USERDATA);
    if (message == WM_NCCREATE) {
        ui = (maage_launcher_win32 *) ((CREATESTRUCTW *) lparam)->lpCreateParams;
        ui->window = window;
        SetWindowLongPtrW(window, GWLP_USERDATA, (LONG_PTR) ui);
    }
    if (!ui) return DefWindowProcW(window, message, wparam, lparam);
    switch (message) {
    case WM_GETMINMAXINFO: {
        MINMAXINFO *info = (MINMAXINFO *) lparam;
        UINT dpi = GetDpiForWindow(window);
        RECT bounds;
        if (!dpi) dpi = ui->native.dpi ? ui->native.dpi : 96;
        SetRect(&bounds, 0, 0, MulDiv(MAAGE_LAUNCHER_WINDOW_MIN_WIDTH, dpi, 96), MulDiv(MAAGE_LAUNCHER_WINDOW_MIN_HEIGHT, dpi, 96));
        if (AdjustWindowRectExForDpi(&bounds, (DWORD) GetWindowLongPtrW(window, GWL_STYLE), FALSE, (DWORD) GetWindowLongPtrW(window, GWL_EXSTYLE), dpi)) {
            info->ptMinTrackSize.x = bounds.right - bounds.left;
            info->ptMinTrackSize.y = bounds.bottom - bounds.top;
        } else {
            info->ptMinTrackSize.x = MulDiv(MAAGE_LAUNCHER_WINDOW_MIN_WIDTH, dpi, 96) + 2 * GetSystemMetricsForDpi(SM_CXSIZEFRAME, dpi) + 2 * GetSystemMetricsForDpi(SM_CXPADDEDBORDER, dpi);
            info->ptMinTrackSize.y = MulDiv(MAAGE_LAUNCHER_WINDOW_MIN_HEIGHT, dpi, 96) + 2 * GetSystemMetricsForDpi(SM_CYSIZEFRAME, dpi) + 2 * GetSystemMetricsForDpi(SM_CXPADDEDBORDER, dpi) + GetSystemMetricsForDpi(SM_CYCAPTION, dpi);
        }
        return 0;
    }
    case WM_DPICHANGED: {
        maage_status result;
        maage_ui_transition_cancel(ui);
        result = maage_launcher_win32_dpi_changed(&ui->native, wparam, lparam);
        if (maage_status_failed(result)) maage_log_status(MAAGE_LOG_WARNING, "maage.launcher.ui", result, "Could not update Launcher DPI.");
        if (ui->controls_ready) maage_ui_splash_fonts(ui);
        maage_ui_layout(ui);
        return 0;
    }
    case WM_SETTINGCHANGE:
    case WM_THEMECHANGED:
        if (ui->native.window) maage_ui_appearance(ui);
        break;
    case WM_ACTIVATE:
        if (LOWORD(wparam) == WA_INACTIVE) maage_ui_transition_cancel(ui);
        maage_ui_home_hover(ui);
        break;
    case WM_MOUSEMOVE:
        maage_ui_home_hover(ui);
        break;
    case WM_MOVE:
        if (!ui->laying_out && ui->transition_active) maage_ui_transition_cancel(ui);
        break;
    case WM_ENTERSIZEMOVE:
        ui->live_resize = 1;
        maage_ui_transition_cancel(ui);
        return 0;
    case WM_EXITSIZEMOVE:
        ui->live_resize = 0;
        return 0;
    case WM_SIZE:
        if (!ui->laying_out && ui->transition_active) maage_ui_transition_cancel(ui);
        if (wparam != SIZE_MINIMIZED) maage_ui_layout(ui);
        return 0;
    case WM_VSCROLL: {
        int page = (int) ui->state->page, position, amount;
        SCROLLINFO info;
        if (page < MAAGE_PAGE_INSTALLATIONS) break;
        memset(&info, 0, sizeof(info));
        info.cbSize = sizeof(info);
        info.fMask = SIF_ALL;
        if (!GetScrollInfo(window, SB_VERT, &info)) break;
        position = ui->scroll[page];
        amount = (int) info.nPage;
        switch (LOWORD(wparam)) {
        case SB_LINEUP:
            position -= 28;
            break;
        case SB_LINEDOWN:
            position += 28;
            break;
        case SB_PAGEUP:
            position -= amount;
            break;
        case SB_PAGEDOWN:
            position += amount;
            break;
        case SB_THUMBPOSITION:
        case SB_THUMBTRACK:
            position = info.nTrackPos;
            break;
        case SB_TOP:
            position = 0;
            break;
        case SB_BOTTOM:
            position = info.nMax;
            break;
        default:
            return 0;
        }
        if (position < 0) position = 0;
        if (position > info.nMax - (int) info.nPage + 1) position = info.nMax - (int) info.nPage + 1;
        if (position != ui->scroll[page]) {
            ui->scroll[page] = position;
            maage_ui_layout(ui);
        }
        return 0;
    }
    case WM_MOUSEWHEEL: {
        POINT point;
        int page = (int) ui->state->page, position, max_scroll;
        SCROLLINFO info;
        if (page < MAAGE_PAGE_INSTALLATIONS) break;
        point.x = GET_X_LPARAM(lparam);
        point.y = GET_Y_LPARAM(lparam);
        ScreenToClient(window, &point);
        if (point.x < maage_launcher_win32_scale(&ui->native, 170)) break;
        memset(&info, 0, sizeof(info));
        info.cbSize = sizeof(info);
        info.fMask = SIF_RANGE | SIF_PAGE;
        if (!GetScrollInfo(window, SB_VERT, &info)) break;
        max_scroll = info.nMax - (int) info.nPage + 1;
        if (max_scroll <= 0) break;
        position = ui->scroll[page] - (int) GET_WHEEL_DELTA_WPARAM(wparam) * 48 / WHEEL_DELTA;
        if (position < 0) position = 0;
        if (position > max_scroll) position = max_scroll;
        if (position != ui->scroll[page]) {
            ui->scroll[page] = position;
            maage_ui_layout(ui);
        }
        return 0;
    }
    case WM_TIMER:
        if (wparam == 1) {
            maage_ui_refresh(ui);
            maage_ui_home_hover(ui);
        }
        if (wparam == MAAGE_UI_ANIMATION_TIMER) {
            double now = maage_time_monotonic_seconds();
            int hover_active = 0, index;
            for (index = 0; index < MAAGE_UI_HOME_BUTTONS; ++index) {
                if (ui->home_hover_started[index]) {
                    HWND button = maage_ui_home_button_at(ui, index);
                    if (button && IsWindowVisible(button)) InvalidateRect(button, NULL, FALSE);
                    if (!ui->animate_splash || now - ui->home_hover_started[index] >= MAAGE_UI_HOME_HOVER_SECONDS) ui->home_hover_started[index] = 0;
                    else hover_active = 1;
                }
            }
            if (ui->transition_active) {
                double t = (now - ui->transition_started) / MAAGE_UI_PAGE_FADE_SECONDS;
                if (t >= 1.0 || !ui->animate_splash || IsIconic(window)) maage_ui_transition_cancel(ui);
                else {
                    double remaining = (1.0 - t) * (1.0 - t);
                    if (!SetLayeredWindowAttributes(ui->transition_window, 0, (BYTE) (remaining * 255 + .5), LWA_ALPHA)) maage_ui_transition_cancel(ui);
                }
            }
            if (ui->splash_visible && ui->animate_splash && !IsIconic(window)) {
                double elapsed = now - ui->splash_started;
                int size = (int) (144 + 3 * sin(elapsed * 6.2831853 / 12.0) + .5);
                if (size != ui->splash_logo_size) {
                    RECT rect = ui->splash_logo;
                    ui->splash_logo_size = size;
                    ui->backdrop_dirty = 1;
                    InflateRect(&rect, maage_ui_scale(ui, 5), maage_ui_scale(ui, 5));
                    InvalidateRect(window, &rect, FALSE);
                }
                if ((ui->splash_flags & MAAGE_SPLASH_PROGRESS) && (ui->splash_percent < 0 || ui->progress_from != ui->splash_percent)) {
                    InvalidateRect(ui->progress, NULL, FALSE);
                    if (ui->splash_percent >= 0 && now - ui->progress_started >= MAAGE_UI_PROGRESS_SECONDS) ui->progress_from = ui->splash_percent;
                }
            }
            if (!hover_active && !ui->transition_active && (!ui->splash_visible || !ui->animate_splash)) KillTimer(window, MAAGE_UI_ANIMATION_TIMER);
        }
        return 0;
    case WM_COMMAND: {
        int id = LOWORD(wparam), event = HIWORD(wparam);
        if (id == MAAGE_UI_SPLASH_SIGN_IN) id = MAAGE_UI_SIGN_IN;
        if (id == MAAGE_UI_SPLASH_DEVICE_SIGN_IN) id = MAAGE_UI_DEVICE_SIGN_IN;
        if (id == MAAGE_UI_SPLASH_CANCEL_ACCOUNT) id = MAAGE_UI_CANCEL_ACCOUNT;
        if (id == MAAGE_UI_SPLASH_COPY) id = MAAGE_UI_COPY_CODE;
        if (id == MAAGE_UI_SPLASH_OPEN) id = MAAGE_UI_OPEN_DEVICE;
        if (id == MAAGE_UI_HOME_STATUS) id = MAAGE_UI_STATUS;
#if MAAGE_LAUNCHER_DEVELOPMENT
        if (id == MAAGE_UI_CLIENT_IMPORT) maage_ui_client_import(ui, 0);
        if (id == MAAGE_UI_CLIENT_REPLACE) maage_ui_client_import(ui, 1);
#endif
        if (id == MAAGE_UI_CLIENT_REMOVE && !ui->state->client_job) {
            LRESULT selected = SendMessageW(ui->clients, CB_GETCURSEL, 0, 0);
            if (selected >= 0 && MessageBoxW(window, L"Remove unused managed Client files? Shared Minecraft resources and developer originals are preserved.", L"Remove Client", MB_OKCANCEL | MB_ICONQUESTION) == IDOK) maage_launcher_client_remove(ui->state, (unsigned) selected);
        }
        if (id == 150) maage_launcher_client_play(ui->state);
        if (id >= MAAGE_UI_HOME_CHOOSE && id < MAAGE_UI_HOME_CHOOSE + 4) maage_ui_home_menu(ui, (unsigned) (id - MAAGE_UI_HOME_CHOOSE));
        if (id >= MAAGE_UI_HOME_MANAGE && id < MAAGE_UI_HOME_MANAGE + 4) {
            unsigned card = (unsigned) (id - MAAGE_UI_HOME_MANAGE);
            if (card == 3) maage_ui_home_menu(ui, card);
            else maage_ui_home_manage(ui, card);
        }
        if (id == MAAGE_UI_HOME_SETTINGS) {
            maage_ui_navigate(ui, MAAGE_PAGE_SETTINGS);
            SetFocus(ui->navigation[MAAGE_PAGE_SETTINGS]);
        }
        if (id == MAAGE_UI_SPLASH_SKIP) ui->splash_dismissed = 1;
        if (id == MAAGE_UI_SPLASH_RETRY) maage_launcher_resources(ui->state, MAAGE_ASSETS_PREPARE);
        if (id >= 100 && id < 105) {
            maage_ui_navigate(ui, (maage_launcher_page) (id - 100));
            if (ui->state->page == MAAGE_PAGE_INSTALLATIONS) maage_ui_installations_ensure_selection(ui);
            if (ui->state->page == MAAGE_PAGE_PLAY) SetFocus(ui->state->can_play ? ui->play : ui->home_settings);
            return 0;
        }
        if (id == MAAGE_UI_ABOUT || id == MAAGE_UI_BACK_SETTINGS) {
            maage_ui_navigate(ui, id == MAAGE_UI_ABOUT ? MAAGE_PAGE_ABOUT : MAAGE_PAGE_SETTINGS);
            return 0;
        }
        if (id == MAAGE_UI_STATUS) {
            maage_ui_navigate(ui, maage_launcher_attention_page(ui->state));
            if (ui->state->page != MAAGE_PAGE_PLAY) SetFocus(ui->navigation[ui->state->page]);
            return 0;
        }
        if (id == MAAGE_UI_ACCOUNT && event == CBN_SELCHANGE && !ui->updating) {
            LRESULT row = SendMessageW(ui->account, CB_GETCURSEL, 0, 0), index;
            index = SendMessageW(ui->account, CB_GETITEMDATA, row, 0);
            if (index >= 0 && (unsigned) index < ui->state->accounts.count) maage_launcher_account_select(ui->state, ui->state->accounts.profiles[index].uuid);
            else if (index == -2) {
                maage_ui_navigate(ui, MAAGE_PAGE_ACCOUNTS);
            }
            ui->revision = 0;
        }
        if ((id >= MAAGE_UI_NEW && id <= MAAGE_UI_DEFAULT) || id == MAAGE_UI_CANCEL_NEW || id == MAAGE_UI_ACTIVE) maage_ui_installation_action(ui, id);
        if (id == MAAGE_UI_OPEN_FOLDER) maage_ui_open_installation(ui);
        if (id == MAAGE_UI_VERIFY) {
            if (!ui->state->accounts.restored) maage_access_restore(ui->state->access);
            else if (*ui->state->account) maage_access_verify_local(ui->state->access, ui->state->account);
        }
        if (id == MAAGE_UI_SIGN_IN || id == MAAGE_UI_DEVICE_SIGN_IN) maage_access_sign_in(ui->state->access, id == MAAGE_UI_DEVICE_SIGN_IN);
        if (id == MAAGE_UI_SIGN_OUT) maage_access_sign_out(ui->state->access, ui->state->account);
        if (id == MAAGE_UI_REMOVE_ACCOUNT) maage_access_disconnect(ui->state->access, ui->state->account);
        if (id == MAAGE_UI_CANCEL_ACCOUNT) maage_access_cancel_operation(ui->state->access);
        if (id == MAAGE_UI_COPY_CODE) maage_ui_copy_code(ui);
        if (id == MAAGE_UI_OPEN_DEVICE && *ui->state->accounts.device_url) {
            wchar_t url[256];
            if (MultiByteToWideChar(CP_UTF8, 0, ui->state->accounts.device_url, -1, url, 256)) ShellExecuteW(window, L"open", url, NULL, NULL, SW_SHOWNORMAL);
        }
        if (id == MAAGE_UI_APPEARANCE && event == CBN_SELCHANGE) {
            int previous = ui->state->appearance;
            ui->state->appearance = (int) SendMessageW(ui->appearance, CB_GETCURSEL, 0, 0);
            if (maage_status_failed(maage_launcher_ui_save(ui->state))) {
                ui->state->appearance = previous;
                SendMessageW(ui->appearance, CB_SETCURSEL, previous, 0);
            }
            maage_ui_appearance(ui);
        }
        if (id == MAAGE_UI_REPAIR) maage_launcher_resources(ui->state, MAAGE_ASSETS_REPAIR);
        if (id == MAAGE_UI_VERIFY_RESOURCES) maage_launcher_resources(ui->state, MAAGE_ASSETS_VERIFY);
        if (id == MAAGE_UI_RETRY_RESOURCES) maage_launcher_resources(ui->state, ui->state->resource_mode);
        if (id == MAAGE_UI_CANCEL_RESOURCES) maage_launcher_resources_cancel(ui->state);
        if (id == MAAGE_UI_PROJECT && (INT_PTR) ShellExecuteW(window, L"open", L"https://github.com/FireController1847/MaaGE", NULL, NULL, SW_SHOWNORMAL) <= 32) strcpy(ui->state->error, "Windows could not open the project link in your browser.");
        maage_ui_refresh(ui);
        return 0;
    }
    case WM_NOTIFY:
        if (((NMHDR *) lparam)->hwndFrom == ui->home_tooltip && ((NMHDR *) lparam)->code == TTN_GETDISPINFOW) {
            NMTTDISPINFOW *info = (NMTTDISPINFOW *) lparam;
            HWND button = (HWND) info->hdr.idFrom;
            unsigned i;
            GetWindowTextW(button, ui->home_tooltip_text, 384);
            if (button == ui->home_choose[1] && ui->state->clients) {
                for (i = 0; i < ui->state->count; ++i) {
                    maage_installation *entry = &ui->state->installations[i];
                    int index;
                    char label[384];
                    if (entry->id != ui->state->selected) continue;
                    index = maage_clients_find(ui->state->clients, entry->client);
                    if (index >= 0) {
                        maage_client_build_label(&ui->state->clients->builds[index], label);
                        MultiByteToWideChar(CP_UTF8, 0, label, -1, ui->home_tooltip_text, 384);
                    }
                    break;
                }
            }
            info->lpszText = ui->home_tooltip_text;
            return 0;
        }
        if (((NMHDR *) lparam)->hwndFrom == ui->profiles && ((NMHDR *) lparam)->code == LVN_ITEMCHANGED && !ui->updating) {
            NMLISTVIEW *change = (NMLISTVIEW *) lparam;
            LVITEMW item;
            if ((change->uChanged & LVIF_STATE) && (change->uNewState & LVIS_SELECTED) && change->iItem >= 0) {
                memset(&item, 0, sizeof(item));
                item.mask = LVIF_PARAM;
                item.iItem = change->iItem;
                if (SendMessageW(ui->profiles, LVM_GETITEMW, 0, (LPARAM) &item) && (unsigned) item.lParam < ui->state->accounts.count) maage_launcher_account_select(ui->state, ui->state->accounts.profiles[item.lParam].uuid);
                maage_ui_refresh(ui);
            }
            return 0;
        }
        if (((NMHDR *) lparam)->code == NM_CUSTOMDRAW) {
            if (((NMHDR *) lparam)->hwndFrom == ui->list) return maage_ui_installations_draw(ui, (NMLVCUSTOMDRAW *) lparam);
            return maage_launcher_win32_button(&ui->native, lparam);
        }
        if (((NMHDR *) lparam)->hwndFrom == ui->list && !ui->updating) {
            if (((NMHDR *) lparam)->code == NM_DBLCLK && ((NMITEMACTIVATE *) lparam)->iItem >= 0 && !ui->creating) {
                ui->editor_row = ((NMITEMACTIVATE *) lparam)->iItem;
                maage_ui_installation_action(ui, MAAGE_UI_ACTIVE);
                return 0;
            }
            if (((NMHDR *) lparam)->code == LVN_ITEMCHANGED) {
                NMLISTVIEW *change = (NMLISTVIEW *) lparam;
                if (!(change->uChanged & LVIF_STATE) || !(change->uNewState & LVIS_SELECTED) || change->iItem < 0) return 0;
                ui->editor_row = change->iItem;
                if (ui->creating) {
                    ui->creating = 0;
                    maage_ui_layout(ui);
                }
                maage_ui_editor_bind(ui);
                InvalidateRect(ui->list, NULL, FALSE);
                maage_ui_refresh(ui);
            }
        }
        return 0;
    case WM_MEASUREITEM:
        return maage_launcher_win32_combo_measure(&ui->native, lparam);
    case WM_DRAWITEM:
        if (maage_ui_draw_home_button(ui, (const DRAWITEMSTRUCT *) lparam)) return TRUE;
        return maage_launcher_win32_combo_draw(&ui->native, lparam);
    case WM_CTLCOLORSTATIC:
    case WM_CTLCOLOREDIT:
    case WM_CTLCOLORLISTBOX:
        SetTextColor((HDC) wparam, maage_launcher_win32_text_color(&ui->native));
        SetBkColor((HDC) wparam, maage_launcher_win32_color(&ui->native, 0));
        SetBkMode((HDC) wparam, TRANSPARENT);
        return (LRESULT) ui->background;
    case WM_SETCURSOR:
        if (!ui->splash_visible && ui->state->page == MAAGE_PAGE_PLAY && (HWND) wparam != window && IsWindowEnabled((HWND) wparam)) {
            SetCursor(LoadCursorW(NULL, IDC_HAND));
            return TRUE;
        }
        break;
    case WM_ERASEBKGND: {
        RECT rect;
        if (ui->controls_ready) return 1;
        GetClientRect(window, &rect);
        FillRect((HDC) wparam, &rect, ui->background);
        rect.right = maage_launcher_win32_scale(&ui->native, 170);
        FillRect((HDC) wparam, &rect, ui->sidebar);
        return 1;
    }
    case WM_PAINT: {
        PAINTSTRUCT paint;
        HDC dc = BeginPaint(window, &paint);
        maage_ui_paint_client(ui, dc, paint.rcPaint);
        EndPaint(window, &paint);
        return 0;
    }
    case WM_PRINTCLIENT: {
        RECT rect;
        GetClientRect(window, &rect);
        maage_ui_paint_client(ui, (HDC) wparam, rect);
        return 0;
    }
    case WM_CLOSE:
        DestroyWindow(window);
        return 0;
    case WM_DESTROY:
        KillTimer(window, 1);
        KillTimer(window, MAAGE_UI_ANIMATION_TIMER);
        maage_ui_transition_cancel(ui);
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(window, message, wparam, lparam);
}
maage_status maage_launcher_native_run(maage_launcher_ui *state) {
    maage_launcher_win32 ui;
    WNDCLASSW klass;
    MSG message;
    int received, i;
    double started = maage_time_monotonic_seconds();
    HWND control;
    LVCOLUMNW column;
    const wchar_t *names[] = {L"Play", L"Installations", L"Accounts", L"Settings", L"About"};
    maage_status cleanup, outcome = maage_status_make(MAAGE_STATUS_SUCCESS, MAAGE_CONTEXT_LAUNCHER, MAAGE_OPERATION_OPEN, 0);
    if (!state) return maage_status_make(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_CONTEXT_LAUNCHER, MAAGE_OPERATION_OPEN, 0);
    memset(&ui, 0, sizeof(ui));
    ui.state = state;
    ui.editor_row = -1;
    ui.progress_value = -1;
    ui.splash_visible = 1;
    ui.splash_flags = MAAGE_SPLASH_PROGRESS;
    ui.splash_stage = ui.splash_step = -1;
    ui.splash_logo_size = 144;
    ui.splash_percent = -1;
    ui.backdrop_dirty = 1;
    ui.caption_home = -1;
    ui.splash_started = maage_time_monotonic_seconds();
    ui.background = CreateSolidBrush(GetSysColor(COLOR_WINDOW));
    ui.sidebar = CreateSolidBrush(GetSysColor(COLOR_BTNFACE));
    memset(&klass, 0, sizeof(klass));
    klass.lpfnWndProc = maage_ui_proc;
    klass.hInstance = GetModuleHandleW(NULL);
    klass.hCursor = LoadCursor(NULL, IDC_ARROW);
    klass.lpszClassName = L"MaaGELauncher";
    RegisterClassW(&klass);
    ui.window = CreateWindowExW(WS_EX_CONTROLPARENT, klass.lpszClassName, L"MaaGE Launcher", WS_CLIPCHILDREN | WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, MAAGE_LAUNCHER_WINDOW_WIDTH, MAAGE_LAUNCHER_WINDOW_HEIGHT, NULL, NULL, klass.hInstance, &ui);
    if (!ui.window) {
        outcome = maage_status_from_win32((unsigned long) GetLastError(), MAAGE_CONTEXT_LAUNCHER, MAAGE_OPERATION_OPEN);
        goto cleanup;
    }
    outcome = maage_launcher_win32_style_init(&ui.native, ui.window, MAAGE_LAUNCHER_WINDOW_WIDTH, MAAGE_LAUNCHER_WINDOW_HEIGHT, 0);
    if (maage_status_failed(outcome)) {
        DestroyWindow(ui.window);
        goto cleanup;
    }
    if (!ui.background || !ui.sidebar) {
        outcome = maage_status_make(MAAGE_STATUS_ALLOCATION_FAILED, MAAGE_CONTEXT_LAUNCHER, MAAGE_OPERATION_CREATE, 0);
        DestroyWindow(ui.window);
        goto cleanup;
    }
    /* Prefer the prototype's installed font without adding a packaged dependency. */
    {
        HDC dc = GetDC(ui.window);
        HFONT font = CreateFontW(-16, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, ANTIALIASED_QUALITY, DEFAULT_PITCH, L"Fira Sans");
        if (dc && font) {
            HFONT previous = (HFONT) SelectObject(dc, font);
            wchar_t face[LF_FACESIZE];
            if (previous && previous != (HFONT) HGDI_ERROR) {
                ui.home_fira = GetTextFaceW(dc, LF_FACESIZE, face) > 0 && !lstrcmpiW(face, L"Fira Sans");
                SelectObject(dc, previous);
            }
        }
        if (font) DeleteObject(font);
        if (dc) ReleaseDC(ui.window, dc);
    }
    if (!maage_win32_surface_resource(&ui.logo, klass.hInstance, MAAGE_LAUNCHER_LOGO_RESOURCE)) {
        ui.artwork = (HICON) LoadImageW(klass.hInstance, MAKEINTRESOURCEW(MAAGE_LAUNCHER_ICON_RESOURCE), IMAGE_ICON, 256, 256, 0);
        maage_log_status(MAAGE_LOG_WARNING, "maage.launcher.ui", maage_status_make(MAAGE_STATUS_PARTIAL, MAAGE_CONTEXT_LAUNCHER, MAAGE_OPERATION_LOAD, 0), "Launcher PNG artwork is unavailable; using the native icon.");
    }
    if (!maage_win32_surface_resource(&ui.home_background, klass.hInstance, MAAGE_LAUNCHER_HOME_BACKGROUND))
        maage_log_status(MAAGE_LOG_WARNING, "maage.launcher.ui", maage_status_make(MAAGE_STATUS_PARTIAL, MAAGE_CONTEXT_LAUNCHER, MAAGE_OPERATION_LOAD, 0), "Launcher home background is unavailable; using the gradient.");
    {
        int missing = 0;
        for (i = 0; i < MAAGE_LAUNCHER_HOME_ICON_COUNT; ++i)
            if (!maage_win32_surface_resource(&ui.home_icons[i], klass.hInstance, MAAGE_LAUNCHER_HOME_ICONS + i)) missing = 1;
        if (missing) maage_log_status(MAAGE_LOG_WARNING, "maage.launcher.ui", maage_status_make(MAAGE_STATUS_PARTIAL, MAAGE_CONTEXT_LAUNCHER, MAAGE_OPERATION_LOAD, 0), "Launcher home icon artwork is incomplete; using native strokes.");
    }
    if (ui.logo.dc && !maage_win32_surface_shadow(&ui.logo_shadow, &ui.logo)) maage_log_status(MAAGE_LOG_WARNING, "maage.launcher.ui", maage_status_make(MAAGE_STATUS_PARTIAL, MAAGE_CONTEXT_LAUNCHER, MAAGE_OPERATION_CREATE, 0), "Launcher logo shadow is unavailable.");
    if (!maage_win32_surface_make_control_shadow(&ui.play_shadow, 1) || !maage_win32_surface_make_control_shadow(&ui.card_shadow, 0)) maage_log_status(MAAGE_LOG_WARNING, "maage.launcher.ui", maage_status_make(MAAGE_STATUS_PARTIAL, MAAGE_CONTEXT_LAUNCHER, MAAGE_OPERATION_CREATE, 0), "Launcher control shadows are unavailable.");
    maage_ui_appearance(&ui);
    maage_ui_label(&ui, -1, L"MaaGE", 20, 24, 130, 24);
    for (i = 0; i < 5; ++i)
        ui.navigation[i] = maage_ui_control(&ui, -1, L"BUTTON", names[i], WS_TABSTOP | BS_AUTORADIOBUTTON | BS_PUSHLIKE, 14, i >= 3 ? -104 + (i - 3) * 46 : 100 + i * 46, 142, 34, 100 + i);
    {
        const wchar_t *headings[] = {L"Play", L"Installations", L"Accounts", L"Settings", L"About MaaGE"};
        for (i = 1; i < 5; ++i) {
            control = maage_ui_label(&ui, i, headings[i], 198, 80, 330, 38);
            SendMessageW(control, WM_SETFONT, (WPARAM) ui.native.heading, TRUE);
        }
    }
    ui.active_summary = maage_ui_label(&ui, -1, L"Starting Launcher…", 198, 24, -540, 28);
    SetWindowLongPtrW(ui.active_summary, GWL_STYLE, GetWindowLongPtrW(ui.active_summary, GWL_STYLE) | SS_ENDELLIPSIS);
    ui.account = maage_ui_control(&ui, -1, L"COMBOBOX", L"Active Minecraft account", WS_TABSTOP | CBS_DROPDOWNLIST | WS_VSCROLL, -310, 18, 280, 260, MAAGE_UI_ACCOUNT);
    ui.play = maage_ui_button(&ui, MAAGE_UI_HOME, L"Play MaaGE", 0, 0, 280, 150);
    maage_ui_enabled(ui.play, FALSE);
    ui.home_status = maage_ui_button(&ui, MAAGE_UI_HOME, L"Initializing…", 0, 0, 260, MAAGE_UI_HOME_STATUS);
    {
        const wchar_t *choices[] = {L"Loading installation…", L"No Client build", L"No account selected", L"Playspace: Default Playspace"};
        const wchar_t *manage[] = {L"Manage Installations", L"Manage Client Builds", L"Manage Accounts", L"Manage Playspaces (Coming Soon)"};
        for (i = 0; i < 4; ++i) {
            ui.home_choose[i] = maage_ui_button(&ui, MAAGE_UI_HOME, choices[i], 0, 0, 200, MAAGE_UI_HOME_CHOOSE + i);
            ui.home_manage[i] = maage_ui_button(&ui, MAAGE_UI_HOME, manage[i], 0, 0, 40, MAAGE_UI_HOME_MANAGE + i);
        }
    }
    ui.home_settings = maage_ui_button(&ui, MAAGE_UI_HOME, L"Launcher Configuration", 0, 0, 260, MAAGE_UI_HOME_SETTINGS);
    ui.splash_message = maage_ui_control(&ui, MAAGE_UI_SPLASH, L"STATIC", L"Preparing the Launcher…", SS_CENTER | SS_CENTERIMAGE, 0, 0, 600, 30, 0);
    SendMessageW(ui.splash_message, WM_SETFONT, (WPARAM) ui.native.heading, TRUE);
    ui.splash_detail = maage_ui_control(&ui, MAAGE_UI_SPLASH, L"STATIC", L"Loading installations and saved accounts.", SS_CENTER, 0, 0, 600, 42, 0);
    ui.splash_sign_in = maage_ui_button(&ui, MAAGE_UI_SPLASH, L"Sign In with Microsoft", 0, 0, 220, MAAGE_UI_SPLASH_SIGN_IN);
    ui.splash_device_sign_in = maage_ui_button(&ui, MAAGE_UI_SPLASH, L"Use Device Code", 0, 0, 166, MAAGE_UI_SPLASH_DEVICE_SIGN_IN);
    ui.splash_cancel = maage_ui_button(&ui, MAAGE_UI_SPLASH, L"Cancel Sign In", 0, 0, 170, MAAGE_UI_SPLASH_CANCEL_ACCOUNT);
    ui.splash_code = maage_ui_control(&ui, MAAGE_UI_SPLASH, L"STATIC", L"", SS_CENTER | SS_CENTERIMAGE, 0, 0, 460, 30, 0);
    ui.splash_copy = maage_ui_button(&ui, MAAGE_UI_SPLASH, L"Copy Code", 0, 0, 155, MAAGE_UI_SPLASH_COPY);
    ui.splash_open = maage_ui_button(&ui, MAAGE_UI_SPLASH, L"Open Microsoft Sign In", 0, 0, 215, MAAGE_UI_SPLASH_OPEN);
    ui.splash_retry = maage_ui_button(&ui, MAAGE_UI_SPLASH, L"Retry Resource Preparation", 0, 0, 290, MAAGE_UI_SPLASH_RETRY);
    ui.splash_skip = maage_ui_button(&ui, MAAGE_UI_SPLASH, L"Continue to Launcher", 0, 0, 250, MAAGE_UI_SPLASH_SKIP);
    ui.splash_activity = maage_ui_label(&ui, MAAGE_UI_SPLASH, L"", 0, 0, 600, 80);
    ui.splash_summary = maage_ui_label(&ui, MAAGE_UI_SPLASH, L"", 0, 0, 600, 18);
    ui.clients_label = maage_ui_label(&ui, MAAGE_PAGE_INSTALLATIONS, L"Installed Clients", 198, 78, -230, 26);
    ui.clients = maage_ui_control(&ui, MAAGE_PAGE_INSTALLATIONS, L"COMBOBOX", L"Installed Client builds", WS_TABSTOP | CBS_DROPDOWNLIST | WS_VSCROLL, 198, 110, -230, 240, MAAGE_UI_CLIENTS);
#if MAAGE_LAUNCHER_DEVELOPMENT
    ui.clients_import = maage_ui_button(&ui, MAAGE_PAGE_INSTALLATIONS, L"Import Local Client...", 198, 150, 200, MAAGE_UI_CLIENT_IMPORT);
    ui.clients_replace = maage_ui_button(&ui, MAAGE_PAGE_INSTALLATIONS, L"Replace Client...", 410, 150, 170, MAAGE_UI_CLIENT_REPLACE);
    ui.clients_remove = maage_ui_button(&ui, MAAGE_PAGE_INSTALLATIONS, L"Remove Client...", 592, 150, 170, MAAGE_UI_CLIENT_REMOVE);
#else
    ui.clients_remove = maage_ui_button(&ui, MAAGE_PAGE_INSTALLATIONS, L"Remove Client...", 198, 150, 170, MAAGE_UI_CLIENT_REMOVE);
#endif
    ui.client_status = maage_ui_label(&ui, MAAGE_PAGE_INSTALLATIONS, L"Loading Client builds…", 198, 188, -230, 30);
    ui.installations_label = maage_ui_label(&ui, MAAGE_PAGE_INSTALLATIONS, L"Installations", 198, 205, 250, 32);
    SendMessageW(ui.installations_label, WM_SETFONT, (WPARAM) ui.native.heading, TRUE);
    ui.new_button = maage_ui_button(&ui, MAAGE_PAGE_INSTALLATIONS, L"+ New", 198, 205, 116, MAAGE_UI_NEW);
    ui.list_hint = maage_ui_label(&ui, MAAGE_PAGE_INSTALLATIONS, L"Select to edit; double-click to activate.", 198, 244, 320, 28);
    ui.list = maage_ui_control(&ui, MAAGE_PAGE_INSTALLATIONS, WC_LISTVIEWW, L"MaaGE installations", WS_TABSTOP | WS_BORDER | LVS_REPORT | LVS_SINGLESEL | LVS_SHOWSELALWAYS, 198, 280, 350, 330, MAAGE_UI_LIST);
    SetWindowLongPtrW(ui.list, GWL_EXSTYLE, GetWindowLongPtrW(ui.list, GWL_EXSTYLE) | WS_EX_CLIENTEDGE);
    SetWindowPos(ui.list, NULL, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
    ListView_SetBkColor(ui.list, GetSysColor(COLOR_WINDOW));
    ListView_SetTextBkColor(ui.list, GetSysColor(COLOR_WINDOW));
    ListView_SetExtendedListViewStyle(ui.list, LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER | LVS_EX_LABELTIP);
    memset(&column, 0, sizeof(column));
    column.mask = LVCF_TEXT | LVCF_WIDTH;
    column.pszText = L"Installation";
    column.cx = maage_launcher_win32_scale(&ui.native, 220);
    SendMessageW(ui.list, LVM_INSERTCOLUMNW, 0, (LPARAM) &column);
    column.pszText = L"Role";
    column.cx = maage_launcher_win32_scale(&ui.native, 120);
    SendMessageW(ui.list, LVM_INSERTCOLUMNW, 1, (LPARAM) &column);
    ui.editor_heading = maage_ui_label(&ui, MAAGE_PAGE_INSTALLATIONS, L"Edit installation", 540, 206, 470, 32);
    SendMessageW(ui.editor_heading, WM_SETFONT, (WPARAM) ui.native.heading, TRUE);
    ui.editor_hint = maage_ui_label(&ui, MAAGE_PAGE_INSTALLATIONS, L"", 540, 244, 470, 42);
    ui.name_label = maage_ui_label(&ui, MAAGE_PAGE_INSTALLATIONS, L"Installation name", 540, 294, 400, 24);
    ui.name = maage_ui_control(&ui, MAAGE_PAGE_INSTALLATIONS, L"EDIT", L"", WS_TABSTOP | ES_AUTOHSCROLL, 540, 318, 470, 32, MAAGE_UI_NAME);
    SendMessageW(ui.name, EM_SETLIMITTEXT, 95, 0);
    ui.client_label = maage_ui_label(&ui, MAAGE_PAGE_INSTALLATIONS, L"Client build", 540, 364, 400, 24);
    ui.client_choice = maage_ui_control(&ui, MAAGE_PAGE_INSTALLATIONS, L"COMBOBOX", L"Client build", WS_TABSTOP | CBS_DROPDOWNLIST | WS_VSCROLL, 540, 390, 470, 220, MAAGE_UI_CLIENT_CHOICE);
    ui.arguments_label = maage_ui_label(&ui, MAAGE_PAGE_INSTALLATIONS, L"Client CLI arguments (quote values containing spaces)", 540, 438, 470, 24);
    ui.arguments = maage_ui_control(&ui, MAAGE_PAGE_INSTALLATIONS, L"EDIT", L"", WS_TABSTOP | ES_AUTOHSCROLL, 540, 464, 470, 32, MAAGE_UI_ARGUMENTS);
    SendMessageW(ui.arguments, EM_SETLIMITTEXT, MAAGE_INSTANCE_ARGUMENTS_CAPACITY - 1, 0);
    ui.save_button = maage_ui_button(&ui, MAAGE_PAGE_INSTALLATIONS, L"Save Changes", 540, 517, 140, MAAGE_UI_EDIT);
    ui.duplicate_button = maage_ui_button(&ui, MAAGE_PAGE_INSTALLATIONS, L"Duplicate", 690, 517, 140, MAAGE_UI_DUPLICATE);
    ui.remove_button = maage_ui_button(&ui, MAAGE_PAGE_INSTALLATIONS, L"Remove", 840, 517, 140, MAAGE_UI_REMOVE);
    ui.cancel_button = maage_ui_button(&ui, MAAGE_PAGE_INSTALLATIONS, L"Cancel", 840, 517, 140, MAAGE_UI_CANCEL_NEW);
    ui.default_button = maage_ui_button(&ui, MAAGE_PAGE_INSTALLATIONS, L"Set as Default", 540, 559, 220, MAAGE_UI_DEFAULT);
    ui.active_button = maage_ui_button(&ui, MAAGE_PAGE_INSTALLATIONS, L"Make Active for Play", 770, 559, 220, MAAGE_UI_ACTIVE);
    ui.folder_button = maage_ui_button(&ui, MAAGE_PAGE_INSTALLATIONS, L"Open installation folder", 540, 602, 245, MAAGE_UI_OPEN_FOLDER);
    maage_ui_label(&ui, MAAGE_PAGE_ACCOUNTS, L"Only verified Minecraft Java Edition access enables Play.", 198, 130, -230, 30);
    ui.profiles = maage_ui_control(&ui, MAAGE_PAGE_ACCOUNTS, WC_LISTVIEWW, L"Minecraft accounts", WS_TABSTOP | LVS_REPORT | LVS_SINGLESEL | LVS_SHOWSELALWAYS, 198, 172, -230, 116, MAAGE_UI_PROFILES);
    ListView_SetExtendedListViewStyle(ui.profiles, LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER | LVS_EX_LABELTIP);
    memset(&column, 0, sizeof(column));
    column.mask = LVCF_TEXT | LVCF_WIDTH;
    column.pszText = L"Profile";
    column.cx = maage_launcher_win32_scale(&ui.native, 240);
    SendMessageW(ui.profiles, LVM_INSERTCOLUMNW, 0, (LPARAM) &column);
    column.pszText = L"Verification";
    column.cx = maage_launcher_win32_scale(&ui.native, 360);
    SendMessageW(ui.profiles, LVM_INSERTCOLUMNW, 1, (LPARAM) &column);
    ui.account_status = maage_ui_label(&ui, MAAGE_PAGE_ACCOUNTS, L"", 198, 304, -230, 64);
    ui.sign_in = maage_ui_button(&ui, MAAGE_PAGE_ACCOUNTS, L"Sign In with Microsoft", 198, 382, 205, MAAGE_UI_SIGN_IN);
    ui.device_sign_in = maage_ui_button(&ui, MAAGE_PAGE_ACCOUNTS, L"Use Device Code", 413, 382, 170, MAAGE_UI_DEVICE_SIGN_IN);
    ui.verify = maage_ui_button(&ui, MAAGE_PAGE_ACCOUNTS, L"Refresh / Retry", 593, 382, 170, MAAGE_UI_VERIFY);
    ui.sign_out = maage_ui_button(&ui, MAAGE_PAGE_ACCOUNTS, L"Sign Out", 198, 426, 130, MAAGE_UI_SIGN_OUT);
    ui.remove_account = maage_ui_button(&ui, MAAGE_PAGE_ACCOUNTS, L"Remove Account", 338, 426, 170, MAAGE_UI_REMOVE_ACCOUNT);
    ui.cancel_account = maage_ui_button(&ui, MAAGE_PAGE_ACCOUNTS, L"Cancel Sign In", 518, 426, 170, MAAGE_UI_CANCEL_ACCOUNT);
    ui.authentication = maage_ui_label(&ui, MAAGE_PAGE_ACCOUNTS, L"", 198, 474, -230, 88);
    ui.copy_code = maage_ui_button(&ui, MAAGE_PAGE_ACCOUNTS, L"Copy Code", 198, 574, 140, MAAGE_UI_COPY_CODE);
    ui.open_device = maage_ui_button(&ui, MAAGE_PAGE_ACCOUNTS, L"Open Microsoft Sign In", 350, 574, 235, MAAGE_UI_OPEN_DEVICE);
    maage_ui_label(&ui, MAAGE_PAGE_SETTINGS, L"Accounts and resources prepare in the background while you use the Launcher.", 198, 130, -230, 46);
    maage_ui_label(&ui, MAAGE_PAGE_SETTINGS, L"Resources", 198, 190, -230, 26);
    control = maage_ui_control(&ui, MAAGE_PAGE_SETTINGS, L"EDIT", L"Minecraft resource location", ES_READONLY | ES_AUTOHSCROLL | WS_TABSTOP, 198, 226, -230, 30, 0);
    maage_ui_text(control, state->resources);
    ui.resource = maage_ui_label(&ui, MAAGE_PAGE_SETTINGS, L"", 198, 270, -230, 88);
    ui.verify_resources = maage_ui_button(&ui, MAAGE_PAGE_SETTINGS, L"Verify Resources", 198, 370, 190, MAAGE_UI_VERIFY_RESOURCES);
    ui.repair = maage_ui_button(&ui, MAAGE_PAGE_SETTINGS, L"Repair Resources", 400, 370, 190, MAAGE_UI_REPAIR);
    ui.retry_resources = maage_ui_button(&ui, MAAGE_PAGE_SETTINGS, L"Retry", 198, 414, 120, MAAGE_UI_RETRY_RESOURCES);
    ui.cancel_resources = maage_ui_button(&ui, MAAGE_PAGE_SETTINGS, L"Cancel", 330, 414, 120, MAAGE_UI_CANCEL_RESOURCES);
    maage_ui_label(&ui, MAAGE_PAGE_SETTINGS, L"Verify checks contents. Repair also restores missing or damaged files.", 198, 460, -230, 38);
    maage_ui_label(&ui, MAAGE_PAGE_SETTINGS, L"Appearance", 198, 510, 210, 24);
    ui.appearance = maage_ui_control(&ui, MAAGE_PAGE_SETTINGS, L"COMBOBOX", L"Launcher appearance", WS_TABSTOP | CBS_DROPDOWNLIST, 420, 506, 230, 150, MAAGE_UI_APPEARANCE);
    SendMessageW(ui.appearance, CB_ADDSTRING, 0, (LPARAM) L"System");
    SendMessageW(ui.appearance, CB_ADDSTRING, 0, (LPARAM) L"Light");
    SendMessageW(ui.appearance, CB_ADDSTRING, 0, (LPARAM) L"Dark");
    SendMessageW(ui.appearance, CB_SETCURSEL, state->appearance, 0);
    control = maage_ui_label(&ui, 4, L"MaaGE", 198, 130, 400, 44);
    SendMessageW(control, WM_SETFONT, (WPARAM) ui.native.brand, TRUE);
    maage_ui_label(&ui, 4, L"Minecraft as a Game Engine", 198, 180, -230, 30);
    maage_ui_label(&ui, 4, L"Launcher build", 198, 235, 420, 28);
    maage_ui_label(&ui, 4, L"Version " MAAGE_LAUNCHER_SEMANTIC_VERSION, 198, 270, -230, 27);
    maage_ui_label(&ui, 4, L"Git revision " MAAGE_LAUNCHER_GIT_HASH, 198, 306, -230, 27);
    maage_ui_label(&ui, 4, L"Open source and attribution", 198, 362, 440, 28);
    maage_ui_label(&ui, 4, L"MaaGE's original source code is licensed under the Apache License, Version 2.0.\nMinecraft game resources have separate ownership and licensing.", 198, 400, -230, 62);
    maage_ui_label(&ui, 4, L"MaaGE is an independent project and is not affiliated with or endorsed by Mojang Studios or Microsoft.", 198, 478, -230, 55);
    maage_ui_button(&ui, 4, L"View MaaGE on GitHub", 198, 552, 216, MAAGE_UI_PROJECT);
    maage_ui_button(&ui, 4, L"Back to Settings", 430, 552, 182, MAAGE_UI_BACK_SETTINGS);
    maage_ui_label(&ui, -1, L"Launcher " MAAGE_LAUNCHER_VERSION, 198, -42, -548, 26);
    ui.status = maage_ui_button(&ui, -1, L"", -360, -48, 330, MAAGE_UI_STATUS);
    ui.error = maage_ui_label(&ui, -1, L"", 198, -90, -230, 38);
    ui.progress = maage_ui_control(&ui, -1, PROGRESS_CLASSW, L"Background preparation progress", 0, 198, -112, -230, 8, 0);
    SendMessageW(ui.progress, PBM_SETRANGE32, 0, 100);
    if (maage_status_failed(ui.native.result)) {
        outcome = ui.native.result;
        DestroyWindow(ui.window);
        goto cleanup;
    }
    ui.home_tooltip = CreateWindowExW(WS_EX_TOPMOST, TOOLTIPS_CLASSW, NULL, WS_POPUP | TTS_ALWAYSTIP | TTS_NOPREFIX, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, ui.window, NULL, klass.hInstance, NULL);
    for (i = 0; i < (int) ui.native.count; ++i) {
        HWND child = ui.native.controls[i].handle;
        if (ui.home_tooltip && maage_ui_is_home_button(&ui, child)) {
            TOOLINFOW tool;
            memset(&tool, 0, sizeof(tool));
            tool.cbSize = sizeof(tool);
            tool.uFlags = TTF_IDISHWND | TTF_SUBCLASS;
            tool.hwnd = ui.window;
            tool.uId = (UINT_PTR) child;
            tool.lpszText = LPSTR_TEXTCALLBACKW;
            SendMessageW(ui.home_tooltip, TTM_ADDTOOLW, 0, (LPARAM) &tool);
        }
        if (maage_ui_is_home_button(&ui, child) && !SetWindowSubclass(child, maage_ui_painted_control, 2, (DWORD_PTR) &ui)) {
            outcome = maage_status_make(MAAGE_STATUS_ERROR, MAAGE_CONTEXT_LAUNCHER, MAAGE_OPERATION_INITIALIZE, 0);
            DestroyWindow(ui.window);
            goto cleanup;
        }
    }
    if (!SetWindowSubclass(ui.progress, maage_ui_painted_control, 2, (DWORD_PTR) &ui)) {
        outcome = maage_status_make(MAAGE_STATUS_ERROR, MAAGE_CONTEXT_LAUNCHER, MAAGE_OPERATION_INITIALIZE, 0);
        DestroyWindow(ui.window);
        goto cleanup;
    }
    /* An owned compositor surface avoids clipping the real window's child
     * controls during the fade, and has no separate taskbar entry. */
    ui.transition_window = CreateWindowExW(WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW, L"STATIC", NULL, WS_POPUP, 0, 0, 0, 0, ui.window, NULL, klass.hInstance, NULL);
    if (ui.transition_window && !SetWindowSubclass(ui.transition_window, maage_ui_transition_proc, 1, (DWORD_PTR) &ui)) {
        DestroyWindow(ui.transition_window);
        ui.transition_window = NULL;
    }
    ui.controls_ready = 1;
    maage_ui_splash_fonts(&ui);
    maage_ui_appearance(&ui);
    maage_ui_installations_reload(&ui, -1);
    maage_ui_layout(&ui);
    maage_ui_refresh(&ui);
    if (ui.state->page == MAAGE_PAGE_INSTALLATIONS) maage_ui_installations_ensure_selection(&ui);
    if (!ui.splash_visible) SetFocus(ui.state->page == MAAGE_PAGE_PLAY ? ui.play : ui.navigation[ui.state->page]);
    if (!SetTimer(ui.window, 1, 250, NULL)) {
        outcome = maage_status_from_win32((unsigned long) GetLastError(), MAAGE_CONTEXT_LAUNCHER, MAAGE_OPERATION_OPEN);
        DestroyWindow(ui.window);
    } else {
        ShowWindow(ui.window, SW_SHOW);
        if (ui.splash_visible && ui.animate_splash) SetTimer(ui.window, MAAGE_UI_ANIMATION_TIMER, MAAGE_UI_ANIMATION_INTERVAL, NULL);
        UpdateWindow(ui.window);
        MAAGE_LOG_DEBUG_EVENT("maage.launcher.startup", maage_log_debug("maage.launcher.startup", "Win32 main window creation/presentation: %.3f s.", maage_time_monotonic_seconds() - started));
        maage_launcher_ui_presented(state);
    }
    while ((received = GetMessageW(&message, NULL, 0, 0)) > 0)
        if (!IsDialogMessageW(ui.window, &message)) {
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
    if (received < 0) {
        outcome = maage_status_from_win32((unsigned long) GetLastError(), MAAGE_CONTEXT_LAUNCHER, MAAGE_OPERATION_OPEN);
        if (IsWindow(ui.window)) DestroyWindow(ui.window);
    }
cleanup:
    cleanup = maage_launcher_win32_style_destroy(&ui.native);
    if (!maage_status_failed(outcome) && maage_status_failed(cleanup)) outcome = cleanup;
    if (ui.splash_title_font) DeleteObject(ui.splash_title_font);
    if (ui.splash_detail_font) DeleteObject(ui.splash_detail_font);
    if (ui.splash_log_font) DeleteObject(ui.splash_log_font);
    for (i = 0; i < MAAGE_UI_HOME_FONTS; ++i)
        if (ui.home_fonts[i].handle) DeleteObject(ui.home_fonts[i].handle);
    maage_win32_surface_destroy(&ui.transition);
    maage_win32_surface_destroy(&ui.button_surface);
    maage_win32_surface_destroy(&ui.footer_surface);
    maage_win32_surface_destroy(&ui.backdrop);
    maage_win32_surface_destroy(&ui.play_shadow);
    maage_win32_surface_destroy(&ui.card_shadow);
    maage_win32_surface_destroy(&ui.logo_shadow);
    maage_win32_surface_destroy(&ui.logo);
    maage_win32_surface_destroy(&ui.home_background);
    for (i = 0; i < MAAGE_LAUNCHER_HOME_ICON_COUNT; ++i) maage_win32_surface_destroy(&ui.home_icons[i]);
    if (ui.artwork) DestroyIcon(ui.artwork);
    DeleteObject(ui.background);
    DeleteObject(ui.sidebar);
    UnregisterClassW(klass.lpszClassName, klass.hInstance);
    return outcome;
}
