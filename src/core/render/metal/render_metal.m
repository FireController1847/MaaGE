#import <AppKit/AppKit.h>
#import <Metal/Metal.h>
#import <QuartzCore/CAMetalLayer.h>
#import <QuartzCore/CATransaction.h>
#include "../render_internal.h"
#include "maage/log.h"
#include "maage_mesh.h"
#include <stdlib.h>
#include <string.h>

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
    id<MTLRenderPipelineState> ui_pipeline;
    id<MTLBuffer> ui_vertices[3];
    id<MTLBuffer> ui_indices[3];
} maage_metal_renderer;
typedef struct maage_metal_mesh {
    id<MTLBuffer> vertices;
    id<MTLBuffer> indices;
    NSUInteger index_count;
} maage_metal_mesh;

static maage_status maage_metal_compile_pipeline(maage_renderer *renderer,
    id<MTLLibrary> library, int ui, id<MTLRenderPipelineState> *output)
{
    maage_metal_renderer *native = renderer->platform;
    id<MTLFunction> vertex, fragment;
    id<MTLRenderPipelineState> compiled;
    MTLRenderPipelineDescriptor *pipeline;
    NSError *error = nil;
    *output = nil;
    vertex = [library newFunctionWithName:ui ? @"maage_ui_vertex_shader" : @"maage_mesh_vertex_shader"];
    fragment = [library newFunctionWithName:ui ? @"maage_ui_fragment_shader" : @"maage_mesh_fragment_shader"];
    pipeline = [[MTLRenderPipelineDescriptor alloc] init];
    pipeline.vertexFunction = vertex;
    pipeline.fragmentFunction = fragment;
    pipeline.colorAttachments[0].pixelFormat = MTLPixelFormatBGRA8Unorm;
    if (ui) {
        pipeline.colorAttachments[0].blendingEnabled = YES;
        pipeline.colorAttachments[0].sourceRGBBlendFactor = MTLBlendFactorSourceAlpha;
        pipeline.colorAttachments[0].destinationRGBBlendFactor = MTLBlendFactorOneMinusSourceAlpha;
        pipeline.colorAttachments[0].sourceAlphaBlendFactor = MTLBlendFactorOne;
        pipeline.colorAttachments[0].destinationAlphaBlendFactor = MTLBlendFactorOneMinusSourceAlpha;
    } else pipeline.depthAttachmentPixelFormat = MTLPixelFormatDepth32Float;
    compiled = vertex && fragment && pipeline ?
        [native->device newRenderPipelineStateWithDescriptor:pipeline error:&error] : nil;
    [pipeline release];
    [vertex release];
    [fragment release];
    if (!compiled) return maage_status_trace_record(renderer->operation_trace,
        maage_status_make(MAAGE_STATUS_ERROR, MAAGE_CONTEXT_RENDER, MAAGE_OPERATION_INITIALIZE, 0),
        MAAGE_CONTEXT_RENDER, MAAGE_OPERATION_INITIALIZE, __FILE__, __LINE__, error ? (long)[error code] : 0);
    *output = compiled;
    return maage_status_make(MAAGE_STATUS_SUCCESS, 0, MAAGE_OPERATION_INITIALIZE, 0);
}
static maage_status maage_metal_pipeline(maage_renderer *renderer)
{
    maage_metal_renderer *native = renderer->platform;
    NSString *source;
    id<MTLLibrary> library;
    id<MTLRenderPipelineState> compiled = nil, ui_pipeline = nil;
    maage_status result;
    id<MTLDepthStencilState> depth_state;
    MTLDepthStencilDescriptor *depth;
    NSError *error = nil;
    source = [[NSString alloc] initWithUTF8String:maage_metal_mesh_source];
    if (!source) return MAAGE_STATUS_VALUE(MAAGE_STATUS_ALLOCATION_FAILED, MAAGE_CONTEXT_RENDER, MAAGE_OPERATION_INITIALIZE, 0);
    library = [native->device newLibraryWithSource:source options:nil error:&error];
    [source release];
    if (!library) {
        return maage_status_trace_record(renderer->operation_trace,
            maage_status_make(MAAGE_STATUS_ERROR, MAAGE_CONTEXT_RENDER, MAAGE_OPERATION_INITIALIZE, 0),
            MAAGE_CONTEXT_RENDER, MAAGE_OPERATION_INITIALIZE, __FILE__, __LINE__, error ? (long)[error code] : 0);
    }
    result = maage_metal_compile_pipeline(renderer, library, 0, &compiled);
    if (!maage_status_failed(result)) result = maage_metal_compile_pipeline(renderer, library, 1, &ui_pipeline);
    [library release];
    if (!compiled || !ui_pipeline) {
        [compiled release];
        [ui_pipeline release];
        return MAAGE_STATUS_PROPAGATE(renderer->operation_trace, result, MAAGE_CONTEXT_RENDER, MAAGE_OPERATION_INITIALIZE);
    }
    depth = [[MTLDepthStencilDescriptor alloc] init];
    depth.depthCompareFunction = MTLCompareFunctionLessEqual;
    depth.depthWriteEnabled = YES;
    depth_state = depth ? [native->device newDepthStencilStateWithDescriptor:depth] : nil;
    [depth release];
    if (!depth_state) {
        [compiled release];
        [ui_pipeline release];
        return MAAGE_STATUS_VALUE(MAAGE_STATUS_ALLOCATION_FAILED, MAAGE_CONTEXT_RENDER, MAAGE_OPERATION_INITIALIZE, 0);
    }
    native->pipeline = compiled;
    native->ui_pipeline = ui_pipeline;
    native->depth_state = depth_state;
    return MAAGE_STATUS_VALUE(MAAGE_STATUS_SUCCESS, MAAGE_CONTEXT_RENDER, MAAGE_OPERATION_INITIALIZE, 0);
}

