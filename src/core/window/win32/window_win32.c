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
static UINT maage_win32_dpi(maage_window *window);
static unsigned int maage_win32_users;
static const WCHAR maage_win32_class[] = L"MaaGEWindow";
static maage_status maage_win32_capture(maage_window *, int);

static int maage_win32_key(LPARAM code)
{
    switch (((unsigned long)code >> 16) & 255) {
    case 0x11: return MAAGE_INPUT_KEY_W;
    case 0x1e: return MAAGE_INPUT_KEY_A;
    case 0x1f: return MAAGE_INPUT_KEY_S;
    case 0x20: return MAAGE_INPUT_KEY_D;
    case 0x39: return MAAGE_INPUT_KEY_SPACE;
    case 0x01: return MAAGE_INPUT_KEY_ESCAPE;
    case 0x3d: return MAAGE_INPUT_KEY_F3;
    case 0x57: return MAAGE_INPUT_KEY_F11;
    case 0x1c: return MAAGE_INPUT_KEY_ENTER;
    case 0x0f: return MAAGE_INPUT_KEY_TAB;
    case 0x48: return MAAGE_INPUT_KEY_UP;
    case 0x50: return MAAGE_INPUT_KEY_DOWN;
    case 0x4b: return MAAGE_INPUT_KEY_LEFT;
    case 0x4d: return MAAGE_INPUT_KEY_RIGHT;
    case 0x12: return MAAGE_INPUT_KEY_E;
    case 0x02: return MAAGE_INPUT_KEY_1;
    case 0x03: return MAAGE_INPUT_KEY_2;
    case 0x04: return MAAGE_INPUT_KEY_3;
    case 0x05: return MAAGE_INPUT_KEY_4;
    case 0x06: return MAAGE_INPUT_KEY_5;
    case 0x07: return MAAGE_INPUT_KEY_6;
    case 0x08: return MAAGE_INPUT_KEY_7;
    case 0x09: return MAAGE_INPUT_KEY_8;
    case 0x0a: return MAAGE_INPUT_KEY_9;
    case 0x2a: case 0x36: return MAAGE_INPUT_KEY_SHIFT;
    default: return -1;
    }
}

