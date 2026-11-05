#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <windowsx.h>
#include <commctrl.h>
#include <shellapi.h>
#include <commdlg.h>
#include "launcher.h"
#include "../native/windows/win32_ui.h"
#include "maage/launcher_version.h"
#include "maage/log.h"
#include "maage/time.h"
#include <string.h>
#define MAAGE_UI_ACCOUNT 110
#define MAAGE_UI_INSTALLATION 111
#define MAAGE_UI_LIST 112
#define MAAGE_UI_NAME 113
#define MAAGE_UI_ARGUMENTS 114
#define MAAGE_UI_MANAGE 120
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
#define MAAGE_UI_RESOURCES_PAGE 174
typedef struct maage_launcher_win32 {
    maage_launcher_ui *state;
    HWND window, navigation[5], account, installation, list, name, arguments, status, resource, account_status, verify, repair, error, clients, client_choice, client_status, play;
    HWND clients_label, clients_import, clients_replace, clients_remove, installations_label, list_hint, new_button, editor_heading, editor_hint, name_label, client_label, arguments_label;
    HWND active_summary, launch_status, authentication, profiles, sign_in, device_sign_in, sign_out, remove_account, cancel_account, copy_code, open_device, verify_resources, retry_resources, cancel_resources, progress, appearance;
    HWND save_button, duplicate_button, remove_button, default_button, active_button, cancel_button, folder_button;
    maage_launcher_win32_style native;
    HBRUSH background, sidebar;
    unsigned revision, client_revision;
    int updating, creating, previous_row, editor_row, scroll[5], scrollbar_visible, displayed_appearance, marquee, progress_value;
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
    return maage_ui_control(ui, page, L"BUTTON", title, WS_TABSTOP | BS_PUSHBUTTON, x, y, width, 32, id);
}

/* Installation management uses a two-pane editor when space allows. On compact
 * windows it stacks the panes and scrolls the content; the native list keeps its
 * own keyboard, accessibility and selection behavior. All positions are 96-DPI. */
static void maage_ui_installation_place(maage_launcher_win32 *ui, HWND control, int x, int y, int width, int height, int offset, int bottom) {
    int shown_height, visible;
    if (!control) return;
    y += 52;
    y -= offset;
    shown_height = control == ui->list ? height : height > 36 ? 36
                                                              : height;
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
    if (visible) MoveWindow(control, maage_launcher_win32_scale(&ui->native, x), maage_launcher_win32_scale(&ui->native, y), maage_launcher_win32_scale(&ui->native, width), maage_launcher_win32_scale(&ui->native, height), TRUE);
    ShowWindow(control, visible ? SW_SHOW : SW_HIDE);
}

static void maage_ui_installations_layout(maage_launcher_win32 *ui, int width, int height) {
    int left = 198, full = width - 230, stacked = full < 680, bottom = height - 120;
    int list_width = stacked ? full : full * 42 / 100;
    int editor_x = stacked ? left : left + list_width + 24;
    int editor_width = stacked ? full : full - list_width - 24;
    int editor_y = stacked ? 560 : 206;
    int offset = ui->scroll[MAAGE_PAGE_INSTALLATIONS], list_offset = stacked ? offset : 0;
    int action_width = (editor_width - 20) / 3, secondary_width = (editor_width - 10) / 2;
    int client_width = stacked ? (full - 20) / 3 : 182;
    maage_ui_text(ui->list_hint, stacked ? "Select to edit, then scroll down." : "Select to edit; double-click to activate.");
    maage_ui_installation_place(ui, ui->clients_label, left, 78, full, 26, offset, bottom);
    maage_ui_installation_place(ui, ui->clients, left, 110, full, 240, offset, bottom);
    maage_ui_installation_place(ui, ui->clients_import, left, 151, client_width, 32, offset, bottom);
    maage_ui_installation_place(ui, ui->clients_replace, left + client_width + 10, 151, client_width, 32, offset, bottom);
    maage_ui_installation_place(ui, ui->clients_remove, left + (ui->clients_replace ? 2 * (client_width + 10) : 0), 151, client_width, 32, offset, bottom);
    maage_ui_installation_place(ui, ui->installations_label, left, 205, list_width - 120, 32, offset, bottom);
    maage_ui_installation_place(ui, ui->new_button, left + list_width - 116, 205, 116, 32, offset, bottom);
    maage_ui_installation_place(ui, ui->list_hint, left, 244, list_width, 28, offset, bottom);
    maage_ui_installation_place(ui, ui->list, left, 280, list_width, stacked ? 235 : height > 680 ? height - 400
                                                                                                  : 280,
                                list_offset, bottom);
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
    ListView_SetColumnWidth(ui->list, 0, maage_launcher_win32_scale(&ui->native, list_width - 132));
    ListView_SetColumnWidth(ui->list, 1, maage_launcher_win32_scale(&ui->native, 120));
}

