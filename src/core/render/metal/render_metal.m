#import <AppKit/AppKit.h>
#import <Metal/Metal.h>
#import <QuartzCore/CAMetalLayer.h>
#import <QuartzCore/CATransaction.h>
#include "../render_internal.h"
#include "maage/log.h"
#include "maage_mesh.h"
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
    id<MTLRenderPipelineState> pipeline;
    id<MTLDepthStencilState> depth_state;
    id<MTLTexture> depth;
} maage_metal_renderer;
typedef struct maage_metal_mesh {
    id<MTLBuffer> vertices;
    id<MTLBuffer> indices;
    id<MTLTexture> atlas;
    NSUInteger index_count;
} maage_metal_mesh;

static maage_render_result maage_metal_pipeline(maage_metal_renderer *native)
{
    NSString *source;
    id<MTLLibrary> library;
    id<MTLFunction> vertex, fragment;
    id<MTLRenderPipelineState> compiled;
    id<MTLDepthStencilState> depth_state;
    MTLRenderPipelineDescriptor *pipeline;
    MTLDepthStencilDescriptor *depth;
    NSError *error = nil;
    source = [[NSString alloc] initWithUTF8String:maage_metal_mesh_source];
    if (!source) return MAAGE_RENDER_OUT_OF_MEMORY;
    library = [native->device newLibraryWithSource:source options:nil error:&error];
    [source release];
    if (!library) {
        maage_log_error("maage.render.metal", "Mesh shader library unavailable: %s",
            error ? [[error localizedDescription] UTF8String] : "unknown error");
        return MAAGE_RENDER_PLATFORM_ERROR;
    }
    vertex = [library newFunctionWithName:@"maage_mesh_vertex_shader"];
    fragment = [library newFunctionWithName:@"maage_mesh_fragment_shader"];
    pipeline = [[MTLRenderPipelineDescriptor alloc] init];
    pipeline.vertexFunction = vertex;
    pipeline.fragmentFunction = fragment;
    pipeline.colorAttachments[0].pixelFormat = MTLPixelFormatBGRA8Unorm;
    pipeline.depthAttachmentPixelFormat = MTLPixelFormatDepth32Float;
    compiled = vertex && fragment && pipeline ?
        [native->device newRenderPipelineStateWithDescriptor:pipeline error:&error] : nil;
    [pipeline release];
    [vertex release];
    [fragment release];
    [library release];
    if (!compiled) {
        maage_log_error("maage.render.metal", "Mesh pipeline unavailable: %s",
            error ? [[error localizedDescription] UTF8String] : "missing shader function");
        return MAAGE_RENDER_PLATFORM_ERROR;
    }
    depth = [[MTLDepthStencilDescriptor alloc] init];
    depth.depthCompareFunction = MTLCompareFunctionLessEqual;
    depth.depthWriteEnabled = YES;
    depth_state = depth ? [native->device newDepthStencilStateWithDescriptor:depth] : nil;
    [depth release];
    if (!depth_state) {
        [compiled release];
        return MAAGE_RENDER_OUT_OF_MEMORY;
    }
    native->pipeline = compiled;
    native->depth_state = depth_state;
    return MAAGE_RENDER_OK;
}

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
        MAAGE_RENDER_CAP_STENCIL | MAAGE_RENDER_CAP_INSTANCING | MAAGE_RENDER_CAP_COMPUTE |
        MAAGE_RENDER_CAP_TEXTURED_MESH;
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
    [native->depth release];
    [native->depth_state release];
    [native->pipeline release];
    [native->queue release];
    [native->device release];
    free(native);
}
static maage_render_result maage_metal_frame(maage_renderer *renderer, const maage_window_state *state,
    int preparing, const maage_render_mesh *mesh, const float *view_projection)
{
    maage_metal_renderer *native = renderer->platform;
    NSWindow *window = renderer->native.handles.cocoa.window;
    NSAutoreleasePool *pool;
    id<CAMetalDrawable> drawable;
    id<MTLCommandBuffer> command;
    id<MTLRenderCommandEncoder> encoder;
    MTLRenderPassDescriptor *pass;
    maage_render_result result;
    maage_metal_mesh *geometry = mesh ? mesh->platform : NULL;
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
    if (geometry && (!native->depth || native->depth.width != drawable.texture.width ||
        native->depth.height != drawable.texture.height)) {
        MTLTextureDescriptor *descriptor = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:
            MTLPixelFormatDepth32Float width:drawable.texture.width height:drawable.texture.height mipmapped:NO];
        [native->depth release];
        descriptor.storageMode = MTLStorageModePrivate;
        descriptor.usage = MTLTextureUsageRenderTarget;
        native->depth = [native->device newTextureWithDescriptor:descriptor];
        if (!native->depth) { [pool drain]; return MAAGE_RENDER_OUT_OF_MEMORY; }
    }
    command = [native->queue commandBuffer];
    pass = [MTLRenderPassDescriptor renderPassDescriptor];
    pass.colorAttachments[0].texture = [drawable texture];
    pass.colorAttachments[0].loadAction = MTLLoadActionClear;
    pass.colorAttachments[0].storeAction = MTLStoreActionStore;
    pass.colorAttachments[0].clearColor = MTLClearColorMake(renderer->clear[0],
        renderer->clear[1], renderer->clear[2], renderer->clear[3]);
    if (!command || !pass) { [pool drain]; return MAAGE_RENDER_OUT_OF_MEMORY; }
    if (geometry) {
        pass.depthAttachment.texture = native->depth;
        pass.depthAttachment.loadAction = MTLLoadActionClear;
        pass.depthAttachment.storeAction = MTLStoreActionDontCare;
        pass.depthAttachment.clearDepth = 1.0;
    }
    encoder = [command renderCommandEncoderWithDescriptor:pass];
    if (!encoder) { [pool drain]; return MAAGE_RENDER_PLATFORM_ERROR; }
    if (geometry) {
        MTLViewport viewport = {0, 0, drawable.texture.width, drawable.texture.height, 0, 1};
        [encoder setViewport:viewport];
        [encoder setRenderPipelineState:native->pipeline];
        [encoder setDepthStencilState:native->depth_state];
        [encoder setCullMode:MTLCullModeBack];
        [encoder setFrontFacingWinding:MTLWindingCounterClockwise];
        [encoder setVertexBuffer:geometry->vertices offset:0 atIndex:0];
        [encoder setVertexBytes:view_projection length:16 * sizeof(float) atIndex:1];
        [encoder setFragmentTexture:geometry->atlas atIndex:0];
        [encoder drawIndexedPrimitives:MTLPrimitiveTypeTriangle indexCount:geometry->index_count
            indexType:MTLIndexTypeUInt32 indexBuffer:geometry->indices indexBufferOffset:0];
    }
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
    return maage_metal_frame(renderer, state, 0, NULL, NULL);
}
static maage_render_result maage_metal_prepare_frame(maage_renderer *renderer, const maage_window_state *state)
{
    return maage_metal_frame(renderer, state, 1, NULL, NULL);
}
static maage_render_result maage_metal_upload_mesh(maage_renderer *renderer, maage_render_mesh *mesh,
    const maage_mesh_data *data, const maage_pixel_buffer *atlas)
{
    maage_metal_renderer *native = renderer->platform;
    maage_metal_mesh *geometry;
    MTLTextureDescriptor *texture;
    NSAutoreleasePool *pool;
    maage_render_result result;
    if (![NSThread isMainThread]) return MAAGE_RENDER_WINDOW_ERROR;
    if (sizeof(unsigned int) != 4 || sizeof(maage_mesh_vertex) != 48)
        return MAAGE_RENDER_UNSUPPORTED;
    pool = [[NSAutoreleasePool alloc] init];
    /* Clear-only renderers do not need the mesh shaders or depth state. */
    if (!native->pipeline) {
        result = maage_metal_pipeline(native);
        if (result != MAAGE_RENDER_OK) { [pool drain]; return result; }
    }
    geometry = calloc(1, sizeof(*geometry));
    if (!geometry) { [pool drain]; return MAAGE_RENDER_OUT_OF_MEMORY; }
    mesh->platform = geometry;
    geometry->vertices = [native->device newBufferWithBytes:data->vertices
        length:data->vertex_count * sizeof(*data->vertices) options:MTLResourceStorageModeShared];
    geometry->indices = [native->device newBufferWithBytes:data->indices
        length:data->index_count * sizeof(*data->indices) options:MTLResourceStorageModeShared];
    geometry->index_count = data->index_count;
    texture = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA8Unorm
        width:atlas->width height:atlas->height mipmapped:NO];
    texture.usage = MTLTextureUsageShaderRead;
    texture.storageMode = MTLStorageModeManaged;
    if (@available(macOS 10.15, *)) {
        if ([native->device supportsFamily:MTLGPUFamilyApple1])
            texture.storageMode = MTLStorageModeShared;
    }
    geometry->atlas = [native->device newTextureWithDescriptor:texture];
    if (geometry->atlas)
        [geometry->atlas replaceRegion:MTLRegionMake2D(0, 0, atlas->width, atlas->height)
            mipmapLevel:0 withBytes:atlas->pixels bytesPerRow:atlas->stride];
    [pool drain];
    return geometry->vertices && geometry->indices && geometry->atlas ? MAAGE_RENDER_OK : MAAGE_RENDER_OUT_OF_MEMORY;
}
static void maage_metal_destroy_mesh(maage_render_mesh *mesh)
{
    maage_metal_mesh *geometry = mesh->platform;
    if (!geometry) return;
    [geometry->vertices release];
    [geometry->indices release];
    [geometry->atlas release];
    free(geometry);
    mesh->platform = NULL;
}
static maage_render_result maage_metal_draw_mesh(maage_renderer *renderer, const maage_render_mesh *mesh,
    const maage_window_state *state, const float view_projection[16])
{
    return maage_metal_frame(renderer, state, 0, mesh, view_projection);
}
const maage_render_backend_api maage_render_metal_api = {
    MAAGE_RENDER_METAL, maage_metal_create, maage_metal_destroy, maage_metal_present,
    MAAGE_RENDER_CAPABILITIES_VERSION, maage_metal_prepare_frame, maage_metal_upload_mesh,
    maage_metal_destroy_mesh, maage_metal_draw_mesh
};