static LRESULT CALLBACK maage_win32_proc(HWND handle, UINT message, WPARAM wp, LPARAM lp)
{
    maage_window *window;
    RECT *suggested;
    RAWINPUT raw;
    UINT raw_size;
    if (message == WM_NCCREATE) {
        window = (maage_window *)((CREATESTRUCTW *)lp)->lpCreateParams;
        SetWindowLongPtrW(handle, GWLP_USERDATA, (LONG_PTR)window);
    } else window = (maage_window *)GetWindowLongPtrW(handle, GWLP_USERDATA);
    if (window) {
        switch (message) {
        case WM_SETFOCUS: maage_window_input_focus(window, 1); break;
        case WM_KILLFOCUS: maage_window_input_focus(window, 0); break;
        case WM_CHAR:
            if (!window->input.captured) maage_window_input_character(window, (unsigned int)wp);
            break;
        case WM_KEYDOWN: case WM_KEYUP:
            maage_window_input_key(window, (maage_input_key)maage_win32_key(lp), message == WM_KEYDOWN);
            break;
        case WM_MOUSEMOVE: case WM_LBUTTONDOWN: case WM_LBUTTONUP:
        case WM_RBUTTONDOWN: case WM_RBUTTONUP:
            window->input.mouse_x = (double)(short)LOWORD(lp) * 96.0 / maage_win32_dpi(window);
            window->input.mouse_y = (double)(short)HIWORD(lp) * 96.0 / maage_win32_dpi(window);
            if (message == WM_LBUTTONDOWN) window->input.primary_pressed = window->input.primary_down = 1;
            if (message == WM_LBUTTONUP) window->input.primary_down = 0;
            if (message == WM_RBUTTONDOWN) window->input.secondary_pressed = window->input.secondary_down = 1;
            if (message == WM_RBUTTONUP) window->input.secondary_down = 0;
            break;
        case WM_MOUSEWHEEL:
            window->input.scroll_y += (double)(short)HIWORD(wp) / WHEEL_DELTA;
            break;
        case WM_INPUT:
            raw_size = sizeof(raw);
            if (window->input.captured && GetRawInputData((HRAWINPUT)lp, RID_INPUT,
                &raw, &raw_size, sizeof(RAWINPUTHEADER)) != (UINT)-1 &&
                raw.header.dwType == RIM_TYPEMOUSE && !(raw.data.mouse.usFlags & MOUSE_MOVE_ABSOLUTE)) {
                window->input.mouse_delta_x += raw.data.mouse.lLastX;
                window->input.mouse_delta_y += raw.data.mouse.lLastY;
            }
            break;
        case WM_CAPTURECHANGED:
            if (window->input.captured && (HWND)lp != handle)
                maage_window_capture_mouse(window, 0);
            break;
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
        case WM_DESTROY:
            maage_window_input_focus(window, 0);
            window->state.close_requested = 1;
            break;
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
static maage_status maage_win32_init(maage_window_system *system)
{
    maage_win32_system *native;
    maage_win32_awareness_fn awareness;
    HMODULE user32;
    WNDCLASSW cls;
    native = (maage_win32_system *)calloc(1, sizeof(*native));
    if (!native) return MAAGE_STATUS_VALUE(MAAGE_STATUS_ALLOCATION_FAILED, MAAGE_CONTEXT_WINDOW, MAAGE_OPERATION_INITIALIZE, 0);
    native->instance = GetModuleHandleW(NULL);
    native->thread = GetCurrentThreadId();
    user32 = GetModuleHandleW(L"user32.dll");
    native->dpi = (maage_win32_dpi_fn)GetProcAddress(user32, "GetDpiForWindow");
    awareness = (maage_win32_awareness_fn)GetProcAddress(user32, "SetProcessDpiAwarenessContext");
    if (awareness) awareness((HANDLE)(LONG_PTR)-4);
    memset(&cls, 0, sizeof(cls));
    cls.lpfnWndProc = maage_win32_proc;
    cls.style = CS_OWNDC;
    cls.hInstance = native->instance;
    cls.hCursor = LoadCursorW(NULL, IDC_ARROW);
    cls.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    cls.lpszClassName = maage_win32_class;
    if (!maage_win32_users && !RegisterClassW(&cls)) { free(native); return MAAGE_STATUS_VALUE(MAAGE_STATUS_ERROR, MAAGE_CONTEXT_WINDOW, MAAGE_OPERATION_INITIALIZE, 0); }
    ++maage_win32_users;
    system->platform = native;
    return MAAGE_STATUS_VALUE(MAAGE_STATUS_SUCCESS, MAAGE_CONTEXT_WINDOW, MAAGE_OPERATION_INITIALIZE, 0);
}
static void maage_win32_shutdown(maage_window_system *system)
{
    maage_win32_system *native = system->platform;
    if (!native) return;
    if (!--maage_win32_users) UnregisterClassW(maage_win32_class, native->instance);
    free(native);
}
static maage_status maage_win32_title(maage_window *window, const char *title)
{
    maage_win32_window *native = window->platform;
    WCHAR *text = maage_win32_text(title);
    BOOL result;
    if (!text) return MAAGE_STATUS_VALUE(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_CONTEXT_WINDOW, MAAGE_OPERATION_SET, 0);
    result = SetWindowTextW(native->handle, text);
    free(text);
    return result ? MAAGE_STATUS_VALUE(MAAGE_STATUS_SUCCESS, MAAGE_CONTEXT_WINDOW, MAAGE_OPERATION_SET, 0) : MAAGE_STATUS_VALUE(MAAGE_STATUS_ERROR, MAAGE_CONTEXT_WINDOW, MAAGE_OPERATION_SET, 0);
}
static maage_status maage_win32_visible(maage_window *window, int visible)
{
    maage_win32_window *native = window->platform;
    ShowWindow(native->handle, visible ? SW_SHOW : SW_HIDE);
    window->state.visible = visible != 0;
    return MAAGE_STATUS_VALUE(MAAGE_STATUS_SUCCESS, MAAGE_CONTEXT_WINDOW, MAAGE_OPERATION_SET, 0);
}
static maage_status maage_win32_resize(maage_window *window, int width, int height)
{
    maage_win32_window *native = window->platform;
    RECT frame;
    UINT dpi = maage_win32_dpi(window);
    frame.left = frame.top = 0;
    frame.right = MulDiv(width, dpi, 96);
    frame.bottom = MulDiv(height, dpi, 96);
    if (!AdjustWindowRectEx(&frame, (DWORD)GetWindowLongPtrW(native->handle, GWL_STYLE), FALSE, 0))
        return MAAGE_STATUS_VALUE(MAAGE_STATUS_ERROR, MAAGE_CONTEXT_WINDOW, MAAGE_OPERATION_RESIZE, 0);
    if (!SetWindowPos(native->handle, NULL, 0, 0, frame.right - frame.left,
        frame.bottom - frame.top, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE))
        return MAAGE_STATUS_VALUE(MAAGE_STATUS_ERROR, MAAGE_CONTEXT_WINDOW, MAAGE_OPERATION_RESIZE, 0);
    window->state.resized = 1;
    return MAAGE_STATUS_VALUE(MAAGE_STATUS_SUCCESS, MAAGE_CONTEXT_WINDOW, MAAGE_OPERATION_RESIZE, 0);
}
static maage_status maage_win32_mode(maage_window *window, maage_window_mode mode)
{
    maage_win32_window *native = window->platform;
    MONITORINFO monitor;
    RECT frame;
    LONG_PTR style;
    if (mode >= MAAGE_WINDOW_NATIVE_FULLSCREEN) return MAAGE_STATUS_VALUE(MAAGE_STATUS_NOT_IMPLEMENTED, MAAGE_CONTEXT_WINDOW, MAAGE_OPERATION_SET, 0);
    if (mode == window->state.mode) return MAAGE_STATUS_VALUE(MAAGE_STATUS_SUCCESS, MAAGE_CONTEXT_WINDOW, MAAGE_OPERATION_SET, 0);
    if (window->state.mode < MAAGE_WINDOW_BORDERLESS_FULLSCREEN)
        GetWindowRect(native->handle, &native->normal_rect);
    style = mode == MAAGE_WINDOW_WINDOWED ? WS_OVERLAPPEDWINDOW : WS_POPUP;
    style |= GetWindowLongPtrW(native->handle, GWL_STYLE) & (WS_VISIBLE | WS_DISABLED);
    frame = native->normal_rect;
    if (mode == MAAGE_WINDOW_BORDERLESS_FULLSCREEN) {
        monitor.cbSize = sizeof(monitor);
        if (!GetMonitorInfoW(MonitorFromWindow(native->handle, MONITOR_DEFAULTTONEAREST), &monitor))
            return MAAGE_STATUS_VALUE(MAAGE_STATUS_ERROR, MAAGE_CONTEXT_WINDOW, MAAGE_OPERATION_SET, 0);
        frame = monitor.rcMonitor;
    }
    SetLastError(0);
    if (!SetWindowLongPtrW(native->handle, GWL_STYLE, style) && GetLastError())
        return MAAGE_STATUS_VALUE(MAAGE_STATUS_ERROR, MAAGE_CONTEXT_WINDOW, MAAGE_OPERATION_SET, 0);
    if (!SetWindowPos(native->handle, NULL, frame.left, frame.top,
        frame.right - frame.left, frame.bottom - frame.top,
        SWP_FRAMECHANGED | SWP_NOZORDER | SWP_NOACTIVATE)) return MAAGE_STATUS_VALUE(MAAGE_STATUS_ERROR, MAAGE_CONTEXT_WINDOW, MAAGE_OPERATION_SET, 0);
    window->state.mode = mode;
    window->state.resized = 1;
    return MAAGE_STATUS_VALUE(MAAGE_STATUS_SUCCESS, MAAGE_CONTEXT_WINDOW, MAAGE_OPERATION_SET, 0);
}
static maage_status maage_win32_create(maage_window *window, const maage_window_config *config)
{
    maage_win32_system *system = window->system->platform;
    maage_win32_window *native;
    maage_status result;
    native = (maage_win32_window *)calloc(1, sizeof(*native));
    if (!native) return MAAGE_STATUS_VALUE(MAAGE_STATUS_ALLOCATION_FAILED, MAAGE_CONTEXT_WINDOW, MAAGE_OPERATION_INITIALIZE, 0);
    window->platform = native;
    native->handle = CreateWindowExW(0, maage_win32_class, L"", WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT, config->width, config->height, NULL, NULL,
        system->instance, window);
    if (!native->handle) return MAAGE_STATUS_VALUE(MAAGE_STATUS_ERROR, MAAGE_CONTEXT_WINDOW, MAAGE_OPERATION_INITIALIZE, 0);
    result = maage_win32_title(window, config->title);
    if (maage_status_code(result) != MAAGE_STATUS_SUCCESS) return result;
    result = maage_win32_resize(window, config->width, config->height);
    if (maage_status_code(result) != MAAGE_STATUS_SUCCESS) return result;
    GetWindowRect(native->handle, &native->normal_rect);
    result = maage_win32_mode(window, config->mode);
    if (maage_status_code(result) != MAAGE_STATUS_SUCCESS) return result;
    return maage_win32_visible(window, config->visible);
}
static void maage_win32_destroy(maage_window *window)
{
    maage_win32_window *native = window->platform;
    if (!native) return;
    maage_window_input_focus(window, 0);
    if (native->handle) DestroyWindow(native->handle);
    free(native);
}
static maage_status maage_win32_capture(maage_window *window, int capture)
{
    maage_win32_system *system = window->system->platform;
    maage_win32_window *native = window->platform;
    RAWINPUTDEVICE device;
    RECT rect;
    POINT corners[2];
    maage_window *other;
    if (GetCurrentThreadId() != system->thread) return MAAGE_STATUS_VALUE(MAAGE_STATUS_INVALID_STATE, MAAGE_CONTEXT_WINDOW, MAAGE_OPERATION_ACQUIRE, MAAGE_DETAIL_WRONG_THREAD);
    if (capture == window->input.captured) return MAAGE_STATUS_VALUE(MAAGE_STATUS_SUCCESS, MAAGE_CONTEXT_WINDOW, MAAGE_OPERATION_ACQUIRE, 0);
    if (capture) {
        if (GetFocus() != native->handle || !GetClientRect(native->handle, &rect))
            return MAAGE_STATUS_VALUE(MAAGE_STATUS_SYSTEM_NOT_SUPPORTED, MAAGE_CONTEXT_WINDOW, MAAGE_OPERATION_ACQUIRE, 0);
        for (other = window->system->windows; other; other = other->next)
            if (other != window && other->input.captured) maage_window_capture_mouse(other, 0);
        corners[0].x = rect.left; corners[0].y = rect.top;
        corners[1].x = rect.right; corners[1].y = rect.bottom;
        if (!ClientToScreen(native->handle, &corners[0]) ||
            !ClientToScreen(native->handle, &corners[1])) return MAAGE_STATUS_VALUE(MAAGE_STATUS_ERROR, MAAGE_CONTEXT_WINDOW, MAAGE_OPERATION_ACQUIRE, 0);
        device.usUsagePage = 1; device.usUsage = 2; device.dwFlags = 0;
        device.hwndTarget = native->handle;
        if (!RegisterRawInputDevices(&device, 1, sizeof(device))) return MAAGE_STATUS_VALUE(MAAGE_STATUS_NOT_IMPLEMENTED, MAAGE_CONTEXT_WINDOW, MAAGE_OPERATION_ACQUIRE, 0);
        rect.left = corners[0].x; rect.top = corners[0].y;
        rect.right = corners[1].x; rect.bottom = corners[1].y;
        if (!ClipCursor(&rect)) {
            device.dwFlags = RIDEV_REMOVE; device.hwndTarget = NULL;
            RegisterRawInputDevices(&device, 1, sizeof(device));
            return MAAGE_STATUS_VALUE(MAAGE_STATUS_ERROR, MAAGE_CONTEXT_WINDOW, MAAGE_OPERATION_ACQUIRE, 0);
        }
        SetCapture(native->handle);
        ShowCursor(FALSE);
    } else {
        /* Clear first: ReleaseCapture synchronously emits WM_CAPTURECHANGED. */
        window->input.captured = 0;
        device.usUsagePage = 1; device.usUsage = 2;
        device.dwFlags = RIDEV_REMOVE; device.hwndTarget = NULL;
        RegisterRawInputDevices(&device, 1, sizeof(device));
        if (GetCapture() == native->handle) ReleaseCapture();
        ClipCursor(NULL);
        ShowCursor(TRUE);
    }
    return MAAGE_STATUS_VALUE(MAAGE_STATUS_SUCCESS, MAAGE_CONTEXT_WINDOW, MAAGE_OPERATION_ACQUIRE, 0);
}
static maage_status maage_win32_poll(maage_window_system *system, int timeout_ms)
{
    maage_win32_system *native = system->platform;
    maage_window *window;
    MSG message;
    if (GetCurrentThreadId() != native->thread) return MAAGE_STATUS_VALUE(MAAGE_STATUS_INVALID_STATE, MAAGE_CONTEXT_WINDOW, MAAGE_OPERATION_POLL, MAAGE_DETAIL_WRONG_THREAD);
    if (timeout_ms && !PeekMessageW(&message, NULL, 0, 0, PM_NOREMOVE)) {
        if (MsgWaitForMultipleObjects(0, NULL, FALSE,
            timeout_ms < 0 ? INFINITE : (DWORD)timeout_ms, QS_ALLINPUT) == WAIT_FAILED)
            return MAAGE_STATUS_VALUE(MAAGE_STATUS_ERROR, MAAGE_CONTEXT_WINDOW, MAAGE_OPERATION_POLL, 0);
    }
    while (PeekMessageW(&message, NULL, 0, 0, PM_REMOVE)) {
        if (message.message == WM_QUIT) {
            for (window = system->windows; window; window = window->next)
                window->state.close_requested = 1;
        } else {
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
    }
    return MAAGE_STATUS_VALUE(MAAGE_STATUS_SUCCESS, MAAGE_CONTEXT_WINDOW, MAAGE_OPERATION_POLL, 0);
}
static maage_status maage_win32_state(maage_window *window)
{
    maage_win32_system *system = window->system->platform;
    maage_win32_window *native = window->platform;
    RECT rect;
    UINT dpi = maage_win32_dpi(window);
    if (GetCurrentThreadId() != system->thread) return MAAGE_STATUS_VALUE(MAAGE_STATUS_INVALID_STATE, MAAGE_CONTEXT_WINDOW, MAAGE_OPERATION_QUERY, MAAGE_DETAIL_WRONG_THREAD);
    if (!GetClientRect(native->handle, &rect)) return MAAGE_STATUS_VALUE(MAAGE_STATUS_ERROR, MAAGE_CONTEXT_WINDOW, MAAGE_OPERATION_QUERY, 0);
    window->state.framebuffer_width = rect.right - rect.left;
    window->state.framebuffer_height = rect.bottom - rect.top;
    window->state.width = MulDiv(window->state.framebuffer_width, 96, dpi);
    window->state.height = MulDiv(window->state.framebuffer_height, 96, dpi);
    window->state.visible = IsWindowVisible(native->handle) != 0;
    return MAAGE_STATUS_VALUE(MAAGE_STATUS_SUCCESS, MAAGE_CONTEXT_WINDOW, MAAGE_OPERATION_QUERY, 0);
}
static void maage_win32_native(maage_window *window, maage_window_native *output)
{
    maage_win32_system *system = window->system->platform;
    maage_win32_window *native = window->platform;
    output->handles.win32.instance = system->instance;
    output->handles.win32.window = native->handle;
}
static maage_status maage_win32_pixels(maage_window *window, const maage_pixel_buffer *buffer)
{
    maage_win32_window *native = window->platform;
    BITMAPINFO info;
    HDC dc;
    unsigned char *bgra;
    size_t index, size = (size_t)buffer->width * 4 * (size_t)buffer->height;
    int row, result;
    bgra = (unsigned char *)malloc(size);
    if (!bgra) return MAAGE_STATUS_VALUE(MAAGE_STATUS_ALLOCATION_FAILED, MAAGE_CONTEXT_WINDOW, MAAGE_OPERATION_PRESENT, 0);
    for (row = 0; row < buffer->height; ++row) {
        const unsigned char *source = buffer->pixels + (size_t)row * buffer->stride;
        unsigned char *target = bgra + (size_t)row * (size_t)buffer->width * 4;
        for (index = 0; index < (size_t)buffer->width * 4; index += 4) {
            target[index] = source[index + 2]; target[index + 1] = source[index + 1];
            target[index + 2] = source[index]; target[index + 3] = 0;
        }
    }
    memset(&info, 0, sizeof(info));
    info.bmiHeader.biSize = sizeof(info.bmiHeader);
    info.bmiHeader.biWidth = buffer->width;
    info.bmiHeader.biHeight = -buffer->height;
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;
    dc = GetDC(native->handle);
    result = dc ? StretchDIBits(dc, 0, 0, buffer->width, buffer->height, 0, 0,
        buffer->width, buffer->height, bgra, &info, DIB_RGB_COLORS, SRCCOPY) : GDI_ERROR;
    if (dc) ReleaseDC(native->handle, dc);
    free(bgra);
    return result == GDI_ERROR || result == 0 ? MAAGE_STATUS_VALUE(MAAGE_STATUS_ERROR, MAAGE_CONTEXT_WINDOW, MAAGE_OPERATION_PRESENT, 0) : MAAGE_STATUS_VALUE(MAAGE_STATUS_SUCCESS, MAAGE_CONTEXT_WINDOW, MAAGE_OPERATION_PRESENT, 0);
}
const maage_window_backend_api maage_window_win32_api = {
    MAAGE_WINDOW_WIN32, maage_win32_init, maage_win32_shutdown, maage_win32_create,
    maage_win32_destroy, maage_win32_poll, maage_win32_state, maage_win32_title,
    maage_win32_visible, maage_win32_resize, maage_win32_mode, maage_win32_native, NULL, maage_win32_pixels,
    maage_win32_capture
};
