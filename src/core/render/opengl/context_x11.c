#include "opengl_internal.h"
#include "maage/log.h"
#include <GL/glx.h>
#include <GL/glxext.h>
#include <X11/Xutil.h>
#include <stdlib.h>

typedef struct maage_opengl_context {
    Display *display;
    Window window;
    GLXContext context;
    PFNGLXSWAPINTERVALEXTPROC swap_ext;
    PFNGLXSWAPINTERVALMESAPROC swap_mesa;
    PFNGLXGETSWAPINTERVALMESAPROC get_mesa;
    PFNGLXSWAPINTERVALSGIPROC swap_sgi;
} maage_opengl_context;
/* Xlib's handler is process-wide. Window/context operations run on the owning
 * initialization thread; no worker performs an X request within this bracket. */
static int maage_glx_error;
static int maage_glx_error_handler(Display *display, XErrorEvent *error) {
    (void) display;
    maage_glx_error = error->error_code;
    return 0;
}
static maage_status maage_glx_result(maage_renderer *renderer, int success, unsigned char operation) {
    maage_status result = maage_status_make(success && !maage_glx_error ? MAAGE_STATUS_SUCCESS : MAAGE_STATUS_SYSTEM_NOT_SUPPORTED, MAAGE_CONTEXT_RENDER, operation, 0);
    if (maage_glx_error) maage_status_trace_record(renderer->operation_trace, result, MAAGE_CONTEXT_RENDER, operation, __FILE__, __LINE__, (long) maage_glx_error);
    return result;
}
maage_status maage_opengl_context_create(maage_renderer *renderer, int modern) {
    maage_opengl_renderer *owner = renderer->platform;
    maage_opengl_context *native;
    XWindowAttributes attributes;
    XVisualInfo query, *visual;
    GLXFBConfig *configs;
    PFNGLXCREATECONTEXTATTRIBSARBPROC create;
    const char *extensions;
    int (*previous)(Display *, XErrorEvent *);
    int count, index, id, flags, red, green, blue, major, minor, last_error = 0;
    const int requested[] = {GLX_CONTEXT_MAJOR_VERSION_ARB, 3, GLX_CONTEXT_MINOR_VERSION_ARB, 3, GLX_CONTEXT_PROFILE_MASK_ARB, GLX_CONTEXT_CORE_PROFILE_BIT_ARB, None};
    if (renderer->native.backend != MAAGE_WINDOW_X11) return MAAGE_STATUS_VALUE(MAAGE_STATUS_SYSTEM_NOT_SUPPORTED, MAAGE_CONTEXT_RENDER, MAAGE_OPERATION_INITIALIZE, 0);
    native = (maage_opengl_context *) calloc(1, sizeof(*native));
    if (!native) return MAAGE_STATUS_VALUE(MAAGE_STATUS_ALLOCATION_FAILED, MAAGE_CONTEXT_RENDER, MAAGE_OPERATION_INITIALIZE, 0);
    owner->context = native;
    native->display = renderer->native.handles.x11.display;
    native->window = renderer->native.handles.x11.window;
    if (!glXQueryVersion(native->display, &major, &minor) || (modern && (major < 1 || (major == 1 && minor < 3)))) return MAAGE_STATUS_VALUE(MAAGE_STATUS_SYSTEM_NOT_SUPPORTED, MAAGE_CONTEXT_RENDER, MAAGE_OPERATION_INITIALIZE, 0);
    if (!XGetWindowAttributes(native->display, native->window, &attributes)) return MAAGE_STATUS_VALUE(MAAGE_STATUS_INVALID_STATE, MAAGE_CONTEXT_RENDER, MAAGE_OPERATION_INITIALIZE, 0);
    query.visualid = XVisualIDFromVisual(attributes.visual);
    visual = XGetVisualInfo(native->display, VisualIDMask, &query, &count);
    if (!visual) return MAAGE_STATUS_VALUE(MAAGE_STATUS_SYSTEM_NOT_SUPPORTED, MAAGE_CONTEXT_RENDER, MAAGE_OPERATION_INITIALIZE, 0);
    extensions = glXQueryExtensionsString(native->display, visual->screen);
    XSync(native->display, False);
    maage_glx_error = 0;
    previous = XSetErrorHandler(maage_glx_error_handler);
    if (modern) {
        create = (PFNGLXCREATECONTEXTATTRIBSARBPROC) glXGetProcAddressARB((const GLubyte *) "glXCreateContextAttribsARB");
        if (maage_opengl_extension(extensions, "GLX_ARB_create_context") && maage_opengl_extension(extensions, "GLX_ARB_create_context_profile") && create) {
            configs = glXGetFBConfigs(native->display, visual->screen, &count);
            for (index = 0; configs && index < count; ++index) {
                glXGetFBConfigAttrib(native->display, configs[index], GLX_VISUAL_ID, &id);
                if ((VisualID) id != query.visualid) continue;
                glXGetFBConfigAttrib(native->display, configs[index], GLX_DRAWABLE_TYPE, &flags);
                if (!(flags & GLX_WINDOW_BIT)) continue;
                glXGetFBConfigAttrib(native->display, configs[index], GLX_RENDER_TYPE, &flags);
                if (!(flags & GLX_RGBA_BIT)) continue;
                glXGetFBConfigAttrib(native->display, configs[index], GLX_DOUBLEBUFFER, &id);
                glXGetFBConfigAttrib(native->display, configs[index], GLX_RED_SIZE, &red);
                glXGetFBConfigAttrib(native->display, configs[index], GLX_GREEN_SIZE, &green);
                glXGetFBConfigAttrib(native->display, configs[index], GLX_BLUE_SIZE, &blue);
                if (!id || red < 8 || green < 8 || blue < 8) continue;
                native->context = create(native->display, configs[index], NULL, True, requested);
                XSync(native->display, False);
                if (native->context && !maage_glx_error) break;
                if (maage_glx_error) last_error = maage_glx_error;
                if (native->context) glXDestroyContext(native->display, native->context);
                XSync(native->display, False);
                native->context = NULL;
                maage_glx_error = 0;
            }
            if (configs) XFree(configs);
        } else maage_log_warning("maage.render.opengl", "GLX_ARB_create_context/profile or its entry point is unavailable.");
    } else if (glXGetConfig(native->display, visual, GLX_DOUBLEBUFFER, &id) == 0 && id) native->context = glXCreateContext(native->display, visual, NULL, True);
    XSync(native->display, False);
    if (maage_glx_error && native->context) {
        last_error = maage_glx_error;
        glXDestroyContext(native->display, native->context);
        native->context = NULL;
        XSync(native->display, False);
        maage_glx_error = last_error;
    }
    if (!native->context && !maage_glx_error) maage_glx_error = last_error;
    XSetErrorHandler(previous);
    XFree(visual);
    if (!native->context || maage_glx_error) {
        maage_log_warning("maage.render.opengl", "No compatible %s GLX context is available for the window's double-buffered RGB visual.", modern ? "3.3 Core" : "legacy");
        return maage_glx_result(renderer, 0, MAAGE_OPERATION_INITIALIZE);
    }
    if (maage_opengl_extension(extensions, "GLX_EXT_swap_control")) native->swap_ext = (PFNGLXSWAPINTERVALEXTPROC) glXGetProcAddressARB((const GLubyte *) "glXSwapIntervalEXT");
    if (maage_opengl_extension(extensions, "GLX_MESA_swap_control")) {
        native->swap_mesa = (PFNGLXSWAPINTERVALMESAPROC) glXGetProcAddressARB((const GLubyte *) "glXSwapIntervalMESA");
        native->get_mesa = (PFNGLXGETSWAPINTERVALMESAPROC) glXGetProcAddressARB((const GLubyte *) "glXGetSwapIntervalMESA");
    }
    if (maage_opengl_extension(extensions, "GLX_SGI_swap_control")) native->swap_sgi = (PFNGLXSWAPINTERVALSGIPROC) glXGetProcAddressARB((const GLubyte *) "glXSwapIntervalSGI");
    return MAAGE_STATUS_VALUE(MAAGE_STATUS_SUCCESS, 0, MAAGE_OPERATION_INITIALIZE, 0);
}
maage_status maage_opengl_context_attach(maage_renderer *renderer) {
    return maage_opengl_context_bind(renderer);
}
void maage_opengl_context_destroy(maage_renderer *renderer) {
    maage_opengl_renderer *owner = renderer->platform;
    maage_opengl_context *native = owner->context;
    if (!native) return;
    if (glXGetCurrentContext() == native->context) glXMakeCurrent(native->display, None, NULL);
    if (native->context) glXDestroyContext(native->display, native->context);
    free(native);
    owner->context = NULL;
}
maage_status maage_opengl_context_bind(maage_renderer *renderer) {
    maage_opengl_context *native = ((maage_opengl_renderer *) renderer->platform)->context;
    int success;
    int (*previous)(Display *, XErrorEvent *);
    if (!native || !native->context) return MAAGE_STATUS_VALUE(MAAGE_STATUS_INVALID_STATE, MAAGE_CONTEXT_RENDER, MAAGE_OPERATION_ACQUIRE, 0);
    if (glXGetCurrentContext() == native->context && glXGetCurrentDrawable() == native->window) return MAAGE_STATUS_VALUE(MAAGE_STATUS_SUCCESS, 0, MAAGE_OPERATION_ACQUIRE, 0);
    XSync(native->display, False);
    maage_glx_error = 0;
    previous = XSetErrorHandler(maage_glx_error_handler);
    success = glXMakeCurrent(native->display, native->window, native->context);
    XSync(native->display, False);
    XSetErrorHandler(previous);
    return maage_glx_result(renderer, success, MAAGE_OPERATION_ACQUIRE);
}
maage_status maage_opengl_context_swap(maage_renderer *renderer) {
    maage_opengl_context *native = ((maage_opengl_renderer *) renderer->platform)->context;
    glXSwapBuffers(native->display, native->window);
    return maage_status_make(MAAGE_STATUS_SUCCESS, 0, MAAGE_OPERATION_PRESENT, 0);
}
maage_opengl_proc maage_opengl_context_proc(maage_renderer *renderer, const char *name) {
    (void) renderer;
    return (maage_opengl_proc) glXGetProcAddressARB((const GLubyte *) name);
}
maage_status maage_opengl_context_vsync(maage_renderer *renderer, int enabled) {
    maage_opengl_context *native = ((maage_opengl_renderer *) renderer->platform)->context;
    maage_status result = maage_opengl_context_bind(renderer);
    int (*previous)(Display *, XErrorEvent *);
    unsigned int actual = 0, maximum = 0;
    int success = 0;
    if (maage_status_failed(result)) return result;
    if (!native->swap_ext && !(native->swap_mesa && native->get_mesa) && !(enabled && native->swap_sgi)) return MAAGE_STATUS_VALUE(MAAGE_STATUS_NOT_IMPLEMENTED, MAAGE_CONTEXT_RENDER, MAAGE_OPERATION_SET, 0);
    XSync(native->display, False);
    maage_glx_error = 0;
    previous = XSetErrorHandler(maage_glx_error_handler);
    if (native->swap_ext) {
        glXQueryDrawable(native->display, native->window, GLX_MAX_SWAP_INTERVAL_EXT, &maximum);
        if ((unsigned int) enabled <= maximum) {
            native->swap_ext(native->display, native->window, enabled);
            glXQueryDrawable(native->display, native->window, GLX_SWAP_INTERVAL_EXT, &actual);
            success = actual == (unsigned int) enabled;
        }
    } else if (native->swap_mesa && native->get_mesa) success = native->swap_mesa((unsigned int) enabled) == 0 && native->get_mesa() == enabled;
    else if (enabled && native->swap_sgi) success = native->swap_sgi(1) == 0;
    XSync(native->display, False);
    XSetErrorHandler(previous);
    return maage_glx_result(renderer, success, MAAGE_OPERATION_SET);
}