/* Only the main content scrolls. Sidebar navigation and the status footer
 * remain visible, even when a compact window needs additional page height. */
static void maage_ui_layout(maage_launcher_win32 *ui) {
    RECT rect;
    SCROLLINFO scroll;
    unsigned i;
    int width, height, x, y, w, h, page;
    int top = 124, bottom, content_bottom = 0, max_scroll, visible_scroll, visible;
    if (!ui->native.count) return;
    GetClientRect(ui->window, &rect);
    width = MulDiv(rect.right, 96, ui->native.dpi ? (int) ui->native.dpi : 96);
    height = MulDiv(rect.bottom, 96, ui->native.dpi ? (int) ui->native.dpi : 96);
    page = (int) ui->state->page;
    bottom = height - 120;
    if (page == MAAGE_PAGE_INSTALLATIONS) content_bottom = width - 230 < 680 ? 1052 : 687;
    else if (page == MAAGE_PAGE_ACCOUNTS) content_bottom = 610;
    else if (page == MAAGE_PAGE_SETTINGS) content_bottom = 566;
    else if (page == MAAGE_PAGE_ABOUT) content_bottom = 584;
    max_scroll = content_bottom - bottom;
    if (max_scroll < 0) max_scroll = 0;
    if (page < MAAGE_PAGE_INSTALLATIONS) max_scroll = 0;
    if (ui->scroll[page] > max_scroll) ui->scroll[page] = max_scroll;
    visible_scroll = max_scroll > 0;
    if (ui->scrollbar_visible != visible_scroll) {
        ui->scrollbar_visible = visible_scroll;
        ShowScrollBar(ui->window, SB_VERT, visible_scroll);
        GetClientRect(ui->window, &rect);
        width = MulDiv(rect.right, 96, ui->native.dpi ? (int) ui->native.dpi : 96);
    }
    memset(&scroll, 0, sizeof(scroll));
    scroll.cbSize = sizeof(scroll);
    scroll.fMask = SIF_RANGE | SIF_PAGE | SIF_POS;
    scroll.nMin = 0;
    scroll.nMax = max_scroll + bottom - top - 1;
    scroll.nPage = (UINT) (bottom > top ? bottom - top : 1);
    scroll.nPos = ui->scroll[page];
    SetScrollInfo(ui->window, SB_VERT, &scroll, TRUE);
    for (i = 0; i < ui->native.count; ++i) {
        maage_launcher_control *c = &ui->native.controls[i];
        x = c->x;
        y = c->y;
        w = c->width;
        h = c->height;
        visible = c->page < 0 || c->page == page;
        if (c->page == MAAGE_PAGE_INSTALLATIONS && c->y != 80) {
            if (page != MAAGE_PAGE_INSTALLATIONS) ShowWindow(c->handle, SW_HIDE);
            continue;
        }
        if (y < 0) y += height;
        if (x < 0) x += width;
        if (w < 0) w += width;
        if (h < 0) h += height;
        if (c->page == MAAGE_PAGE_PLAY && c->y >= 150) y += height > 740 ? (height - 740) / 3 : 0;
        if (c->page >= MAAGE_PAGE_INSTALLATIONS && c->page == page && c->y != 80) {
            int shown_height = c->handle == ui->appearance ? 32 : h;
            y -= ui->scroll[page];
            if (y < top || y + shown_height > bottom) visible = 0;
        }
        if (visible) MoveWindow(c->handle, maage_launcher_win32_scale(&ui->native, x), maage_launcher_win32_scale(&ui->native, y), maage_launcher_win32_scale(&ui->native, w), maage_launcher_win32_scale(&ui->native, h), TRUE);
        ShowWindow(c->handle, visible ? SW_SHOW : SW_HIDE);
    }
    if (page == MAAGE_PAGE_INSTALLATIONS) maage_ui_installations_layout(ui, width, height);
    ListView_SetColumnWidth(ui->profiles, 0, maage_launcher_win32_scale(&ui->native, (width - 230) * 36 / 100));
    ListView_SetColumnWidth(ui->profiles, 1, maage_launcher_win32_scale(&ui->native, (width - 230) * 60 / 100));
    for (i = 0; i < 5; ++i)
        SendMessageW(ui->navigation[i], BM_SETCHECK, i == (unsigned) page ? BST_CHECKED : BST_UNCHECKED, 0);
    InvalidateRect(ui->window, NULL, TRUE);
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
    SendMessageW(ui->installation, CB_RESETCONTENT, 0, 0);
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
        SendMessageW(ui->installation, CB_ADDSTRING, 0, (LPARAM) wide);
        if (entry->id == ui->state->selected) {
            active = (int) i;
            SendMessageW(ui->installation, CB_SETCURSEL, i, 0);
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
    int active = -1;
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
        if (entry->id == ui->state->selected) active = (int) i;
    }
    if (active >= 0) SendMessageW(ui->installation, CB_SETCURSEL, active, 0);
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

static void maage_ui_refresh(maage_launcher_win32 *ui) {
    maage_launcher_ui *state = ui->state;
    unsigned i;
    int selected = -1, added = 0;
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
    maage_ui_text(ui->client_status, state->client_status);
    maage_ui_text(ui->launch_status, state->launch_status);
    maage_ui_text(ui->active_summary, state->active_summary);
    maage_ui_text(ui->authentication, state->authentication_status);
    maage_ui_enabled(ui->play, state->can_play);
    maage_ui_text(ui->play, state->client_process ? "Client Running" : state->launch_pending      ? "Checking Resources…"
                                                                   : state->client_operation == 4 ? "Starting Client…"
                                                                                                  : "Play");
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
    maage_ui_text(ui->error, state->error);
    if (state->progress.busy || state->accounts.busy) {
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
        ShowWindow(ui->progress, SW_SHOW);
    } else {
        if (ui->marquee) SendMessageW(ui->progress, PBM_SETMARQUEE, FALSE, 0);
        ui->marquee = 0;
        ShowWindow(ui->progress, SW_HIDE);
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
static LRESULT CALLBACK maage_ui_proc(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
    maage_launcher_win32 *ui = (maage_launcher_win32 *) GetWindowLongPtrW(window, GWLP_USERDATA);
    if (message == WM_NCCREATE) {
        ui = (maage_launcher_win32 *) ((CREATESTRUCTW *) lparam)->lpCreateParams;
        ui->window = window;
        SetWindowLongPtrW(window, GWLP_USERDATA, (LONG_PTR) ui);
    }
    if (!ui) return DefWindowProcW(window, message, wparam, lparam);
    switch (message) {
    case WM_GETMINMAXINFO:
        ((MINMAXINFO *) lparam)->ptMinTrackSize.x = maage_launcher_win32_scale(&ui->native, 820);
        ((MINMAXINFO *) lparam)->ptMinTrackSize.y = maage_launcher_win32_scale(&ui->native, 650);
        return 0;
    case WM_DPICHANGED: {
        maage_status result = maage_launcher_win32_dpi_changed(&ui->native, wparam, lparam);
        if (maage_status_failed(result)) maage_log_status(MAAGE_LOG_WARNING, "maage.launcher.ui", result, "Could not update Launcher DPI.");
        maage_ui_layout(ui);
        return 0;
    }
    case WM_SETTINGCHANGE:
    case WM_THEMECHANGED:
        if (ui->native.window) maage_ui_appearance(ui);
        break;
    case WM_SIZE:
        maage_ui_layout(ui);
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
        maage_ui_refresh(ui);
        return 0;
    case WM_COMMAND: {
        int id = LOWORD(wparam), event = HIWORD(wparam);
#if MAAGE_LAUNCHER_DEVELOPMENT
        if (id == MAAGE_UI_CLIENT_IMPORT) maage_ui_client_import(ui, 0);
        if (id == MAAGE_UI_CLIENT_REPLACE) maage_ui_client_import(ui, 1);
#endif
        if (id == MAAGE_UI_CLIENT_REMOVE && !ui->state->client_job) {
            LRESULT selected = SendMessageW(ui->clients, CB_GETCURSEL, 0, 0);
            if (selected >= 0 && MessageBoxW(window, L"Remove unused managed Client files? Shared Minecraft resources and developer originals are preserved.", L"Remove Client", MB_OKCANCEL | MB_ICONQUESTION) == IDOK) maage_launcher_client_remove(ui->state, (unsigned) selected);
        }
        if (id == 150) maage_launcher_client_play(ui->state);
        if (id >= 100 && id < 105) {
            ui->state->page = (maage_launcher_page) (id - 100);
            maage_ui_layout(ui);
            if (ui->state->page == MAAGE_PAGE_INSTALLATIONS) maage_ui_installations_ensure_selection(ui);
            return 0;
        }
        if (id == MAAGE_UI_ABOUT || id == MAAGE_UI_BACK_SETTINGS) {
            ui->state->page = id == MAAGE_UI_ABOUT ? MAAGE_PAGE_ABOUT : MAAGE_PAGE_SETTINGS;
            maage_ui_layout(ui);
            return 0;
        }
        if (id == MAAGE_UI_STATUS || id == MAAGE_UI_RESOURCES_PAGE) {
            ui->state->page = id == MAAGE_UI_STATUS ? maage_launcher_attention_page(ui->state) : MAAGE_PAGE_SETTINGS;
            maage_ui_layout(ui);
            return 0;
        }
        if (id == MAAGE_UI_ACCOUNT && event == CBN_SELCHANGE && !ui->updating) {
            LRESULT row = SendMessageW(ui->account, CB_GETCURSEL, 0, 0), index;
            index = SendMessageW(ui->account, CB_GETITEMDATA, row, 0);
            if (index >= 0 && (unsigned) index < ui->state->accounts.count) maage_launcher_account_select(ui->state, ui->state->accounts.profiles[index].uuid);
            else if (index == -2) {
                ui->state->page = MAAGE_PAGE_ACCOUNTS;
                maage_ui_layout(ui);
            }
            ui->revision = 0;
        }
        if (id == MAAGE_UI_INSTALLATION && event == CBN_SELCHANGE && !ui->updating) {
            maage_status result = maage_launcher_installation_select(ui->state, (unsigned) SendMessageW(ui->installation, CB_GETCURSEL, 0, 0));
            if (!maage_status_failed(result)) maage_ui_installations_roles(ui);
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
        if (id == MAAGE_UI_MANAGE) {
            ui->state->page = MAAGE_PAGE_ACCOUNTS;
            maage_ui_layout(ui);
        }
        maage_ui_refresh(ui);
        return 0;
    }
    case WM_NOTIFY:
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
        return maage_launcher_win32_combo_draw(&ui->native, lparam);
    case WM_CTLCOLORSTATIC:
    case WM_CTLCOLOREDIT:
    case WM_CTLCOLORLISTBOX:
        SetTextColor((HDC) wparam, maage_launcher_win32_text_color(&ui->native));
        SetBkColor((HDC) wparam, maage_launcher_win32_color(&ui->native, 0));
        SetBkMode((HDC) wparam, TRANSPARENT);
        return (LRESULT) ui->background;
    case WM_ERASEBKGND: {
        RECT rect;
        GetClientRect(window, &rect);
        FillRect((HDC) wparam, &rect, ui->background);
        rect.right = maage_launcher_win32_scale(&ui->native, 170);
        FillRect((HDC) wparam, &rect, ui->sidebar);
        return 1;
    }
    case WM_CLOSE:
        DestroyWindow(window);
        return 0;
    case WM_DESTROY:
        KillTimer(window, 1);
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
    ui.background = CreateSolidBrush(GetSysColor(COLOR_WINDOW));
    ui.sidebar = CreateSolidBrush(GetSysColor(COLOR_BTNFACE));
    memset(&klass, 0, sizeof(klass));
    klass.lpfnWndProc = maage_ui_proc;
    klass.hInstance = GetModuleHandleW(NULL);
    klass.hCursor = LoadCursor(NULL, IDC_ARROW);
    klass.lpszClassName = L"MaaGELauncher";
    RegisterClassW(&klass);
    ui.window = CreateWindowExW(WS_EX_CONTROLPARENT, klass.lpszClassName, L"MaaGE Launcher", WS_CLIPCHILDREN | WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, 1080, 780, NULL, NULL, klass.hInstance, &ui);
    if (!ui.window) {
        outcome = maage_status_from_win32((unsigned long) GetLastError(), MAAGE_CONTEXT_LAUNCHER, MAAGE_OPERATION_OPEN);
        goto cleanup;
    }
    outcome = maage_launcher_win32_style_init(&ui.native, ui.window, 1080, 740, 0);
    if (maage_status_failed(outcome)) {
        DestroyWindow(ui.window);
        goto cleanup;
    }
    if (!ui.background || !ui.sidebar) {
        outcome = maage_status_make(MAAGE_STATUS_ALLOCATION_FAILED, MAAGE_CONTEXT_LAUNCHER, MAAGE_OPERATION_CREATE, 0);
        DestroyWindow(ui.window);
        goto cleanup;
    }
    maage_ui_appearance(&ui);
    maage_ui_label(&ui, -1, L"MaaGE", 20, 24, 130, 24);
    for (i = 0; i < 5; ++i)
        ui.navigation[i] = maage_ui_control(&ui, -1, L"BUTTON", names[i], WS_TABSTOP | BS_AUTORADIOBUTTON | BS_PUSHLIKE, 14, i >= 3 ? -104 + (i - 3) * 46 : 100 + i * 46, 142, 34, 100 + i);
    {
        const wchar_t *headings[] = {L"Play", L"Installations", L"Accounts", L"Settings", L"About MaaGE"};
        for (i = 0; i < 5; ++i) {
            control = maage_ui_label(&ui, i, headings[i], 198, 80, 330, 38);
            SendMessageW(control, WM_SETFONT, (WPARAM) ui.native.heading, TRUE);
        }
    }
    ui.active_summary = maage_ui_label(&ui, -1, L"Starting Launcher…", 198, 24, -540, 28);
    SetWindowLongPtrW(ui.active_summary, GWL_STYLE, GetWindowLongPtrW(ui.active_summary, GWL_STYLE) | SS_ENDELLIPSIS);
    ui.account = maage_ui_control(&ui, -1, L"COMBOBOX", L"Active Minecraft account", WS_TABSTOP | CBS_DROPDOWNLIST | WS_VSCROLL, -310, 18, 280, 260, MAAGE_UI_ACCOUNT);
    maage_ui_label(&ui, MAAGE_PAGE_PLAY, L"Choose your installation and jump into MaaGE.", 198, 130, -230, 28);
    maage_ui_label(&ui, MAAGE_PAGE_PLAY, L"Installation", 198, 176, -230, 24);
    ui.installation = maage_ui_control(&ui, MAAGE_PAGE_PLAY, L"COMBOBOX", L"Installation for Play", WS_TABSTOP | CBS_DROPDOWNLIST | WS_VSCROLL, 198, 208, -230, 230, MAAGE_UI_INSTALLATION);
    ui.client_status = maage_ui_label(&ui, MAAGE_PAGE_PLAY, L"Loading Client builds…", 198, 254, -230, 52);
    ui.launch_status = maage_ui_label(&ui, MAAGE_PAGE_PLAY, L"Starting Launcher services…", 198, 316, -230, 60);
    ui.play = maage_ui_control(&ui, MAAGE_PAGE_PLAY, L"BUTTON", L"Play", WS_TABSTOP | BS_PUSHBUTTON, 198, 394, 280, 48, 150);
    ui.native.primary = ui.play;
    SendMessageW(ui.play, WM_SETFONT, (WPARAM) ui.native.heading, TRUE);
    maage_ui_enabled(ui.play, FALSE);
    maage_ui_button(&ui, MAAGE_PAGE_PLAY, L"Accounts", 198, 466, 150, MAAGE_UI_MANAGE);
    maage_ui_button(&ui, MAAGE_PAGE_PLAY, L"Resources", 360, 466, 150, MAAGE_UI_RESOURCES_PAGE);
    ui.clients_label = maage_ui_label(&ui, MAAGE_PAGE_INSTALLATIONS, L"Installed Clients", 198, 78, -230, 26);
    ui.clients = maage_ui_control(&ui, MAAGE_PAGE_INSTALLATIONS, L"COMBOBOX", L"Installed Client builds", WS_TABSTOP | CBS_DROPDOWNLIST | WS_VSCROLL, 198, 110, -230, 240, MAAGE_UI_CLIENTS);
#if MAAGE_LAUNCHER_DEVELOPMENT
    ui.clients_import = maage_ui_button(&ui, MAAGE_PAGE_INSTALLATIONS, L"Import Local Client...", 198, 150, 200, MAAGE_UI_CLIENT_IMPORT);
    ui.clients_replace = maage_ui_button(&ui, MAAGE_PAGE_INSTALLATIONS, L"Replace Client...", 410, 150, 170, MAAGE_UI_CLIENT_REPLACE);
    ui.clients_remove = maage_ui_button(&ui, MAAGE_PAGE_INSTALLATIONS, L"Remove Client...", 592, 150, 170, MAAGE_UI_CLIENT_REMOVE);
#else
    ui.clients_remove = maage_ui_button(&ui, MAAGE_PAGE_INSTALLATIONS, L"Remove Client...", 198, 150, 170, MAAGE_UI_CLIENT_REMOVE);
#endif
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
    maage_ui_appearance(&ui);
    maage_ui_installations_reload(&ui, -1);
    maage_ui_layout(&ui);
    maage_ui_refresh(&ui);
    if (ui.state->page == MAAGE_PAGE_INSTALLATIONS) maage_ui_installations_ensure_selection(&ui);
    SetFocus(ui.navigation[0]);
    if (!SetTimer(ui.window, 1, 250, NULL)) {
        outcome = maage_status_from_win32((unsigned long) GetLastError(), MAAGE_CONTEXT_LAUNCHER, MAAGE_OPERATION_OPEN);
        DestroyWindow(ui.window);
    } else {
        ShowWindow(ui.window, SW_SHOW);
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
    DeleteObject(ui.background);
    DeleteObject(ui.sidebar);
    UnregisterClassW(klass.lpszClassName, klass.hInstance);
    return outcome;
}
