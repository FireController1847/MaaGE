#import <AppKit/AppKit.h>
#import <Metal/Metal.h>
#import <QuartzCore/CAMetalLayer.h>
#import <QuartzCore/CATransaction.h>
#include "../render_internal.h"
#include "maage/log.h"
#include <stdlib.h>

typedef struct maage_metal_renderer {
    id<MTLDevice> device;
    id<MTLCommandQueue> queue;
    CAMetalLayer *layer;
    CALayer *previous_layer;
    NSView *view;
    BOOL previous_wants_layer;
    int installed;
    id<MTLRenderPipelineState> pixels_pipeline;
    id<MTLTexture> pixels_texture;
} maage_metal_renderer;

static maage_render_result maage_metal_create(maage_renderer *renderer)
{
    maage_metal_renderer *native;
    id<MTLLibrary> library;
    id<MTLFunction> vertex, fragment;
    MTLRenderPipelineDescriptor *pipeline;
    NSError *error = nil;
    NSString *source = @"#include <metal_stdlib>\nusing namespace metal;\n"
        "struct V { float4 position [[position]]; float2 uv; };\n"
        "vertex V canvas_vertex(uint i [[vertex_id]]) {\n"
        "const float2 p[3] = {float2(-1,-1),float2(3,-1),float2(-1,3)};\n"
        "V v; v.position=float4(p[i],0,1); v.uv=float2((p[i].x+1)*0.5,(1-p[i].y)*0.5); return v; }\n"
        "fragment float4 canvas_fragment(V v [[stage_in]], texture2d<float> image [[texture(0)]]) {\n"
        "constexpr sampler s(filter::linear,address::clamp_to_edge); return image.sample(s,v.uv); }\n";
    if (![NSThread isMainThread]) return MAAGE_RENDER_WINDOW_ERROR;
    if (renderer->native.backend != MAAGE_WINDOW_COCOA) return MAAGE_RENDER_UNSUPPORTED;
    native = calloc(1, sizeof(*native));
    if (!native) return MAAGE_RENDER_OUT_OF_MEMORY;
    renderer->platform = native;
    native->device = MTLCreateSystemDefaultDevice();
    if (!native->device) return MAAGE_RENDER_UNAVAILABLE;
    native->queue = [native->device newCommandQueue];
    if (!native->queue) return MAAGE_RENDER_OUT_OF_MEMORY;
    library = [native->device newLibraryWithSource:source options:nil error:&error];
    if (!library) {
        maage_log_error("maage.render.metal", "Canvas shader compilation failed: %s", [[error localizedDescription] UTF8String]);
        return MAAGE_RENDER_PLATFORM_ERROR;
    }
    vertex = [library newFunctionWithName:@"canvas_vertex"];
    fragment = [library newFunctionWithName:@"canvas_fragment"];
    pipeline = [[MTLRenderPipelineDescriptor alloc] init];
    pipeline.vertexFunction = vertex;
    pipeline.fragmentFunction = fragment;
    pipeline.colorAttachments[0].pixelFormat = MTLPixelFormatBGRA8Unorm;
    native->pixels_pipeline = [native->device newRenderPipelineStateWithDescriptor:pipeline error:&error];
    [pipeline release]; [vertex release]; [fragment release]; [library release];
    if (!native->pixels_pipeline) {
        maage_log_error("maage.render.metal", "Canvas pipeline creation failed: %s", [[error localizedDescription] UTF8String]);
        return MAAGE_RENDER_PLATFORM_ERROR;
    }
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
    renderer->capabilities.features |= MAAGE_RENDER_CAP_PIXEL_PRESENT;
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
        [native->view setLayer:native->previous_layer];
        [native->view setWantsLayer:native->previous_wants_layer];
    }
    [native->previous_layer release];
    [native->layer release];
    [native->queue release];
    [native->pixels_texture release];
    [native->pixels_pipeline release];
    [native->device release];
    free(native);
}
static maage_render_result maage_metal_present(maage_renderer *renderer, const maage_window_state *state)
{
    maage_metal_renderer *native = renderer->platform;
    NSWindow *window = renderer->native.handles.cocoa.window;
    NSAutoreleasePool *pool;
    id<CAMetalDrawable> drawable;
    id<MTLCommandBuffer> command;
    id<MTLRenderCommandEncoder> encoder;
    MTLRenderPassDescriptor *pass;
    MTLTextureDescriptor *texture;
    maage_render_result result;
    if (![NSThread isMainThread]) return MAAGE_RENDER_WINDOW_ERROR;
    if ([window isMiniaturized] || !([window occlusionState] & NSWindowOcclusionStateVisible))
        return MAAGE_RENDER_SKIPPED;
    pool = [[NSAutoreleasePool alloc] init];
    /* Use the entire content view, including permitted notch-adjacent regions.
     * Safe-area insets are for UI placement, not drawable cropping. */
    [CATransaction begin];
    [CATransaction setDisableActions:YES];
    native->layer.frame = NSRectToCGRect([native->view bounds]);
    native->layer.contentsScale = state->width > 0 ?
        (double)state->framebuffer_width / state->width : [window backingScaleFactor];
    native->layer.drawableSize = CGSizeMake(state->framebuffer_width, state->framebuffer_height);
    [CATransaction commit];
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
    if (renderer->pixels) {
        if (!native->pixels_texture || [native->pixels_texture width] != (NSUInteger)renderer->pixels->width ||
            [native->pixels_texture height] != (NSUInteger)renderer->pixels->height) {
            [native->pixels_texture release];
            native->pixels_texture = nil;
            texture = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA8Unorm
                width:renderer->pixels->width height:renderer->pixels->height mipmapped:NO];
            texture.usage = MTLTextureUsageShaderRead;
            texture.storageMode = MTLStorageModeManaged;
            if (@available(macOS 10.15, *)) {
                if ([native->device hasUnifiedMemory]) texture.storageMode = MTLStorageModeShared;
            }
            native->pixels_texture = [native->device newTextureWithDescriptor:texture];
            if (!native->pixels_texture) { [pool drain]; return MAAGE_RENDER_OUT_OF_MEMORY; }
        }
        [native->pixels_texture replaceRegion:MTLRegionMake2D(0, 0, renderer->pixels->width, renderer->pixels->height)
            mipmapLevel:0 withBytes:renderer->pixels->pixels bytesPerRow:renderer->pixels->stride];
    }
    encoder = [command renderCommandEncoderWithDescriptor:pass];
    if (!encoder) { [pool drain]; return MAAGE_RENDER_PLATFORM_ERROR; }
    if (renderer->pixels) {
        [encoder setRenderPipelineState:native->pixels_pipeline];
        [encoder setFragmentTexture:native->pixels_texture atIndex:0];
        [encoder drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:3];
    }
    [encoder endEncoding];
    [command presentDrawable:drawable];
    [command commit];
    [command waitUntilCompleted];
    result = [command status] == MTLCommandBufferStatusError ? MAAGE_RENDER_PLATFORM_ERROR : MAAGE_RENDER_OK;
    [pool drain];
    return result;
}
const maage_render_backend_api maage_render_metal_api = {
    MAAGE_RENDER_METAL, maage_metal_create, maage_metal_destroy, maage_metal_present,
    MAAGE_RENDER_CAPABILITIES_VERSION
};
