#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shellapi.h>
#include "../access/oauth.h"
#include "maage/time.h"
#include <uxtheme.h>
#include "../access/access.h"
#include <string.h>
#include <stdio.h>
typedef struct maage_access_win32 {
    maage_access *access;
    maage_access_snapshot snapshot;
    HWND window, profiles, identity, status, connect, stored, disconnect, rescan, next, cancel, device, copy, open, stop, signout;
    HFONT font, heading;
    HBRUSH background, panel;
    unsigned revision;
    int result, closing, manage_accounts;
} maage_access_win32;
static void maage_access_win32_text(HWND control, const char *text)
{
    wchar_t wide[2048];
    if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text, -1, wide, 2048)) SetWindowTextW(control, wide);
}
static HWND maage_access_win32_control(maage_access_win32 *ui, const wchar_t *kind, const wchar_t *text, DWORD style, int x, int y, int width, int height, int id)
{
    HWND control = CreateWindowExW(0, kind, text, WS_CHILD | WS_VISIBLE | style, x, y, width, height, ui->window, (HMENU)(INT_PTR)id, GetModuleHandleW(NULL), NULL);
    SendMessageW(control, WM_SETFONT, (WPARAM)ui->font, TRUE); return control;
}
static int maage_access_win32_selected(maage_access_win32 *ui)
{
    LRESULT selected = SendMessageW(ui->profiles, CB_GETCURSEL, 0, 0);
    return selected >= 0 && (unsigned)selected < ui->snapshot.count ? (int)selected : -1;
}
static void maage_access_win32_tick(maage_access_win32 *ui)
{
    char uuid[33] = "", text[1024];
    wchar_t wide[512];
    unsigned i;
    int selected = maage_access_win32_selected(ui), idle;
    maage_access_profile *p;
    if (selected >= 0) strcpy(uuid, ui->snapshot.profiles[selected].uuid);
    maage_access_snapshot_get(ui->access, &ui->snapshot);
    if (ui->closing && !ui->snapshot.busy) { DestroyWindow(ui->window); return; }
    if (!ui->manage_accounts && ui->snapshot.auto_continue && ui->snapshot.allowed && !ui->snapshot.busy) { ui->result = 1; DestroyWindow(ui->window); return; }
    if (ui->revision != ui->snapshot.revision) {
        SendMessageW(ui->profiles, CB_RESETCONTENT, 0, 0); selected = 0;
        for (i = 0; i < ui->snapshot.count; ++i) {
            p = &ui->snapshot.profiles[i]; sprintf(text, "%s — %s", p->name, maage_access_state_name(p->state));
            if (MultiByteToWideChar(CP_UTF8, 0, text, -1, wide, 512)) SendMessageW(ui->profiles, CB_ADDSTRING, 0, (LPARAM)wide);
            if (!strcmp(p->uuid, uuid)) selected = (int)i;
        }
        if (!ui->snapshot.count) SendMessageW(ui->profiles, CB_ADDSTRING, 0, (LPARAM)L"No Minecraft accounts discovered");
        SendMessageW(ui->profiles, CB_SETCURSEL, (WPARAM)selected, 0); ui->revision = ui->snapshot.revision; InvalidateRect(ui->window, NULL, TRUE);
    }
    selected = maage_access_win32_selected(ui); p = selected >= 0 ? &ui->snapshot.profiles[selected] : NULL;
    if (p) sprintf(text, "%s\r\n%s · %s\r\n%s", p->uuid, *p->sources ? p->sources : "Source unavailable", maage_access_state_name(p->state), p->detail);
    else text[0] = 0;
    maage_access_win32_text(ui->identity, text); maage_access_win32_text(ui->status, ui->snapshot.status);
    idle = !ui->snapshot.busy && !ui->closing;
    EnableWindow(ui->profiles, idle && ui->snapshot.count > 0); EnableWindow(ui->connect, idle);
    EnableWindow(ui->stored, idle && p && p->session_saved);
    EnableWindow(ui->disconnect, idle && p && p->connected); EnableWindow(ui->rescan, idle); EnableWindow(ui->signout, idle && p && p->session_saved);
    EnableWindow(ui->stop, ui->snapshot.busy && !ui->closing);
    ShowWindow(ui->profiles, p ? SW_SHOW : SW_HIDE); ShowWindow(ui->identity, p ? SW_SHOW : SW_HIDE);
    ShowWindow(ui->stored, p ? SW_SHOW : SW_HIDE); ShowWindow(ui->disconnect, p ? SW_SHOW : SW_HIDE); ShowWindow(ui->signout, p ? SW_SHOW : SW_HIDE);
    ShowWindow(ui->copy, *ui->snapshot.device_code ? SW_SHOW : SW_HIDE); ShowWindow(ui->open, *ui->snapshot.device_code ? SW_SHOW : SW_HIDE);
    if (*ui->snapshot.device_code) {
        double seconds = ui->snapshot.device_deadline - maage_time_monotonic_seconds();
        sprintf(text, "%s\r\nCode: %s · %.0f seconds remaining", ui->snapshot.device_url, ui->snapshot.device_code, seconds > 0 ? seconds : 0);
    } else strcpy(text, "Your browser will open to complete sign-in.");
    maage_access_win32_text(ui->device, text);
    EnableWindow(ui->next, idle && ui->snapshot.allowed); EnableWindow(ui->cancel, !ui->closing);
    if (!IsWindowVisible(ui->window)) { ShowWindow(ui->window,SW_SHOW); UpdateWindow(ui->window); SetFocus(ui->profiles); }
}
static void maage_access_win32_copy(maage_access_win32 *ui)
{
    HGLOBAL memory;
    wchar_t *text;
    int count;
    if (!*ui->snapshot.device_code || !OpenClipboard(ui->window)) return;
    count = MultiByteToWideChar(CP_UTF8, 0, ui->snapshot.device_code, -1, NULL, 0);
    memory = GlobalAlloc(GMEM_MOVEABLE, (size_t)count * sizeof(wchar_t));
    text = memory ? (wchar_t *)GlobalLock(memory) : NULL;
    if (text) {
        MultiByteToWideChar(CP_UTF8, 0, ui->snapshot.device_code, -1, text, count); GlobalUnlock(memory);
        EmptyClipboard(); if (!SetClipboardData(CF_UNICODETEXT, memory)) GlobalFree(memory);
    } else if (memory) GlobalFree(memory);
    CloseClipboard();
}
static LRESULT CALLBACK maage_access_win32_proc(HWND window, UINT message, WPARAM wparam, LPARAM lparam)
{
    maage_access_win32 *ui = (maage_access_win32 *)GetWindowLongPtrW(window, GWLP_USERDATA);
    if (message == WM_NCCREATE) {
        ui = (maage_access_win32 *)((CREATESTRUCTW *)lparam)->lpCreateParams; ui->window = window; SetWindowLongPtrW(window, GWLP_USERDATA, (LONG_PTR)ui);
    }
    if (!ui) return DefWindowProcW(window, message, wparam, lparam);
    switch (message) {
    case WM_CLOSE:
        ui->closing = 1; maage_access_cancel(ui->access); maage_access_win32_tick(ui); return 0;
    case WM_TIMER: maage_access_win32_tick(ui); return 0;
    case WM_COMMAND:
        if (LOWORD(wparam) == IDCANCEL) SendMessageW(window, WM_CLOSE, 0, 0);
        else if (LOWORD(wparam) == IDOK) {
            if (!ui->snapshot.busy && maage_access_allowed(ui->access)) { ui->result = 1; DestroyWindow(window); }
        } else if (LOWORD(wparam) == 108) maage_access_win32_copy(ui);
        else if (LOWORD(wparam) == 109 && *ui->snapshot.device_url) maage_auth_browser(ui->snapshot.device_url);
        else if (LOWORD(wparam) == 110) maage_access_cancel_operation(ui->access);
        else if (!ui->snapshot.busy && !ui->closing) {
            int selected = maage_access_win32_selected(ui);
            if (LOWORD(wparam) == 103) maage_access_sign_in(ui->access, 1);
            else if (LOWORD(wparam) == 101) maage_access_sign_in(ui->access, 0);
            else if (selected >= 0 && LOWORD(wparam) == 107) maage_access_verify_local(ui->access, ui->snapshot.profiles[selected].uuid);
            else if (selected >= 0 && LOWORD(wparam) == 102) maage_access_disconnect(ui->access, ui->snapshot.profiles[selected].uuid);
            else if (selected >= 0 && LOWORD(wparam) == 111) maage_access_sign_out(ui->access, ui->snapshot.profiles[selected].uuid);
            maage_access_win32_tick(ui);
        }
        return 0;
    case WM_CTLCOLORSTATIC:
    case WM_CTLCOLORBTN:
    case WM_CTLCOLORLISTBOX:
        {
            RECT rect;
            int inset;
            GetWindowRect((HWND)lparam, &rect); MapWindowPoints(NULL, window, (POINT *)&rect, 2); inset = rect.top >= 166 && rect.top < 344;
            SetTextColor((HDC)wparam, (HWND)lparam == GetDlgItem(window, 106) ? RGB(232,74,172) : RGB(225,225,225));
            SetBkColor((HDC)wparam, inset ? RGB(41,41,41) : RGB(28,28,28)); return (LRESULT)(inset ? ui->panel : ui->background);
        }
    case WM_ERASEBKGND:
        {
            RECT rect; GetClientRect(window, &rect); FillRect((HDC)wparam, &rect, ui->background);
            if (ui->snapshot.count) { rect.left = 28; rect.top = 166; rect.right = 622; rect.bottom = 344; FillRect((HDC)wparam, &rect, ui->panel); } return 1;
        }
    case WM_DESTROY: KillTimer(window, 1); PostQuitMessage(0); return 0;
    }
    return DefWindowProcW(window, message, wparam, lparam);
}
int maage_access_native_run(maage_access *access, int manage_accounts)
{
    maage_access_win32 ui;
    WNDCLASSW klass;
    RECT bounds = {0, 0, 650, 630};
    MSG message;
    int received;
    HWND label;
    memset(&ui, 0, sizeof(ui)); ui.access = access; ui.manage_accounts = manage_accounts; ui.revision = (unsigned)-1;
    ui.background = CreateSolidBrush(RGB(28,28,28)); ui.panel = CreateSolidBrush(RGB(41,41,41));
    ui.font = CreateFontW(-16,0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,0,0,CLEARTYPE_QUALITY,0,L"Segoe UI");
    ui.heading = CreateFontW(-27,0,0,0,FW_SEMIBOLD,FALSE,FALSE,FALSE,DEFAULT_CHARSET,0,0,CLEARTYPE_QUALITY,0,L"Segoe UI");
    memset(&klass, 0, sizeof(klass)); klass.lpfnWndProc = maage_access_win32_proc; klass.hInstance = GetModuleHandleW(NULL); klass.hCursor = LoadCursor(NULL, IDC_ARROW); klass.lpszClassName = L"MaaGEVerifyAccess";
    RegisterClassW(&klass); AdjustWindowRect(&bounds, WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX, FALSE);
    ui.window = CreateWindowExW(WS_EX_CONTROLPARENT, klass.lpszClassName, L"MaaGE Launcher — Initial setup", WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX, CW_USEDEFAULT,CW_USEDEFAULT,bounds.right-bounds.left,bounds.bottom-bounds.top,NULL,NULL,klass.hInstance,&ui);
    if (!ui.window) { ui.result = -1; goto cleanup; }
    maage_access_win32_control(&ui,L"STATIC",L"MaaGE Launcher",0,28,22,260,24,0);
    maage_access_win32_control(&ui,L"STATIC",L"Initial setup · 1 of 2",SS_RIGHT,402,22,220,24,0);
    label = maage_access_win32_control(&ui,L"STATIC",L"✓ Verify Minecraft Access",0,30,76,590,38,106); SendMessageW(label,WM_SETFONT,(WPARAM)ui.heading,TRUE);
    maage_access_win32_control(&ui,L"STATIC",L"Sign in with your Microsoft account to verify access to Minecraft: Java Edition.",0,32,121,584,38,0);
    ui.profiles = maage_access_win32_control(&ui,L"COMBOBOX",L"Minecraft profiles",WS_TABSTOP | WS_VSCROLL | CBS_DROPDOWNLIST,44,180,562,260,105);
    ui.identity = maage_access_win32_control(&ui,L"STATIC",L"",0,48,218,554,82,0);

    ui.stored = maage_access_win32_control(&ui,L"BUTTON",L"Refresh / Reverify",WS_TABSTOP | BS_PUSHBUTTON,48,305,150,30,107);
    ui.disconnect = maage_access_win32_control(&ui,L"BUTTON",L"Remove",WS_TABSTOP | BS_PUSHBUTTON,294,305,126,30,102);
    ui.signout = maage_access_win32_control(&ui,L"BUTTON",L"Sign Out",WS_TABSTOP | BS_PUSHBUTTON,430,305,176,30,111);
    ui.connect = maage_access_win32_control(&ui,L"BUTTON",L"Sign in with Microsoft",WS_TABSTOP | BS_PUSHBUTTON,28,358,594,40,101);
    ui.rescan = maage_access_win32_control(&ui,L"BUTTON",L"Use Device Code",WS_TABSTOP | BS_PUSHBUTTON,28,408,200,30,103);
    ui.stop = maage_access_win32_control(&ui,L"BUTTON",L"Cancel Sign-in",WS_TABSTOP | BS_PUSHBUTTON,436,408,186,30,110);
    ui.device = maage_access_win32_control(&ui,L"STATIC",L"",0,30,448,590,46,0);
    ui.copy = maage_access_win32_control(&ui,L"BUTTON",L"Copy Code",WS_TABSTOP | BS_PUSHBUTTON,28,490,125,30,108);
    ui.open = maage_access_win32_control(&ui,L"BUTTON",L"Open Browser",WS_TABSTOP | BS_PUSHBUTTON,164,490,150,30,109);
    ui.status = maage_access_win32_control(&ui,L"STATIC",L"",0,30,518,590,58,0);
    ui.cancel = maage_access_win32_control(&ui,L"BUTTON",L"Cancel",WS_TABSTOP | BS_PUSHBUTTON,402,586,100,30,IDCANCEL);
    ui.next = maage_access_win32_control(&ui,L"BUTTON",L"Continue",WS_TABSTOP | BS_DEFPUSHBUTTON,512,586,110,30,IDOK);
    EnableWindow(ui.next,FALSE);
    if (!SetTimer(ui.window,1,100,NULL)) { DestroyWindow(ui.window); ui.result = -1; goto cleanup; }
    while ((received = GetMessageW(&message,NULL,0,0)) > 0) if (!IsDialogMessageW(ui.window,&message)) { TranslateMessage(&message); DispatchMessageW(&message); }
    if (received < 0) { DestroyWindow(ui.window); ui.result = -1; }
cleanup:
    maage_access_snapshot_release(&ui.snapshot);
    DeleteObject(ui.font); DeleteObject(ui.heading); DeleteObject(ui.background); DeleteObject(ui.panel); UnregisterClassW(klass.lpszClassName,klass.hInstance); return ui.result;
}
