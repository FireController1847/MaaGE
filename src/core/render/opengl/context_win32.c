#include "opengl_internal.h"
#include <stdlib.h>
#include <string.h>

/* WGL extension tokens/signature are confined here; legacy never calls it. */
typedef HGLRC (WINAPI *maage_wgl_create_context)(HDC, HGLRC, const int *);
typedef struct maage_opengl_context {
    HWND window;
    HDC dc;
    HGLRC context;
} maage_opengl_context;

maage_render_result maage_opengl_context_create(maage_renderer *renderer, int modern)
{
    maage_opengl_context *native;
    PIXELFORMATDESCRIPTOR format;
    maage_wgl_create_context create;
    PROC address;
    HGLRC context;
    int selected;
    const int attributes[] = { 0x2091, 3, 0x2092, 3, 0x9126, 1, 0 };
    if (renderer->native.backend != MAAGE_WINDOW_WIN32) return MAAGE_RENDER_UNSUPPORTED;
    native = (maage_opengl_context *)calloc(1, sizeof(*native));
    if (!native) return MAAGE_RENDER_OUT_OF_MEMORY;
    renderer->platform = native;
    native->window = renderer->native.handles.win32.window;
    native->dc = GetDC(native->window);
    if (!native->dc) return MAAGE_RENDER_WINDOW_ERROR;
    memset(&format, 0, sizeof(format));
    format.nSize = sizeof(format); format.nVersion = 1;
    format.dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER;
    format.iPixelType = PFD_TYPE_RGBA;
    format.cColorBits = 24; format.cAlphaBits = 8; format.cDepthBits = 24; format.cStencilBits = 8;
    selected = GetPixelFormat(native->dc);
    if (!selected) {
        selected = ChoosePixelFormat(native->dc, &format);
        if (!selected || !SetPixelFormat(native->dc, selected, &format)) return MAAGE_RENDER_UNAVAILABLE;
    }
    if (!DescribePixelFormat(native->dc, selected, sizeof(format), &format) ||
        (format.dwFlags & (PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER)) != (PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER))
        return MAAGE_RENDER_UNSUPPORTED;
    native->context = wglCreateContext(native->dc);
    if (!native->context || !wglMakeCurrent(native->dc, native->context)) return MAAGE_RENDER_UNAVAILABLE;
    if (modern) {
        address = wglGetProcAddress("wglCreateContextAttribsARB");
        if (!address || address == (PROC)1 || address == (PROC)2 || address == (PROC)3 || address == (PROC)-1)
            return MAAGE_RENDER_UNSUPPORTED;
        create = (maage_wgl_create_context)address;
        context = create(native->dc, NULL, attributes);
        if (!context) return MAAGE_RENDER_UNAVAILABLE;
        wglMakeCurrent(NULL, NULL);
        wglDeleteContext(native->context);
        native->context = context;
    }
    return MAAGE_RENDER_OK;
}
void maage_opengl_context_destroy(maage_renderer *renderer)
{
    maage_opengl_context *native = renderer->platform;
    if (!native) return;
    if (wglGetCurrentContext() == native->context) wglMakeCurrent(NULL, NULL);
    if (native->context) wglDeleteContext(native->context);
    if (native->dc) ReleaseDC(native->window, native->dc);
    free(native);
}
int maage_opengl_context_bind(maage_renderer *renderer)
{
    maage_opengl_context *native = renderer->platform;
    return wglMakeCurrent(native->dc, native->context) != 0;
}
int maage_opengl_context_swap(maage_renderer *renderer)
{
    maage_opengl_context *native = renderer->platform;
    return SwapBuffers(native->dc) != 0;
}
