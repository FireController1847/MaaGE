#define GL_SILENCE_DEPRECATION
#import <AppKit/AppKit.h>
#include "opengl_internal.h"
#include <stdlib.h>

typedef struct maage_opengl_context {
    NSOpenGLContext *context;
    NSOpenGLView *view;
} maage_opengl_context;

maage_render_result maage_opengl_context_create(maage_renderer *renderer, int modern)
{
    NSOpenGLPixelFormatAttribute attributes[] = {
        NSOpenGLPFAOpenGLProfile, modern ? NSOpenGLProfileVersion4_1Core : NSOpenGLProfileVersionLegacy,
        NSOpenGLPFADoubleBuffer, NSOpenGLPFAColorSize, 24, NSOpenGLPFAAlphaSize, 8,
        NSOpenGLPFADepthSize, 24, NSOpenGLPFAStencilSize, 8, 0
    };
    maage_opengl_context *native;
    NSOpenGLPixelFormat *format;
    NSView *parent;
    GLint interval = 1;
    if (renderer->native.backend != MAAGE_WINDOW_COCOA) return MAAGE_RENDER_UNSUPPORTED;
    if (![NSThread isMainThread]) return MAAGE_RENDER_WINDOW_ERROR;
    native = calloc(1, sizeof(*native));
    if (!native) return MAAGE_RENDER_OUT_OF_MEMORY;
    renderer->platform = native;
    format = [[NSOpenGLPixelFormat alloc] initWithAttributes:attributes];
    if (!format) return MAAGE_RENDER_UNAVAILABLE;
    parent = renderer->native.handles.cocoa.view;
    native->view = [[NSOpenGLView alloc] initWithFrame:[parent bounds] pixelFormat:format];
    [format release];
    if (!native->view) return MAAGE_RENDER_OUT_OF_MEMORY;
    [native->view setAutoresizingMask:NSViewWidthSizable | NSViewHeightSizable];
    [native->view setWantsBestResolutionOpenGLSurface:YES];
    [parent addSubview:native->view];
    native->context = [[native->view openGLContext] retain];
    if (!native->context) return MAAGE_RENDER_UNAVAILABLE;
    [native->context setValues:&interval forParameter:NSOpenGLContextParameterSwapInterval];
    return MAAGE_RENDER_OK;
}
void maage_opengl_context_destroy(maage_renderer *renderer)
{
    maage_opengl_context *native = renderer->platform;
    if (!native) return;
    if ([NSOpenGLContext currentContext] == native->context) [NSOpenGLContext clearCurrentContext];
    [native->context clearDrawable];
    [native->view removeFromSuperview];
    [native->view release];
    [native->context release];
    free(native);
}
int maage_opengl_context_bind(maage_renderer *renderer)
{
    maage_opengl_context *native = renderer->platform;
    NSAutoreleasePool *pool = [[NSAutoreleasePool alloc] init];
    int current;
    [native->context update];
    [native->context makeCurrentContext];
    current = [NSOpenGLContext currentContext] == native->context;
    [pool drain];
    return current;
}
int maage_opengl_context_swap(maage_renderer *renderer)
{
    maage_opengl_context *native = renderer->platform;
    NSAutoreleasePool *pool = [[NSAutoreleasePool alloc] init];
    [native->context flushBuffer];
    [pool drain];
    return 1;
}
