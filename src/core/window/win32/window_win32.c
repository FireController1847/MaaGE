#define WIN32_LEAN_AND_MEAN
#define UNICODE
#define _UNICODE
#include <windows.h>
#include <stdlib.h>
#include <string.h>
#include "../window_internal.h"

typedef UINT (WINAPI *maage_win32_dpi_fn)(HWND);
typedef BOOL (WINAPI *maage_win32_awareness_fn)(HANDLE);
typedef struct maage_win32_system {
    HINSTANCE instance;
    DWORD thread;
    maage_win32_dpi_fn dpi;
} maage_win32_system;
typedef struct maage_win32_window {
    HWND handle;
    RECT normal_rect;
} maage_win32_window;
static unsigned int maage_win32_users;
static const WCHAR maage_win32_class[] = L"MaaGEWindow";

static LRESULT CALLBACK maage_win32_proc(HWND handle, UINT message, WPARAM wp, LPARAM lp)
{
    maage_window *window;
    RECT *suggested;
    if (message == WM_NCCREATE) {
        window = (maage_window *)((CREATESTRUCTW *)lp)->lpCreateParams;
        SetWindowLongPtrW(handle, GWLP_USERDATA, (LONG_PTR)window);
    } else window = (maage_window *)GetWindowLongPtrW(handle, GWLP_USERDATA);
    if (window) {
        switch (message) {
        case WM_CLOSE: window->state.close_requested = 1; return 0;
        case WM_SIZE: window->state.resized = 1; break;
        case WM_DPICHANGED:
            suggested = (RECT *)lp;
            if (window->state.mode < MAAGE_WINDOW_BORDERLESS_FULLSCREEN)
                SetWindowPos(handle, NULL, suggested->left, suggested->top,
                    suggested->right - suggested->left, suggested->bottom - suggested->top,
                    SWP_NOZORDER | SWP_NOACTIVATE);
            window->state.resized = 1;
            return 0;
        case WM_DESTROY: window->state.close_requested = 1; break;
        }
    }
    return DefWindowProcW(handle, message, wp, lp);
}
static WCHAR *maage_win32_text(const char *utf8)
{
    int count;
    WCHAR *text;
    count = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, utf8, -1, NULL, 0);
    if (!count) return NULL;
    text = (WCHAR *)malloc((size_t)count * sizeof(*text));
    if (text) MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, utf8, -1, text, count);
    return text;
}
static UINT maage_win32_dpi(maage_window *window)
{
    maage_win32_system *system = window->system->platform;
    maage_win32_window *native = window->platform;
    UINT dpi = system->dpi ? system->dpi(native->handle) : 96;
    return dpi ? dpi : 96;
}
static maage_window_result maage_win32_init(maage_window_system *system)
{
    maage_win32_system *native;
    maage_win32_awareness_fn awareness;
    HMODULE user32;
    WNDCLASSW cls;
    native = (maage_win32_system *)calloc(1, sizeof(*native));
    if (!native) return MAAGE_WINDOW_OUT_OF_MEMORY;
    native->instance = GetModuleHandleW(NULL);
    native->thread = GetCurrentThreadId();
    user32 = GetModuleHandleW(L"user32.dll");
    native->dpi = (maage_win32_dpi_fn)GetProcAddress(user32, "GetDpiForWindow");
    awareness = (maage_win32_awareness_fn)GetProcAddress(user32, "SetProcessDpiAwarenessContext");
    if (awareness) awareness((HANDLE)(LONG_PTR)-4);
    memset(&cls, 0, sizeof(cls));
    cls.lpfnWndProc = maage_win32_proc;
    cls.hInstance = native->instance;
    cls.hCursor = LoadCursorW(NULL, IDC_ARROW);
    cls.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    cls.lpszClassName = maage_win32_class;
    if (!maage_win32_users && !RegisterClassW(&cls)) { free(native); return MAAGE_WINDOW_PLATFORM_ERROR; }
    ++maage_win32_users;
    system->platform = native;
    return MAAGE_WINDOW_OK;
}
static void maage_win32_shutdown(maage_window_system *system)
{
    maage_win32_system *native = system->platform;
    if (!native) return;
    if (!--maage_win32_users) UnregisterClassW(maage_win32_class, native->instance);
    free(native);
}
static maage_window_result maage_win32_title(maage_window *window, const char *title)
{
    maage_win32_window *native = window->platform;
    WCHAR *text = maage_win32_text(title);
    BOOL result;
    if (!text) return MAAGE_WINDOW_INVALID_ARGUMENT;
    result = SetWindowTextW(native->handle, text);
    free(text);
    return result ? MAAGE_WINDOW_OK : MAAGE_WINDOW_PLATFORM_ERROR;
}
static maage_window_result maage_win32_visible(maage_window *window, int visible)
{
    maage_win32_window *native = window->platform;
    ShowWindow(native->handle, visible ? SW_SHOW : SW_HIDE);
    window->state.visible = visible != 0;
    return MAAGE_WINDOW_OK;
}
static maage_window_result maage_win32_resize(maage_window *window, int width, int height)
{
    maage_win32_window *native = window->platform;
    RECT frame;
    UINT dpi = maage_win32_dpi(window);
    frame.left = frame.top = 0;
    frame.right = MulDiv(width, dpi, 96);
    frame.bottom = MulDiv(height, dpi, 96);
    if (!AdjustWindowRectEx(&frame, (DWORD)GetWindowLongPtrW(native->handle, GWL_STYLE), FALSE, 0))
        return MAAGE_WINDOW_PLATFORM_ERROR;
    if (!SetWindowPos(native->handle, NULL, 0, 0, frame.right - frame.left,
        frame.bottom - frame.top, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE))
        return MAAGE_WINDOW_PLATFORM_ERROR;
    window->state.resized = 1;
    return MAAGE_WINDOW_OK;
}
static maage_window_result maage_win32_mode(maage_window *window, maage_window_mode mode)
{
    maage_win32_window *native = window->platform;
    MONITORINFO monitor;
    RECT frame;
    LONG_PTR style;
    if (mode == MAAGE_WINDOW_NATIVE_FULLSCREEN) return MAAGE_WINDOW_UNSUPPORTED;
    if (mode == window->state.mode) return MAAGE_WINDOW_OK;
    if (window->state.mode < MAAGE_WINDOW_BORDERLESS_FULLSCREEN)
        GetWindowRect(native->handle, &native->normal_rect);
    style = mode == MAAGE_WINDOW_WINDOWED ? WS_OVERLAPPEDWINDOW : WS_POPUP;
    style |= GetWindowLongPtrW(native->handle, GWL_STYLE) & (WS_VISIBLE | WS_DISABLED);
    frame = native->normal_rect;
    if (mode == MAAGE_WINDOW_BORDERLESS_FULLSCREEN) {
        monitor.cbSize = sizeof(monitor);
        if (!GetMonitorInfoW(MonitorFromWindow(native->handle, MONITOR_DEFAULTTONEAREST), &monitor))
            return MAAGE_WINDOW_PLATFORM_ERROR;
        frame = monitor.rcMonitor;
    }
    SetLastError(0);
    if (!SetWindowLongPtrW(native->handle, GWL_STYLE, style) && GetLastError())
        return MAAGE_WINDOW_PLATFORM_ERROR;
    if (!SetWindowPos(native->handle, NULL, frame.left, frame.top,
        frame.right - frame.left, frame.bottom - frame.top,
        SWP_FRAMECHANGED | SWP_NOZORDER | SWP_NOACTIVATE)) return MAAGE_WINDOW_PLATFORM_ERROR;
    window->state.mode = mode;
    window->state.resized = 1;
    return MAAGE_WINDOW_OK;
}
static maage_window_result maage_win32_create(maage_window *window, const maage_window_config *config)
{
    maage_win32_system *system = window->system->platform;
    maage_win32_window *native;
    maage_window_result result;
    native = (maage_win32_window *)calloc(1, sizeof(*native));
    if (!native) return MAAGE_WINDOW_OUT_OF_MEMORY;
    window->platform = native;
    native->handle = CreateWindowExW(0, maage_win32_class, L"", WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT, config->width, config->height, NULL, NULL,
        system->instance, window);
    if (!native->handle) return MAAGE_WINDOW_PLATFORM_ERROR;
    result = maage_win32_title(window, config->title);
    if (result != MAAGE_WINDOW_OK) return result;
    result = maage_win32_resize(window, config->width, config->height);
    if (result != MAAGE_WINDOW_OK) return result;
    GetWindowRect(native->handle, &native->normal_rect);
    result = maage_win32_mode(window, config->mode);
    if (result != MAAGE_WINDOW_OK) return result;
    return maage_win32_visible(window, config->visible);
}
static void maage_win32_destroy(maage_window *window)
{
    maage_win32_window *native = window->platform;
    if (!native) return;
    if (native->handle) DestroyWindow(native->handle);
    free(native);
}
static maage_window_result maage_win32_poll(maage_window_system *system, int wait)
{
    maage_win32_system *native = system->platform;
    maage_window *window;
    MSG message;
    if (GetCurrentThreadId() != native->thread) return MAAGE_WINDOW_WRONG_THREAD;
    if (wait && !PeekMessageW(&message, NULL, 0, 0, PM_NOREMOVE)) WaitMessage();
    while (PeekMessageW(&message, NULL, 0, 0, PM_REMOVE)) {
        if (message.message == WM_QUIT) {
            for (window = system->windows; window; window = window->next)
                window->state.close_requested = 1;
        } else {
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
    }
    return MAAGE_WINDOW_OK;
}
static maage_window_result maage_win32_state(maage_window *window)
{
    maage_win32_window *native = window->platform;
    RECT rect;
    UINT dpi = maage_win32_dpi(window);
    if (!GetClientRect(native->handle, &rect)) return MAAGE_WINDOW_PLATFORM_ERROR;
    window->state.framebuffer_width = rect.right - rect.left;
    window->state.framebuffer_height = rect.bottom - rect.top;
    window->state.width = MulDiv(window->state.framebuffer_width, 96, dpi);
    window->state.height = MulDiv(window->state.framebuffer_height, 96, dpi);
    window->state.visible = IsWindowVisible(native->handle) != 0;
    return MAAGE_WINDOW_OK;
}
static void maage_win32_native(maage_window *window, maage_window_native *output)
{
    maage_win32_system *system = window->system->platform;
    maage_win32_window *native = window->platform;
    output->handles.win32.instance = system->instance;
    output->handles.win32.window = native->handle;
}
const maage_window_backend_api maage_window_win32_api = {
    MAAGE_WINDOW_WIN32, maage_win32_init, maage_win32_shutdown, maage_win32_create,
    maage_win32_destroy, maage_win32_poll, maage_win32_state, maage_win32_title,
    maage_win32_visible, maage_win32_resize, maage_win32_mode, maage_win32_native
};