static maage_status maage_metal_create(maage_renderer *renderer)
{
    maage_metal_renderer *native;
    if (![NSThread isMainThread]) return MAAGE_STATUS_VALUE(MAAGE_STATUS_DEPENDENCY_FAILURE, MAAGE_CONTEXT_RENDER, MAAGE_OPERATION_INITIALIZE, MAAGE_STATUS_INVALID_STATE);
    if (renderer->native.backend != MAAGE_WINDOW_COCOA) return MAAGE_STATUS_VALUE(MAAGE_STATUS_NOT_IMPLEMENTED, MAAGE_CONTEXT_RENDER, MAAGE_OPERATION_INITIALIZE, 0);
    native = calloc(1, sizeof(*native));
    if (!native) return MAAGE_STATUS_VALUE(MAAGE_STATUS_ALLOCATION_FAILED, MAAGE_CONTEXT_RENDER, MAAGE_OPERATION_INITIALIZE, 0);
    renderer->platform = native;
    native->device = MTLCreateSystemDefaultDevice();
    if (!native->device) return MAAGE_STATUS_VALUE(MAAGE_STATUS_SYSTEM_NOT_SUPPORTED, MAAGE_CONTEXT_RENDER, MAAGE_OPERATION_INITIALIZE, 0);
    if ([native->device name]) {
        strncpy(renderer->device_name, [[native->device name] UTF8String], sizeof(renderer->device_name) - 1);
        renderer->device_name[sizeof(renderer->device_name) - 1] = 0;
    }
    native->queue = [native->device newCommandQueue];
    if (!native->queue) return MAAGE_STATUS_VALUE(MAAGE_STATUS_ALLOCATION_FAILED, MAAGE_CONTEXT_RENDER, MAAGE_OPERATION_INITIALIZE, 0);
    native->view = renderer->native.handles.cocoa.view;
    if (!native->view) return MAAGE_STATUS_VALUE(MAAGE_STATUS_DEPENDENCY_FAILURE, MAAGE_CONTEXT_RENDER, MAAGE_OPERATION_INITIALIZE, MAAGE_STATUS_INVALID_STATE);
    native->layer = [[CAMetalLayer alloc] init];
    if (!native->layer) return MAAGE_STATUS_VALUE(MAAGE_STATUS_ALLOCATION_FAILED, MAAGE_CONTEXT_RENDER, MAAGE_OPERATION_INITIALIZE, 0);
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
        MAAGE_RENDER_CAP_TEXTURED_MESH | MAAGE_RENDER_CAP_UI | MAAGE_RENDER_CAP_SCENE;
    renderer->capabilities.texture_formats = MAAGE_RENDER_FORMAT_RGBA8 | MAAGE_RENDER_FORMAT_BGRA8;
    renderer->capabilities.synchronization = MAAGE_RENDER_SYNC_CPU_WAIT;
    /* Texture limits vary by GPU family; leave unknown until resource creation
     * needs family-specific queries rather than promising an unqueried limit. */
    return MAAGE_STATUS_VALUE(MAAGE_STATUS_SUCCESS, MAAGE_CONTEXT_RENDER, MAAGE_OPERATION_INITIALIZE, 0);
}
static maage_status maage_metal_destroy(maage_renderer *renderer)
{
    maage_metal_renderer *native = renderer->platform;
    if (!native) return maage_status_make(MAAGE_STATUS_NOOP, 0, MAAGE_OPERATION_DESTROY, 0);
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
    [native->ui_pipeline release];
    {
        int layer;
        for (layer = 0; layer < 3; ++layer) {
            [native->ui_vertices[layer] release]; [native->ui_indices[layer] release];
        }
    }
    [native->queue release];
    [native->device release];
    free(native);
    return maage_status_make(MAAGE_STATUS_SUCCESS, 0, MAAGE_OPERATION_DESTROY, 0);
}
static maage_status maage_metal_ui_buffer(maage_metal_renderer *native,
    id<MTLBuffer> *buffer, size_t bytes)
{
    id<MTLBuffer> replacement;
    size_t capacity;
    if (*buffer && [*buffer length] >= bytes) return MAAGE_STATUS_VALUE(MAAGE_STATUS_SUCCESS, MAAGE_CONTEXT_RENDER, MAAGE_OPERATION_UPLOAD, 0);
    capacity = *buffer ? [*buffer length] : 4096;
    while (capacity < bytes) {
        if (capacity > (size_t)-1 / 2) { capacity = bytes; break; }
        capacity *= 2;
    }
    replacement = [native->device newBufferWithLength:capacity options:MTLResourceStorageModeShared];
    if (!replacement) return MAAGE_STATUS_VALUE(MAAGE_STATUS_ALLOCATION_FAILED, MAAGE_CONTEXT_RENDER, MAAGE_OPERATION_UPLOAD, 0);
    [*buffer release];
    *buffer = replacement;
    return MAAGE_STATUS_VALUE(MAAGE_STATUS_SUCCESS, MAAGE_CONTEXT_RENDER, MAAGE_OPERATION_UPLOAD, 0);
}
static maage_status maage_metal_frame(maage_renderer *renderer, const maage_window_state *state,
    int preparing, const maage_render_mesh *const *meshes, size_t count, const float *view_projection,
    const maage_render_ui_data *ui)
{
    maage_metal_renderer *native = renderer->platform;
    NSWindow *window = renderer->native.handles.cocoa.window;
    NSAutoreleasePool *pool;
    id<CAMetalDrawable> drawable;
    id<MTLCommandBuffer> command;
    id<MTLRenderCommandEncoder> encoder;
    MTLRenderPassDescriptor *pass;
    maage_status result;
    maage_metal_mesh *geometry;
    size_t index;
    const maage_render_ui_data *layers[3], *batch;
    int layer;
    layers[0] = preparing ? NULL : renderer->background;
    layers[1] = preparing ? NULL : renderer->overlay; layers[2] = ui;
    if (![NSThread isMainThread]) return MAAGE_STATUS_VALUE(MAAGE_STATUS_DEPENDENCY_FAILURE, MAAGE_CONTEXT_RENDER, MAAGE_OPERATION_PRESENT, MAAGE_STATUS_INVALID_STATE);
    if (!preparing && ([window isMiniaturized] || !([window occlusionState] & NSWindowOcclusionStateVisible)))
        return MAAGE_STATUS_VALUE(MAAGE_STATUS_NOOP, MAAGE_CONTEXT_RENDER, MAAGE_OPERATION_PRESENT, 0);
    pool = [[NSAutoreleasePool alloc] init];
    for (layer = 0; layer < 3; ++layer) {
        batch = layers[layer];
        if (!batch || !batch->index_count) continue;
        if (sizeof(unsigned int) != 4 || sizeof(maage_render_ui_vertex) != 32) {
            [pool drain]; return MAAGE_STATUS_VALUE(MAAGE_STATUS_NOT_IMPLEMENTED, MAAGE_CONTEXT_RENDER, MAAGE_OPERATION_PRESENT, 0);
        }
        if (!native->pipeline) {
            result = maage_metal_pipeline(renderer);
            if (maage_status_code(result) != MAAGE_STATUS_SUCCESS) { [pool drain]; return result; }
        }
        result = maage_metal_ui_buffer(native, &native->ui_vertices[layer],
            batch->vertex_count * sizeof(*batch->vertices));
        if (maage_status_code(result) == MAAGE_STATUS_SUCCESS)
            result = maage_metal_ui_buffer(native, &native->ui_indices[layer],
                batch->index_count * sizeof(*batch->indices));
        if (maage_status_code(result) != MAAGE_STATUS_SUCCESS) { [pool drain]; return result; }
        /* The existing presentation path completes its command before returning,
         * so these persistent buffers can be reused without another frame wait. */
        memcpy([native->ui_vertices[layer] contents], batch->vertices, batch->vertex_count * sizeof(*batch->vertices));
        memcpy([native->ui_indices[layer] contents], batch->indices, batch->index_count * sizeof(*batch->indices));
    }
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
    if (!drawable) { [pool drain]; return MAAGE_STATUS_VALUE(MAAGE_STATUS_NOOP, MAAGE_CONTEXT_RENDER, MAAGE_OPERATION_PRESENT, 0); }
    if (count && (!native->depth || native->depth.width != drawable.texture.width ||
        native->depth.height != drawable.texture.height)) {
        MTLTextureDescriptor *descriptor = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:
            MTLPixelFormatDepth32Float width:drawable.texture.width height:drawable.texture.height mipmapped:NO];
        [native->depth release];
        descriptor.storageMode = MTLStorageModePrivate;
        descriptor.usage = MTLTextureUsageRenderTarget;
        native->depth = [native->device newTextureWithDescriptor:descriptor];
        if (!native->depth) { [pool drain]; return MAAGE_STATUS_VALUE(MAAGE_STATUS_ALLOCATION_FAILED, MAAGE_CONTEXT_RENDER, MAAGE_OPERATION_PRESENT, 0); }
    }
    command = [native->queue commandBuffer];
    pass = [MTLRenderPassDescriptor renderPassDescriptor];
    pass.colorAttachments[0].texture = [drawable texture];
    pass.colorAttachments[0].loadAction = MTLLoadActionClear;
    pass.colorAttachments[0].storeAction = MTLStoreActionStore;
    pass.colorAttachments[0].clearColor = MTLClearColorMake(renderer->clear[0],
        renderer->clear[1], renderer->clear[2], renderer->clear[3]);
    if (!command || !pass) { [pool drain]; return MAAGE_STATUS_VALUE(MAAGE_STATUS_ALLOCATION_FAILED, MAAGE_CONTEXT_RENDER, MAAGE_OPERATION_PRESENT, 0); }
    if (layers[0] && layers[0]->index_count) {
        float logical_size[2] = {layers[0]->logical_width, layers[0]->logical_height};
        encoder = [command renderCommandEncoderWithDescriptor:pass];
        if (!encoder) { [pool drain]; return MAAGE_STATUS_VALUE(MAAGE_STATUS_ERROR, MAAGE_CONTEXT_RENDER, MAAGE_OPERATION_PRESENT, 0); }
        [encoder setRenderPipelineState:native->ui_pipeline];
        [encoder setCullMode:MTLCullModeNone];
        [encoder setVertexBuffer:native->ui_vertices[0] offset:0 atIndex:0];
        [encoder setVertexBytes:logical_size length:sizeof(logical_size) atIndex:1];
        [encoder setFragmentTexture:(id<MTLTexture>)layers[0]->texture->platform atIndex:0];
        [encoder drawIndexedPrimitives:MTLPrimitiveTypeTriangle indexCount:layers[0]->index_count
            indexType:MTLIndexTypeUInt32 indexBuffer:native->ui_indices[0] indexBufferOffset:0];
        [encoder endEncoding]; pass.colorAttachments[0].loadAction = MTLLoadActionLoad;
    }
    if (count) {
        pass.depthAttachment.texture = native->depth;
        pass.depthAttachment.loadAction = MTLLoadActionClear;
        pass.depthAttachment.storeAction = MTLStoreActionDontCare;
        pass.depthAttachment.clearDepth = 1.0;
    }
    encoder = [command renderCommandEncoderWithDescriptor:pass];
    if (!encoder) { [pool drain]; return MAAGE_STATUS_VALUE(MAAGE_STATUS_ERROR, MAAGE_CONTEXT_RENDER, MAAGE_OPERATION_PRESENT, 0); }
    if (count) {
        MTLViewport viewport = {0, 0, drawable.texture.width, drawable.texture.height, 0, 1};
        [encoder setViewport:viewport];
        [encoder setRenderPipelineState:native->pipeline];
        [encoder setDepthStencilState:native->depth_state];
        [encoder setCullMode:MTLCullModeBack];
        [encoder setFrontFacingWinding:MTLWindingCounterClockwise];
        [encoder setVertexBytes:view_projection length:16 * sizeof(float) atIndex:1];
        [encoder setFragmentBytes:&renderer->sky_light length:sizeof(float) atIndex:0];
        for (index = 0; index < count; ++index) {
            geometry = meshes[index]->platform;
            [encoder setVertexBuffer:geometry->vertices offset:0 atIndex:0];
            [encoder setFragmentTexture:(id<MTLTexture>)meshes[index]->texture->platform atIndex:0];
            [encoder drawIndexedPrimitives:MTLPrimitiveTypeTriangle indexCount:geometry->index_count
                indexType:MTLIndexTypeUInt32 indexBuffer:geometry->indices indexBufferOffset:0];
        }
    }
    [encoder endEncoding];
    if ((layers[1] && layers[1]->index_count) || (layers[2] && layers[2]->index_count)) {
        MTLViewport viewport = {0, 0, drawable.texture.width, drawable.texture.height, 0, 1};
        pass = [MTLRenderPassDescriptor renderPassDescriptor];
        pass.colorAttachments[0].texture = drawable.texture;
        pass.colorAttachments[0].loadAction = MTLLoadActionLoad;
        pass.colorAttachments[0].storeAction = MTLStoreActionStore;
        encoder = [command renderCommandEncoderWithDescriptor:pass];
        if (!encoder) { [pool drain]; return MAAGE_STATUS_VALUE(MAAGE_STATUS_ERROR, MAAGE_CONTEXT_RENDER, MAAGE_OPERATION_PRESENT, 0); }
        [encoder setViewport:viewport]; [encoder setRenderPipelineState:native->ui_pipeline];
        [encoder setDepthStencilState:nil]; [encoder setCullMode:MTLCullModeNone];
        for (layer = 1; layer < 3; ++layer) {
            float logical_size[2];
            batch = layers[layer]; if (!batch || !batch->index_count) continue;
            logical_size[0] = batch->logical_width; logical_size[1] = batch->logical_height;
            [encoder setVertexBuffer:native->ui_vertices[layer] offset:0 atIndex:0];
            [encoder setVertexBytes:logical_size length:sizeof(logical_size) atIndex:1];
            [encoder setFragmentTexture:(id<MTLTexture>)batch->texture->platform atIndex:0];
            [encoder drawIndexedPrimitives:MTLPrimitiveTypeTriangle indexCount:batch->index_count
                indexType:MTLIndexTypeUInt32 indexBuffer:native->ui_indices[layer] indexBufferOffset:0];
        }
        [encoder endEncoding];
    }
    if (!preparing) [command presentDrawable:drawable];
    [command commit];
    [command waitUntilCompleted];
    result = [command status] == MTLCommandBufferStatusError ? MAAGE_STATUS_VALUE(MAAGE_STATUS_ERROR, MAAGE_CONTEXT_RENDER, MAAGE_OPERATION_PRESENT, 0) : MAAGE_STATUS_VALUE(MAAGE_STATUS_SUCCESS, MAAGE_CONTEXT_RENDER, MAAGE_OPERATION_PRESENT, 0);
    if (preparing && maage_status_code(result) == MAAGE_STATUS_SUCCESS) {
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
static maage_status maage_metal_present(maage_renderer *renderer, const maage_window_state *state)
{
    return maage_metal_frame(renderer, state, 0, NULL, 0, NULL, NULL);
}
static maage_status maage_metal_prepare_frame(maage_renderer *renderer, const maage_window_state *state)
{
    return maage_metal_frame(renderer, state, 1, NULL, 0, NULL, NULL);
}
static maage_status maage_metal_create_texture(maage_renderer *renderer,
    maage_render_texture *resource, const maage_pixel_buffer *atlas)
{
    maage_metal_renderer *native = renderer->platform;
    MTLTextureDescriptor *descriptor;
    id<MTLTexture> texture;
    NSAutoreleasePool *pool;
    if (![NSThread isMainThread]) return MAAGE_STATUS_VALUE(MAAGE_STATUS_DEPENDENCY_FAILURE, MAAGE_CONTEXT_RENDER, MAAGE_OPERATION_CREATE, MAAGE_STATUS_INVALID_STATE);
    pool = [[NSAutoreleasePool alloc] init];
    descriptor = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA8Unorm
        width:atlas->width height:atlas->height mipmapped:NO];
    descriptor.usage = MTLTextureUsageShaderRead;
    descriptor.storageMode = MTLStorageModeManaged;
    if (@available(macOS 10.15, *)) {
        if ([native->device supportsFamily:MTLGPUFamilyApple1])
            descriptor.storageMode = MTLStorageModeShared;
    }
    texture = [native->device newTextureWithDescriptor:descriptor];
    if (texture)
        [texture replaceRegion:MTLRegionMake2D(0, 0, atlas->width, atlas->height)
            mipmapLevel:0 withBytes:atlas->pixels bytesPerRow:atlas->stride];
    resource->platform = texture;
    [pool drain];
    return texture ? MAAGE_STATUS_VALUE(MAAGE_STATUS_SUCCESS, MAAGE_CONTEXT_RENDER, MAAGE_OPERATION_CREATE, 0) : MAAGE_STATUS_VALUE(MAAGE_STATUS_ALLOCATION_FAILED, MAAGE_CONTEXT_RENDER, MAAGE_OPERATION_CREATE, 0);
}
static void maage_metal_destroy_texture(maage_render_texture *texture)
{
    [(id<MTLTexture>)texture->platform release];
    texture->platform = NULL;
}
static maage_status maage_metal_upload_geometry(maage_renderer *renderer, maage_render_mesh *mesh,
    const maage_mesh_data *data)
{
    maage_metal_renderer *native = renderer->platform;
    maage_metal_mesh *geometry;
    NSAutoreleasePool *pool;
    maage_status result;
    if (![NSThread isMainThread]) return MAAGE_STATUS_VALUE(MAAGE_STATUS_DEPENDENCY_FAILURE, MAAGE_CONTEXT_RENDER, MAAGE_OPERATION_UPLOAD, MAAGE_STATUS_INVALID_STATE);
    if (sizeof(unsigned int) != 4 || sizeof(maage_mesh_vertex) != 56)
        return MAAGE_STATUS_VALUE(MAAGE_STATUS_NOT_IMPLEMENTED, MAAGE_CONTEXT_RENDER, MAAGE_OPERATION_UPLOAD, 0);
    pool = [[NSAutoreleasePool alloc] init];
    /* Clear-only renderers do not need the mesh shaders or depth state. */
    if (!native->pipeline) {
        result = maage_metal_pipeline(renderer);
        if (maage_status_code(result) != MAAGE_STATUS_SUCCESS) { [pool drain]; return result; }
    }
    geometry = calloc(1, sizeof(*geometry));
    if (!geometry) { [pool drain]; return MAAGE_STATUS_VALUE(MAAGE_STATUS_ALLOCATION_FAILED, MAAGE_CONTEXT_RENDER, MAAGE_OPERATION_UPLOAD, 0); }
    mesh->platform = geometry;
    geometry->vertices = [native->device newBufferWithBytes:data->vertices
        length:data->vertex_count * sizeof(*data->vertices) options:MTLResourceStorageModeShared];
    geometry->indices = [native->device newBufferWithBytes:data->indices
        length:data->index_count * sizeof(*data->indices) options:MTLResourceStorageModeShared];
    geometry->index_count = data->index_count;
    [pool drain];
    return geometry->vertices && geometry->indices ? MAAGE_STATUS_VALUE(MAAGE_STATUS_SUCCESS, MAAGE_CONTEXT_RENDER, MAAGE_OPERATION_UPLOAD, 0) : MAAGE_STATUS_VALUE(MAAGE_STATUS_ALLOCATION_FAILED, MAAGE_CONTEXT_RENDER, MAAGE_OPERATION_UPLOAD, 0);
}
static void maage_metal_destroy_mesh(maage_render_mesh *mesh)
{
    maage_metal_mesh *geometry = mesh->platform;
    if (!geometry) return;
    [geometry->vertices release];
    [geometry->indices release];
    free(geometry);
    mesh->platform = NULL;
}
static maage_status maage_metal_draw_meshes(maage_renderer *renderer,
    const maage_render_mesh *const *meshes, size_t count,
    const maage_window_state *state, const float view_projection[16])
{
    return maage_metal_frame(renderer, state, 0, meshes, count, view_projection, NULL);
}
static maage_status maage_metal_draw_frame(maage_renderer *renderer,
    const maage_render_mesh *const *meshes, size_t count,
    const maage_window_state *state, const float view_projection[16],
    const maage_render_ui_data *ui)
{
    return maage_metal_frame(renderer, state, 0, meshes, count, view_projection, ui);
}
const maage_render_backend_api maage_render_metal_api = {
    MAAGE_RENDER_METAL, maage_metal_create, maage_metal_destroy, maage_metal_present,
    MAAGE_RENDER_CAPABILITIES_VERSION, maage_metal_prepare_frame, maage_metal_upload_geometry,
    maage_metal_destroy_mesh, maage_metal_draw_meshes,
    maage_metal_create_texture, maage_metal_destroy_texture, maage_metal_draw_frame
};
