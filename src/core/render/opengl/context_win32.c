#include "opengl_internal.h"
#include "maage/log.h"
#include <stdlib.h>
#include <string.h>

typedef HGLRC(WINAPI *maage_wgl_create_context)(HDC, HGLRC, const int *);
typedef BOOL(WINAPI *maage_wgl_choose_format)(HDC, const int *, const FLOAT *, UINT, int *, UINT *);
typedef const char *(WINAPI *maage_wgl_extensions)(HDC);
typedef BOOL(WINAPI *maage_wgl_swap_interval)(int);
typedef int(WINAPI *maage_wgl_get_interval)(void);
typedef struct maage_opengl_context {
    HWND window, staging;
    HDC window_dc, dc;
    HGLRC context;
    int selected, registered;
    PIXELFORMATDESCRIPTOR format;
    maage_wgl_swap_interval swap_interval;
    maage_wgl_get_interval get_interval;
} maage_opengl_context;
static const WCHAR maage_wgl_class[] = L"MaaGE.OpenGL.Bootstrap";

static PROC maage_wgl_address(const char *name) {
    PROC address = wglGetProcAddress(name);
    if (!address || address == (PROC) 1 || address == (PROC) 2 || address == (PROC) 3 || address == (PROC) -1) return NULL;
    return address;
}
static maage_status maage_wgl_failure(maage_renderer *renderer, unsigned char operation) {
    DWORD error = GetLastError();
    maage_status result = maage_status_from_win32(error, MAAGE_CONTEXT_RENDER, operation);
    if (!maage_status_failed(result)) result = MAAGE_STATUS_VALUE(MAAGE_STATUS_DEPENDENCY_FAILURE, MAAGE_CONTEXT_RENDER, operation, 0);
    return maage_status_trace_record(renderer->operation_trace, result, MAAGE_CONTEXT_RENDER, operation, __FILE__, __LINE__, (long) error);
}
static int maage_wgl_format(HDC dc, int selected, PIXELFORMATDESCRIPTOR *format) {
    return DescribePixelFormat(dc, selected, sizeof(*format), format) &&
           (format->dwFlags & (PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER)) == (PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER) &&
           format->iPixelType == PFD_TYPE_RGBA && format->cRedBits >= 8 && format->cGreenBits >= 8 && format->cBlueBits >= 8;
}
static void maage_wgl_release_staging(maage_opengl_context *native) {
    if (native->staging) {
        if (native->dc) ReleaseDC(native->staging, native->dc);
        DestroyWindow(native->staging);
        native->staging = NULL;
        native->dc = native->window_dc;
    }
    if (native->registered) {
        UnregisterClassW(maage_wgl_class, GetModuleHandleW(NULL));
        native->registered = 0;
    }
}
maage_status maage_opengl_context_create(maage_renderer *renderer, int modern) {
    const int context_attributes[] = {0x2091, 3, 0x2092, 3, 0x9126, 1, 0};
    /* Window/OpenGL/double-buffer/RGBA/color attributes. Depth, stencil and
     * alpha live in our FBOs; do not require them in a virtualized window format. */
    const int pixel_attributes[] = {0x2001, 1, 0x2010, 1, 0x2011, 1, 0x2013, 0x202B, 0x2014, 24, 0};
    maage_opengl_renderer *owner = renderer->platform;
    maage_opengl_context *native;
    PIXELFORMATDESCRIPTOR format;
    WNDCLASSW cls;
    RECT bounds;
    HWND bootstrap = NULL;
    HDC bootstrap_dc = NULL;
    HGLRC bootstrap_context = NULL;
    maage_wgl_create_context create = NULL;
    maage_wgl_choose_format choose;
    maage_wgl_extensions extensions;
    const char *supported;
    HINSTANCE instance = GetModuleHandleW(NULL);
    maage_status result;
    int selected;
    UINT count = 0;
    if (renderer->native.backend != MAAGE_WINDOW_WIN32) return MAAGE_STATUS_VALUE(MAAGE_STATUS_SYSTEM_NOT_SUPPORTED, MAAGE_CONTEXT_RENDER, MAAGE_OPERATION_INITIALIZE, 0);
    native = (maage_opengl_context *) calloc(1, sizeof(*native));
    if (!native) return MAAGE_STATUS_VALUE(MAAGE_STATUS_ALLOCATION_FAILED, MAAGE_CONTEXT_RENDER, MAAGE_OPERATION_INITIALIZE, 0);
    owner->context = native;
    native->window = renderer->native.handles.win32.window;
    native->window_dc = GetDC(native->window);
    if (!native->window_dc || !GetWindowRect(native->window, &bounds)) return maage_wgl_failure(renderer, MAAGE_OPERATION_INITIALIZE);
    memset(&format, 0, sizeof(format));
    format.nSize = sizeof(format);
    format.nVersion = 1;
    format.dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER;
    format.iPixelType = PFD_TYPE_RGBA;
    format.cColorBits = 24;
    format.cAlphaBits = 8;
    format.cDepthBits = 24;
    format.cStencilBits = 8;
    result = MAAGE_STATUS_VALUE(MAAGE_STATUS_SYSTEM_NOT_SUPPORTED, MAAGE_CONTEXT_RENDER, MAAGE_OPERATION_INITIALIZE, 0);
    selected = GetPixelFormat(native->window_dc);
    memset(&cls, 0, sizeof(cls));
    cls.style = CS_OWNDC;
    cls.lpfnWndProc = DefWindowProcW;
    cls.hInstance = instance;
    cls.lpszClassName = maage_wgl_class;
    native->registered = RegisterClassW(&cls) != 0;
    if (!native->registered && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) goto failed;
    if (modern) {
        /* Match the real window's adapter. This first disposable context only
         * discovers WGL; it never assigns a pixel format to the real window. */
        bootstrap = CreateWindowExW(0, maage_wgl_class, L"", WS_POPUP, bounds.left, bounds.top, 1, 1, NULL, NULL, instance, NULL);
        if (!bootstrap) goto failed;
        bootstrap_dc = GetDC(bootstrap);
        if (!bootstrap_dc) goto failed;
        count = (UINT) ChoosePixelFormat(bootstrap_dc, &format);
        if (!count || !SetPixelFormat(bootstrap_dc, (int) count, &format)) goto failed;
        bootstrap_context = wglCreateContext(bootstrap_dc);
        if (!bootstrap_context || !wglMakeCurrent(bootstrap_dc, bootstrap_context)) goto failed;
        extensions = (maage_wgl_extensions) maage_wgl_address("wglGetExtensionsStringARB");
        supported = extensions ? extensions(bootstrap_dc) : NULL;
        create = (maage_wgl_create_context) maage_wgl_address("wglCreateContextAttribsARB");
        if (!create || !maage_opengl_extension(supported, "WGL_ARB_create_context") || !maage_opengl_extension(supported, "WGL_ARB_create_context_profile")) {
            maage_log_warning("maage.render.opengl", "WGL_ARB_create_context/profile or its entry point is unavailable.");
            goto cleanup;
        }
        if (!selected) {
            choose = (maage_wgl_choose_format) maage_wgl_address("wglChoosePixelFormatARB");
            if (choose && maage_opengl_extension(supported, "WGL_ARB_pixel_format")) {
                count = 0;
                if (!choose(native->window_dc, pixel_attributes, NULL, 1, &selected, &count) || !count) goto failed;
            } else selected = ChoosePixelFormat(native->window_dc, &format);
        }
    } else if (!selected) selected = ChoosePixelFormat(native->window_dc, &format);
    if (!selected || !maage_wgl_format(native->window_dc, selected, &native->format)) {
        maage_log_warning("maage.render.opengl", "The window has no compatible double-buffered RGB8 WGL pixel format.");
        goto cleanup;
    }
    native->selected = selected;
    /* Validate the entire renderer on the chosen format before its one-time
     * commit to the real drawable. Failed shaders/FBOs leave later fallback's
     * window format untouched. GL resources remain in this same context. */
    native->staging = CreateWindowExW(0, maage_wgl_class, L"", WS_POPUP, bounds.left, bounds.top, 1, 1, NULL, NULL, instance, NULL);
    if (!native->staging) goto failed;
    native->dc = GetDC(native->staging);
    if (!native->dc || !SetPixelFormat(native->dc, native->selected, &native->format)) goto failed;
    native->context = modern ? create(native->dc, NULL, context_attributes) : wglCreateContext(native->dc);
    if (!native->context || !wglMakeCurrent(native->dc, native->context)) goto failed;
    extensions = (maage_wgl_extensions) maage_wgl_address("wglGetExtensionsStringARB");
    supported = extensions ? extensions(native->dc) : NULL;
    if (maage_opengl_extension(supported, "WGL_EXT_swap_control")) {
        native->swap_interval = (maage_wgl_swap_interval) maage_wgl_address("wglSwapIntervalEXT");
        native->get_interval = (maage_wgl_get_interval) maage_wgl_address("wglGetSwapIntervalEXT");
    }
    result = MAAGE_STATUS_VALUE(MAAGE_STATUS_SUCCESS, 0, MAAGE_OPERATION_INITIALIZE, 0);
    goto cleanup;
failed:
    result = maage_wgl_failure(renderer, MAAGE_OPERATION_INITIALIZE);
cleanup:
    if (wglGetCurrentContext() == bootstrap_context && bootstrap_context) wglMakeCurrent(NULL, NULL);
    if (bootstrap_context) wglDeleteContext(bootstrap_context);
    if (bootstrap_dc) ReleaseDC(bootstrap, bootstrap_dc);
    if (bootstrap) DestroyWindow(bootstrap);
    return result;
}
maage_status maage_opengl_context_attach(maage_renderer *renderer) {
    maage_opengl_context *native = ((maage_opengl_renderer *) renderer->platform)->context;
    int selected = GetPixelFormat(native->window_dc);
    if (!native->staging) return MAAGE_STATUS_VALUE(MAAGE_STATUS_NOOP, 0, MAAGE_OPERATION_INITIALIZE, 0);
    if (selected && selected != native->selected) return MAAGE_STATUS_VALUE(MAAGE_STATUS_SYSTEM_NOT_SUPPORTED, MAAGE_CONTEXT_RENDER, MAAGE_OPERATION_INITIALIZE, 0);
    if (!selected && !SetPixelFormat(native->window_dc, native->selected, &native->format)) return maage_wgl_failure(renderer, MAAGE_OPERATION_INITIALIZE);
    if (!wglMakeCurrent(native->window_dc, native->context)) return maage_wgl_failure(renderer, MAAGE_OPERATION_INITIALIZE);
    maage_wgl_release_staging(native);
    return MAAGE_STATUS_VALUE(MAAGE_STATUS_SUCCESS, 0, MAAGE_OPERATION_INITIALIZE, 0);
}
void maage_opengl_context_destroy(maage_renderer *renderer) {
    maage_opengl_renderer *owner = renderer->platform;
    maage_opengl_context *native = owner->context;
    if (!native) return;
    if (wglGetCurrentContext() == native->context) wglMakeCurrent(NULL, NULL);
    if (native->context) wglDeleteContext(native->context);
    maage_wgl_release_staging(native);
    if (native->window_dc) ReleaseDC(native->window, native->window_dc);
    free(native);
    owner->context = NULL;
}
maage_status maage_opengl_context_bind(maage_renderer *renderer) {
    maage_opengl_context *native = ((maage_opengl_renderer *) renderer->platform)->context;
    if (!native || !native->context || !native->dc) return MAAGE_STATUS_VALUE(MAAGE_STATUS_INVALID_STATE, MAAGE_CONTEXT_RENDER, MAAGE_OPERATION_ACQUIRE, 0);
    if (wglGetCurrentContext() == native->context && wglGetCurrentDC() == native->dc) return MAAGE_STATUS_VALUE(MAAGE_STATUS_SUCCESS, 0, MAAGE_OPERATION_ACQUIRE, 0);
    return wglMakeCurrent(native->dc, native->context) ? MAAGE_STATUS_VALUE(MAAGE_STATUS_SUCCESS, 0, MAAGE_OPERATION_ACQUIRE, 0) : maage_wgl_failure(renderer, MAAGE_OPERATION_ACQUIRE);
}
maage_status maage_opengl_context_swap(maage_renderer *renderer) {
    maage_opengl_context *native = ((maage_opengl_renderer *) renderer->platform)->context;
    if (native->staging) return MAAGE_STATUS_VALUE(MAAGE_STATUS_INVALID_STATE, MAAGE_CONTEXT_RENDER, MAAGE_OPERATION_PRESENT, 0);
    return SwapBuffers(native->dc) ? MAAGE_STATUS_VALUE(MAAGE_STATUS_SUCCESS, 0, MAAGE_OPERATION_PRESENT, 0) : maage_wgl_failure(renderer, MAAGE_OPERATION_PRESENT);
}
maage_opengl_proc maage_opengl_context_proc(maage_renderer *renderer, const char *name) {
    PROC address = maage_wgl_address(name);
    HMODULE library;
    (void) renderer;
    if (!address) {
        library = GetModuleHandleW(L"opengl32.dll");
        if (library) address = GetProcAddress(library, name);
    }
    return (maage_opengl_proc) address;
}
maage_status maage_opengl_context_vsync(maage_renderer *renderer, int enabled) {
    maage_opengl_context *native = ((maage_opengl_renderer *) renderer->platform)->context;
    maage_status result = maage_opengl_context_bind(renderer);
    if (maage_status_failed(result)) return result;
    if (!native->swap_interval || !native->get_interval) return MAAGE_STATUS_VALUE(MAAGE_STATUS_NOT_IMPLEMENTED, MAAGE_CONTEXT_RENDER, MAAGE_OPERATION_SET, 0);
    if (!native->swap_interval(enabled)) return maage_wgl_failure(renderer, MAAGE_OPERATION_SET);
    return maage_status_make(native->get_interval() == enabled ? MAAGE_STATUS_SUCCESS : MAAGE_STATUS_SYSTEM_NOT_SUPPORTED, MAAGE_CONTEXT_RENDER, MAAGE_OPERATION_SET, 0);
}
