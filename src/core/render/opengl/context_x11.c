#include "opengl_internal.h"
#include <GL/glx.h>
#include <GL/glxext.h>
#include <X11/Xutil.h>
#include <stdlib.h>
#include <string.h>

typedef struct maage_opengl_context {
    Display *display;
    Window window;
    GLXContext context;
} maage_opengl_context;
/* Window/renderer calls are restricted to their initialization thread. Bracket
 * context creation so unsupported GLX requests cannot reach Xlib's fatal handler. */
static int maage_glx_error;
static int maage_glx_error_handler(Display *display, XErrorEvent *error)
{
    (void)display; (void)error;
    maage_glx_error = 1;
    return 0;
}
maage_render_result maage_opengl_context_create(maage_renderer *renderer, int modern)
{
    maage_opengl_context *native;
    XWindowAttributes attributes;
    XVisualInfo query, *visual;
    GLXFBConfig *configs;
    PFNGLXCREATECONTEXTATTRIBSARBPROC create;
    const char *extensions;
    int (*previous)(Display *, XErrorEvent *);
    int count, index, id;
    const int requested[] = { GLX_CONTEXT_MAJOR_VERSION_ARB, 3, GLX_CONTEXT_MINOR_VERSION_ARB, 3,
        GLX_CONTEXT_PROFILE_MASK_ARB, GLX_CONTEXT_CORE_PROFILE_BIT_ARB, None };
    if (renderer->native.backend != MAAGE_WINDOW_X11) return MAAGE_RENDER_UNSUPPORTED;
    native = (maage_opengl_context *)calloc(1, sizeof(*native));
    if (!native) return MAAGE_RENDER_OUT_OF_MEMORY;
    renderer->platform = native;
    native->display = renderer->native.handles.x11.display;
    native->window = renderer->native.handles.x11.window;
    if (!XGetWindowAttributes(native->display, native->window, &attributes)) return MAAGE_RENDER_WINDOW_ERROR;
    query.visualid = XVisualIDFromVisual(attributes.visual);
    visual = XGetVisualInfo(native->display, VisualIDMask, &query, &count);
    if (!visual) return MAAGE_RENDER_UNSUPPORTED;
    XSync(native->display, False);
    maage_glx_error = 0;
    previous = XSetErrorHandler(maage_glx_error_handler);
    if (modern) {
        extensions = glXQueryExtensionsString(native->display, visual->screen);
        create = (PFNGLXCREATECONTEXTATTRIBSARBPROC)glXGetProcAddressARB((const GLubyte *)"glXCreateContextAttribsARB");
        if (extensions && strstr(extensions, "GLX_ARB_create_context_profile") && create) {
            configs = glXGetFBConfigs(native->display, visual->screen, &count);
            for (index = 0; configs && index < count; ++index) {
                glXGetFBConfigAttrib(native->display, configs[index], GLX_VISUAL_ID, &id);
                if ((VisualID)id == query.visualid) {
                    glXGetFBConfigAttrib(native->display, configs[index], GLX_DOUBLEBUFFER, &id);
                    if (id) { native->context = create(native->display, configs[index], NULL, True, requested); break; }
                }
            }
            if (configs) XFree(configs);
        }
    } else {
        if (glXGetConfig(native->display, visual, GLX_DOUBLEBUFFER, &id) == 0 && id)
            native->context = glXCreateContext(native->display, visual, NULL, True);
    }
    XSync(native->display, False);
    if (maage_glx_error && native->context) {
        glXDestroyContext(native->display, native->context);
        native->context = NULL;
        XSync(native->display, False);
    }
    XSetErrorHandler(previous);
    XFree(visual);
    return native->context && !maage_glx_error ? MAAGE_RENDER_OK : MAAGE_RENDER_UNAVAILABLE;
}
void maage_opengl_context_destroy(maage_renderer *renderer)
{
    maage_opengl_context *native = renderer->platform;
    if (!native) return;
    if (glXGetCurrentContext() == native->context) glXMakeCurrent(native->display, None, NULL);
    if (native->context) glXDestroyContext(native->display, native->context);
    free(native);
}
int maage_opengl_context_bind(maage_renderer *renderer)
{
    maage_opengl_context *native = renderer->platform;
    return glXMakeCurrent(native->display, native->window, native->context) != 0;
}
int maage_opengl_context_swap(maage_renderer *renderer)
{
    maage_opengl_context *native = renderer->platform;
    glXSwapBuffers(native->display, native->window);
    return 1;
}
