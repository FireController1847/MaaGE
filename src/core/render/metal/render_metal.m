#import <AppKit/AppKit.h>
#import <Metal/Metal.h>
#import <QuartzCore/CAMetalLayer.h>
#import <QuartzCore/CATransaction.h>
#include "../render_internal.h"
#include <stdlib.h>

typedef struct maage_metal_renderer {
    id<MTLDevice> device;
    id<MTLCommandQueue> queue;
    CAMetalLayer *layer;
    CALayer *previous_layer;
    NSColor *previous_background;
    NSView *view;
    BOOL previous_wants_layer;
    int installed;
} maage_metal_renderer;

static maage_render_result maage_metal_create(maage_renderer *renderer)
{
    maage_metal_renderer *native;
    if (![NSThread isMainThread]) return MAAGE_RENDER_WINDOW_ERROR;
    if (renderer->native.backend != MAAGE_WINDOW_COCOA) return MAAGE_RENDER_UNSUPPORTED;
    native = calloc(1, sizeof(*native));
    if (!native) return MAAGE_RENDER_OUT_OF_MEMORY;
    renderer->platform = native;
    native->device = MTLCreateSystemDefaultDevice();
    if (!native->device) return MAAGE_RENDER_UNAVAILABLE;
    native->queue = [native->device newCommandQueue];
    if (!native->queue) return MAAGE_RENDER_OUT_OF_MEMORY;
    native->view = renderer->native.handles.cocoa.view;
    if (!native->view) return MAAGE_RENDER_WINDOW_ERROR;
    native->layer = [[CAMetalLayer alloc] init];
    if (!native->layer) return MAAGE_RENDER_OUT_OF_MEMORY;
    native->layer.device = native->device;
    native->layer.pixelFormat = MTLPixelFormatBGRA8Unorm;
    native->layer.framebufferOnly = YES;
    native->layer.opaque = YES;
    native->layer.displaySyncEnabled = YES;
    native->layer.allowsNextDrawableTimeout = YES;
    native->layer.autoresizingMask = kCALayerWidthSizable | kCALayerHeightSizable;
    native->previous_background = [[(NSWindow *)renderer->native.handles.cocoa.window backgroundColor] retain];
    native->previous_wants_layer = [native->view wantsLayer];
    native->previous_layer = [[native->view layer] retain];
    [native->view setWantsLayer:YES];
    [native->view setLayer:native->layer];
    native->installed = 1;
    renderer->capabilities.features |= MAAGE_RENDER_CAP_SHADERS | MAAGE_RENDER_CAP_VERTEX_BUFFERS |
        MAAGE_RENDER_CAP_INDEX_BUFFERS | MAAGE_RENDER_CAP_RENDER_TARGETS | MAAGE_RENDER_CAP_DEPTH |
        MAAGE_RENDER_CAP_STENCIL | MAAGE_RENDER_CAP_INSTANCING | MAAGE_RENDER_CAP_COMPUTE;
    renderer->capabilities.texture_formats = MAAGE_RENDER_FORMAT_RGBA8 | MAAGE_RENDER_FORMAT_BGRA8;
    renderer->capabilities.synchronization = MAAGE_RENDER_SYNC_CPU_WAIT;
    /* Texture limits vary by GPU family; leave unknown until resource creation
     * needs family-specific queries rather than promising an unqueried limit. */
    return MAAGE_RENDER_OK;
}
static void maage_metal_destroy(maage_renderer *renderer)
{
    maage_metal_renderer *native = renderer->platform;
    if (!native) return;
    /* Present waits for completion, so no submitted frame outlives these objects. */
    if (native->installed) {
        [(NSWindow *)renderer->native.handles.cocoa.window setBackgroundColor:native->previous_background];
        [native->view setLayer:native->previous_layer];
        [native->view setWantsLayer:native->previous_wants_layer];
    }
    [native->previous_background release];
    [native->previous_layer release];
    [native->layer release];
    [native->queue release];
    [native->device release];
    free(native);
}
static maage_render_result maage_metal_frame(maage_renderer *renderer, const maage_window_state *state, int preparing)
{
    maage_metal_renderer *native = renderer->platform;
    NSWindow *window = renderer->native.handles.cocoa.window;
    NSAutoreleasePool *pool;
    id<CAMetalDrawable> drawable;
    id<MTLCommandBuffer> command;
    id<MTLRenderCommandEncoder> encoder;
    MTLRenderPassDescriptor *pass;
    maage_render_result result;
    if (![NSThread isMainThread]) return MAAGE_RENDER_WINDOW_ERROR;
    if (!preparing && ([window isMiniaturized] || !([window occlusionState] & NSWindowOcclusionStateVisible)))
        return MAAGE_RENDER_SKIPPED;
    pool = [[NSAutoreleasePool alloc] init];
    /* Use the entire content view, including permitted notch-adjacent regions.
     * Safe-area insets are for UI placement, not drawable cropping. */
    [CATransaction begin];
    [CATransaction setDisableActions:YES];
    native->layer.presentsWithTransaction = preparing != 0;
    if (preparing) {
        NSColor *color = [NSColor colorWithSRGBRed:renderer->clear[0] green:renderer->clear[1]
            blue:renderer->clear[2] alpha:renderer->clear[3]];
        [window setBackgroundColor:color];
        native->layer.backgroundColor = [color CGColor];
    }
    native->layer.frame = NSRectToCGRect([native->view bounds]);
    native->layer.contentsScale = state->width > 0 ?
        (double)state->framebuffer_width / state->width : [window backingScaleFactor];
    native->layer.drawableSize = CGSizeMake(state->framebuffer_width, state->framebuffer_height);
    [CATransaction commit];
    if (preparing) [CATransaction flush];
    renderer->refresh = 0;
    drawable = [native->layer nextDrawable];
    if (!drawable) { [pool drain]; return MAAGE_RENDER_SKIPPED; }
    command = [native->queue commandBuffer];
    pass = [MTLRenderPassDescriptor renderPassDescriptor];
    pass.colorAttachments[0].texture = [drawable texture];
    pass.colorAttachments[0].loadAction = MTLLoadActionClear;
    pass.colorAttachments[0].storeAction = MTLStoreActionStore;
    pass.colorAttachments[0].clearColor = MTLClearColorMake(renderer->clear[0],
        renderer->clear[1], renderer->clear[2], renderer->clear[3]);
    if (!command || !pass) { [pool drain]; return MAAGE_RENDER_OUT_OF_MEMORY; }
    encoder = [command renderCommandEncoderWithDescriptor:pass];
    if (!encoder) { [pool drain]; return MAAGE_RENDER_PLATFORM_ERROR; }
    [encoder endEncoding];
    if (!preparing) [command presentDrawable:drawable];
    [command commit];
    [command waitUntilCompleted];
    result = [command status] == MTLCommandBufferStatusError ? MAAGE_RENDER_PLATFORM_ERROR : MAAGE_RENDER_OK;
    if (preparing && result == MAAGE_RENDER_OK) {
        /* Present completed content with the layer transaction before reveal. */
        [CATransaction begin];
        [CATransaction setDisableActions:YES];
        [drawable present];
        [CATransaction commit];
        [CATransaction flush];
    }
    [pool drain];
    return result;
}
static maage_render_result maage_metal_present(maage_renderer *renderer, const maage_window_state *state)
{
    return maage_metal_frame(renderer, state, 0);
}
static maage_render_result maage_metal_prepare_frame(maage_renderer *renderer, const maage_window_state *state)
{
    return maage_metal_frame(renderer, state, 1);
}
const maage_render_backend_api maage_render_metal_api = {
    MAAGE_RENDER_METAL, maage_metal_create, maage_metal_destroy, maage_metal_present,
    MAAGE_RENDER_CAPABILITIES_VERSION, maage_metal_prepare_frame
};
