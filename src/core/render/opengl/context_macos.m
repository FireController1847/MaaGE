#define GL_SILENCE_DEPRECATION
#import <AppKit/AppKit.h>
#include "opengl_internal.h"
#include <stdlib.h>
#include <dlfcn.h>

typedef struct maage_opengl_context {
    NSOpenGLContext *context;
    NSOpenGLView *view;
    NSSize backing_size;
} maage_opengl_context;

maage_status maage_opengl_context_create(maage_renderer *renderer, int modern) {
    NSOpenGLPixelFormatAttribute attributes[] = {
        NSOpenGLPFAOpenGLProfile, modern ? NSOpenGLProfileVersion4_1Core : NSOpenGLProfileVersionLegacy,
        NSOpenGLPFADoubleBuffer, NSOpenGLPFAColorSize, 24, NSOpenGLPFAAlphaSize, 8,
        NSOpenGLPFADepthSize, 24, NSOpenGLPFAStencilSize, 8, 0};
    maage_opengl_context *native;
    NSOpenGLPixelFormat *format;
    NSView *parent;
    GLint interval = 1;
    if (renderer->native.backend != MAAGE_WINDOW_COCOA) return MAAGE_STATUS_VALUE(MAAGE_STATUS_NOT_IMPLEMENTED, MAAGE_CONTEXT_RENDER, MAAGE_OPERATION_INITIALIZE, 0);
    if (![NSThread isMainThread]) return MAAGE_STATUS_VALUE(MAAGE_STATUS_DEPENDENCY_FAILURE, MAAGE_CONTEXT_RENDER, MAAGE_OPERATION_INITIALIZE, MAAGE_STATUS_INVALID_STATE);
    native = calloc(1, sizeof(*native));
    if (!native) return MAAGE_STATUS_VALUE(MAAGE_STATUS_ALLOCATION_FAILED, MAAGE_CONTEXT_RENDER, MAAGE_OPERATION_INITIALIZE, 0);
    ((maage_opengl_renderer *) renderer->platform)->context = native;
    format = [[NSOpenGLPixelFormat alloc] initWithAttributes:attributes];
    if (!format) return MAAGE_STATUS_VALUE(MAAGE_STATUS_SYSTEM_NOT_SUPPORTED, MAAGE_CONTEXT_RENDER, MAAGE_OPERATION_INITIALIZE, 0);
    parent = renderer->native.handles.cocoa.view;
    native->view = [[NSOpenGLView alloc] initWithFrame:[parent bounds] pixelFormat:format];
    [format release];
    if (!native->view) return MAAGE_STATUS_VALUE(MAAGE_STATUS_ALLOCATION_FAILED, MAAGE_CONTEXT_RENDER, MAAGE_OPERATION_INITIALIZE, 0);
    [native->view setAutoresizingMask:NSViewWidthSizable | NSViewHeightSizable];
    [native->view setWantsBestResolutionOpenGLSurface:YES];
    [parent addSubview:native->view];
    native->context = [[native->view openGLContext] retain];
    if (!native->context) return MAAGE_STATUS_VALUE(MAAGE_STATUS_SYSTEM_NOT_SUPPORTED, MAAGE_CONTEXT_RENDER, MAAGE_OPERATION_INITIALIZE, 0);
    /* Hidden startup draws do not pass through AppKit's drawRect/lockFocus. */
    [native->context setView:native->view];
    [native->context setValues:&interval forParameter:NSOpenGLContextParameterSwapInterval];
    return MAAGE_STATUS_VALUE(MAAGE_STATUS_SUCCESS, MAAGE_CONTEXT_RENDER, MAAGE_OPERATION_INITIALIZE, 0);
}
maage_status maage_opengl_context_attach(maage_renderer *renderer) {
    return maage_opengl_context_bind(renderer);
}
void maage_opengl_context_destroy(maage_renderer *renderer) {
    maage_opengl_context *native = ((maage_opengl_renderer *) renderer->platform)->context;
    if (!native) return;
    if ([NSOpenGLContext currentContext] == native->context) [NSOpenGLContext clearCurrentContext];
    [native->context clearDrawable];
    [native->view removeFromSuperview];
    [native->view release];
    [native->context release];
    free(native);
    ((maage_opengl_renderer *) renderer->platform)->context = NULL;
}
maage_status maage_opengl_context_bind(maage_renderer *renderer) {
    maage_opengl_context *native = ((maage_opengl_renderer *) renderer->platform)->context;
    NSAutoreleasePool *pool = [[NSAutoreleasePool alloc] init];
    int current;
    if (![NSThread isMainThread] || !native || !native->context) {
        [pool drain];
        return MAAGE_STATUS_VALUE(MAAGE_STATUS_INVALID_STATE, MAAGE_CONTEXT_RENDER, MAAGE_OPERATION_ACQUIRE, 0);
    }
    if ([native->context view] != native->view) [native->context setView:native->view];
    {
        NSSize size = [native->view convertRectToBacking:[native->view bounds]].size;
        if (!NSEqualSizes(size, native->backing_size) || renderer->refresh) {
            [native->context update];
            native->backing_size = size;
        }
    }
    if ([NSOpenGLContext currentContext] != native->context) [native->context makeCurrentContext];
    current = [NSOpenGLContext currentContext] == native->context;
    [pool drain];
    return maage_status_make(current ? MAAGE_STATUS_SUCCESS : MAAGE_STATUS_INVALID_STATE, MAAGE_CONTEXT_RENDER, MAAGE_OPERATION_ACQUIRE, 0);
}
maage_status maage_opengl_context_swap(maage_renderer *renderer) {
    maage_opengl_context *native = ((maage_opengl_renderer *) renderer->platform)->context;
    NSAutoreleasePool *pool = [[NSAutoreleasePool alloc] init];
    [native->context flushBuffer];
    [pool drain];
    return maage_status_make(MAAGE_STATUS_SUCCESS, 0, MAAGE_OPERATION_PRESENT, 0);
}
maage_opengl_proc maage_opengl_context_proc(maage_renderer *renderer, const char *name) {
    union {
        void *object;
        maage_opengl_proc function;
    } address;
    (void) renderer;
    address.object = dlsym(RTLD_DEFAULT, name);
    return address.function;
}
maage_status maage_opengl_context_vsync(maage_renderer *renderer, int enabled) {
    maage_opengl_context *native = ((maage_opengl_renderer *) renderer->platform)->context;
    GLint interval = enabled, actual = -1;
    maage_status result = maage_opengl_context_bind(renderer);
    if (maage_status_failed(result)) return result;
    [native->context setValues:&interval forParameter:NSOpenGLContextParameterSwapInterval];
    [native->context getValues:&actual forParameter:NSOpenGLContextParameterSwapInterval];
    return maage_status_make(actual == enabled ? MAAGE_STATUS_SUCCESS : MAAGE_STATUS_SYSTEM_NOT_SUPPORTED, MAAGE_CONTEXT_RENDER, MAAGE_OPERATION_SET, 0);
}
