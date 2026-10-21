#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <commctrl.h>
#include <uxtheme.h>
#include "setup.h"
#include <string.h>
typedef struct maage_setup_win32 {
    maage_assets *assets;
    HWND window, heading, subtitle, directories, metadata, resources, client, reconstruction, completion, percent, progress, count, detail, next, cancel;
    HFONT font, heading_font;
    HBRUSH background, panel;
    int result, closing;
} maage_setup_win32;
static void maage_setup_win32_text(HWND control, const char *text)
{
    wchar_t wide[1024];
    if (MultiByteToWideChar(CP_UTF8, 0, text, -1, wide, 1024)) SetWindowTextW(control, wide);
}
static HWND maage_setup_win32_control(maage_setup_win32 *ui, const wchar_t *kind, const wchar_t *text, DWORD style, int x, int y, int width, int height, int id)
{
    HWND control = CreateWindowExW(0, kind, text, WS_CHILD | WS_VISIBLE | style, x, y, width, height, ui->window, (HMENU)(INT_PTR)id, GetModuleHandleW(NULL), NULL);
    SendMessageW(control, WM_SETFONT, (WPARAM)ui->font, TRUE); return control;
}
static void maage_setup_win32_tick(maage_setup_win32 *ui)
{
    maage_setup_progress p;
    maage_setup_text t;
    char percent[16];
    maage_assets_snapshot(ui->assets, &p); maage_setup_describe(&p, &t);
    if (p.stage == MAAGE_SETUP_COMPLETE && !maage_assets_access_allowed(ui->assets)) {
        p.stage = MAAGE_SETUP_FAILED; p.needs_setup = 1;
        strcpy(p.error, "Minecraft access expired. Choose Verify Access and verify again. Existing resources are preserved.");
        maage_setup_describe(&p, &t);
    }
    if (ui->closing && (p.stage == MAAGE_SETUP_CANCELLED || p.stage == MAAGE_SETUP_FAILED || p.stage == MAAGE_SETUP_COMPLETE)) { DestroyWindow(ui->window); return; }
    if (t.complete && !p.needs_setup && maage_assets_access_allowed(ui->assets)) { ui->result = 1; DestroyWindow(ui->window); return; }
    maage_setup_win32_text(ui->heading, t.heading); maage_setup_win32_text(ui->subtitle, t.subtitle);
    maage_setup_win32_text(ui->directories, t.directories); maage_setup_win32_text(ui->metadata, t.metadata);
    maage_setup_win32_text(ui->resources, t.resources); maage_setup_win32_text(ui->client, t.client);
    maage_setup_win32_text(ui->reconstruction, t.reconstruction); maage_setup_win32_text(ui->completion, t.completion); maage_setup_win32_text(ui->count, t.count); maage_setup_win32_text(ui->detail, t.detail);
    SetWindowPos(ui->percent, NULL, 512, t.complete ? 328 : p.stage == MAAGE_SETUP_CLIENT_ARCHIVE ? 266 : p.objects_ready ? 297 : 235, 60, 24, SWP_NOZORDER | SWP_NOACTIVATE);
    percent[0] = 0;
    if (p.percent >= 0) {
        wsprintfA(percent, "%d%%", p.percent);
        SetWindowLongPtrW(ui->progress, GWL_STYLE, GetWindowLongPtrW(ui->progress, GWL_STYLE) & ~PBS_MARQUEE);
        SendMessageW(ui->progress, PBM_SETMARQUEE, FALSE, 0); SendMessageW(ui->progress, PBM_SETPOS, p.percent, 0);
    } else {
        SetWindowLongPtrW(ui->progress, GWL_STYLE, GetWindowLongPtrW(ui->progress, GWL_STYLE) | PBS_MARQUEE);
        SendMessageW(ui->progress, PBM_SETMARQUEE, TRUE, 30);
    }
    if (t.failed && !maage_assets_access_allowed(ui->assets)) t.button = "Verify Access";
    maage_setup_win32_text(ui->percent, percent); maage_setup_win32_text(ui->next, t.button); EnableWindow(ui->next, t.actionable && !ui->closing && (!t.complete || maage_assets_access_allowed(ui->assets)));
    if (!IsWindowVisible(ui->window) && (p.needs_setup || t.failed)) { ShowWindow(ui->window, SW_SHOW); UpdateWindow(ui->window); }
}
static LRESULT CALLBACK maage_setup_win32_proc(HWND window, UINT message, WPARAM wparam, LPARAM lparam)
{
    maage_setup_win32 *ui = (maage_setup_win32 *)GetWindowLongPtrW(window, GWLP_USERDATA);
    if (message == WM_NCCREATE) {
        ui = (maage_setup_win32 *)((CREATESTRUCTW *)lparam)->lpCreateParams;
        ui->window = window; SetWindowLongPtrW(window, GWLP_USERDATA, (LONG_PTR)ui);
    }
    if (!ui) return DefWindowProcW(window, message, wparam, lparam);
    switch (message) {
    case WM_CLOSE:
        ui->closing = 1; maage_assets_cancel(ui->assets); EnableWindow(ui->cancel, FALSE); EnableWindow(ui->next, FALSE); return 0;
    case WM_COMMAND:
        if (LOWORD(wparam) == IDCANCEL) SendMessageW(window, WM_CLOSE, 0, 0);
        else if (LOWORD(wparam) == IDOK) {
            maage_setup_progress p;
            maage_assets_snapshot(ui->assets, &p);
            if (!maage_assets_access_allowed(ui->assets) && (p.stage == MAAGE_SETUP_FAILED || p.stage == MAAGE_SETUP_COMPLETE)) { ui->result = 2; DestroyWindow(window); }
            else if (p.stage == MAAGE_SETUP_COMPLETE && maage_assets_access_allowed(ui->assets)) { ui->result = 1; DestroyWindow(window); }
            else if (p.stage == MAAGE_SETUP_FAILED) maage_assets_retry(ui->assets);
        }
        return 0;
    case WM_TIMER: maage_setup_win32_tick(ui); return 0;
    case WM_CTLCOLORSTATIC:
    case WM_CTLCOLORBTN:
        {
            RECT rect;
            HWND control = (HWND)lparam;
            int inset;
            GetWindowRect(control, &rect); MapWindowPoints(NULL, window, (POINT *)&rect, 2);
            inset = rect.top >= 156 && rect.top < 423;
            SetTextColor((HDC)wparam, RGB(225,225,225)); SetBkColor((HDC)wparam, inset ? RGB(41,41,41) : RGB(28,28,28));
            return (LRESULT)(inset ? ui->panel : ui->background);
        }
    case WM_ERASEBKGND:
        {
            RECT rect;
            GetClientRect(window, &rect); FillRect((HDC)wparam, &rect, ui->background);
            rect.left = 28; rect.top = 156; rect.right = 592; rect.bottom = 423;
            FillRect((HDC)wparam, &rect, ui->panel); return 1;
        }
    case WM_DESTROY: KillTimer(window, 1); PostQuitMessage(0); return 0;
    }
    return DefWindowProcW(window, message, wparam, lparam);
}
maage_status maage_setup_native_run(maage_assets *assets, int *action)
{
    maage_setup_win32 ui;
    WNDCLASSW klass;
    RECT bounds = {0,0,620,561};
    MSG message;
    INITCOMMONCONTROLSEX controls;
    int received;
    maage_status outcome = maage_status_make(MAAGE_STATUS_SUCCESS, MAAGE_CONTEXT_LAUNCHER, MAAGE_OPERATION_OPEN, 0);
    if (!action) return maage_status_make(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_CONTEXT_LAUNCHER, MAAGE_OPERATION_OPEN, 0);
    *action = 0;
    memset(&ui, 0, sizeof(ui)); ui.assets = assets;
    controls.dwSize = sizeof(controls); controls.dwICC = ICC_PROGRESS_CLASS; InitCommonControlsEx(&controls);
    ui.background = CreateSolidBrush(RGB(28,28,28)); ui.panel = CreateSolidBrush(RGB(41,41,41));
    ui.font = CreateFontW(-16,0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,0,0,CLEARTYPE_QUALITY,0,L"Segoe UI");
    ui.heading_font = CreateFontW(-27,0,0,0,FW_SEMIBOLD,FALSE,FALSE,FALSE,DEFAULT_CHARSET,0,0,CLEARTYPE_QUALITY,0,L"Segoe UI");
    memset(&klass, 0, sizeof(klass)); klass.lpfnWndProc = maage_setup_win32_proc; klass.hInstance = GetModuleHandleW(NULL);
    klass.hCursor = LoadCursor(NULL, IDC_ARROW); klass.lpszClassName = L"MaaGEInitialSetup";
    RegisterClassW(&klass); AdjustWindowRect(&bounds, WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX, FALSE);
    ui.window = CreateWindowExW(WS_EX_CONTROLPARENT, klass.lpszClassName, L"MaaGE Launcher — Initial setup", WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
        CW_USEDEFAULT,CW_USEDEFAULT,bounds.right-bounds.left,bounds.bottom-bounds.top,NULL,NULL,klass.hInstance,&ui);
    if (!ui.window) { outcome = maage_status_from_win32((unsigned long)GetLastError(), MAAGE_CONTEXT_LAUNCHER, MAAGE_OPERATION_OPEN); goto cleanup; }
    maage_setup_win32_control(&ui,L"STATIC",L"MaaGE Launcher",0,28,22,360,24,0);
    maage_setup_win32_control(&ui,L"STATIC",L"Initial setup",SS_RIGHT,474,22,118,24,0);
    maage_setup_win32_control(&ui,L"STATIC",L"↓",0,34,75,36,42,0);
    ui.heading = maage_setup_win32_control(&ui,L"STATIC",L"Preparing MaaGE",0,78,68,500,36,0);
    SendMessageW(ui.heading,WM_SETFONT,(WPARAM)ui.heading_font,TRUE);
    ui.subtitle = maage_setup_win32_control(&ui,L"STATIC",L"",0,79,108,480,38,0);
    ui.directories = maage_setup_win32_control(&ui,L"STATIC",L"",0,48,173,520,24,0);
    ui.metadata = maage_setup_win32_control(&ui,L"STATIC",L"",0,48,204,520,24,0);
    ui.resources = maage_setup_win32_control(&ui,L"STATIC",L"",0,48,235,462,24,0);
    ui.client = maage_setup_win32_control(&ui,L"STATIC",L"",0,48,266,462,24,0);
    ui.reconstruction = maage_setup_win32_control(&ui,L"STATIC",L"",0,48,297,462,24,0);
    ui.completion = maage_setup_win32_control(&ui,L"STATIC",L"",0,48,328,462,24,0);
    ui.percent = maage_setup_win32_control(&ui,L"STATIC",L"",SS_RIGHT,512,235,60,24,0);
    ui.progress = maage_setup_win32_control(&ui,PROGRESS_CLASSW,L"Resource setup progress",PBS_SMOOTH,50,371,520,8,0);
    SetWindowTheme(ui.progress,L"",L""); SendMessageW(ui.progress,PBM_SETBARCOLOR,0,RGB(222,48,158)); SendMessageW(ui.progress,PBM_SETBKCOLOR,0,RGB(61,61,61));
    ui.count = maage_setup_win32_control(&ui,L"STATIC",L"",0,50,391,520,24,0);
    ui.detail = maage_setup_win32_control(&ui,L"STATIC",L"",0,30,434,560,66,0);
    ui.cancel = maage_setup_win32_control(&ui,L"BUTTON",L"Cancel",WS_TABSTOP | BS_PUSHBUTTON,378,511,100,30,IDCANCEL);
    ui.next = maage_setup_win32_control(&ui,L"BUTTON",L"Continue",WS_TABSTOP | BS_DEFPUSHBUTTON,488,511,104,30,IDOK);
    EnableWindow(ui.next,FALSE); SetFocus(ui.cancel);
    if (!SetTimer(ui.window,1,100,NULL)) { DestroyWindow(ui.window); outcome = maage_status_from_win32((unsigned long)GetLastError(), MAAGE_CONTEXT_LAUNCHER, MAAGE_OPERATION_OPEN); goto cleanup; }
    while ((received = GetMessageW(&message,NULL,0,0)) > 0) {
        if (!IsDialogMessageW(ui.window,&message)) { TranslateMessage(&message); DispatchMessageW(&message); }
    }
    if (received < 0) { DestroyWindow(ui.window); outcome = maage_status_from_win32((unsigned long)GetLastError(), MAAGE_CONTEXT_LAUNCHER, MAAGE_OPERATION_OPEN); }
cleanup:
    DeleteObject(ui.font); DeleteObject(ui.heading_font); DeleteObject(ui.background); DeleteObject(ui.panel);
    UnregisterClassW(klass.lpszClassName,klass.hInstance); *action = ui.result; return outcome;
}
