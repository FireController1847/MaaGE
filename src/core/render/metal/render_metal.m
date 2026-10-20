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
    id<MTLBuffer> ui_vertices;
    id<MTLBuffer> ui_indices;
} maage_metal_renderer;
typedef struct maage_metal_mesh {
    id<MTLBuffer> vertices;
    id<MTLBuffer> indices;
    NSUInteger index_count;
} maage_metal_mesh;

static id<MTLRenderPipelineState> maage_metal_compile_pipeline(
    maage_metal_renderer *native, id<MTLLibrary> library, int ui)
{
    id<MTLFunction> vertex, fragment;
    id<MTLRenderPipelineState> compiled;
    MTLRenderPipelineDescriptor *pipeline;
    NSError *error = nil;
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
    if (!compiled) maage_log_error("maage.render.metal", "%s pipeline unavailable: %s",
        ui ? "UI" : "Mesh", error ? [[error localizedDescription] UTF8String] : "missing shader function");
    return compiled;
}
static maage_render_result maage_metal_pipeline(maage_metal_renderer *native)
{
    NSString *source;
    id<MTLLibrary> library;
    id<MTLRenderPipelineState> compiled, ui_pipeline;
    id<MTLDepthStencilState> depth_state;
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
    compiled = maage_metal_compile_pipeline(native, library, 0);
    ui_pipeline = compiled ? maage_metal_compile_pipeline(native, library, 1) : nil;
    [library release];
    if (!compiled || !ui_pipeline) {
        [compiled release];
        [ui_pipeline release];
        return MAAGE_RENDER_PLATFORM_ERROR;
    }
    depth = [[MTLDepthStencilDescriptor alloc] init];
    depth.depthCompareFunction = MTLCompareFunctionLessEqual;
    depth.depthWriteEnabled = YES;
    depth_state = depth ? [native->device newDepthStencilStateWithDescriptor:depth] : nil;
    [depth release];
    if (!depth_state) {
        [compiled release];
        [ui_pipeline release];
        return MAAGE_RENDER_OUT_OF_MEMORY;
    }
    native->pipeline = compiled;
    native->ui_pipeline = ui_pipeline;
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
    if ([native->device name]) {
        strncpy(renderer->device_name, [[native->device name] UTF8String], sizeof(renderer->device_name) - 1);
        renderer->device_name[sizeof(renderer->device_name) - 1] = 0;
    }
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
        MAAGE_RENDER_CAP_TEXTURED_MESH | MAAGE_RENDER_CAP_UI;
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
    [native->ui_pipeline release];
    [native->ui_vertices release];
    [native->ui_indices release];
    [native->queue release];
    [native->device release];
    free(native);
}
static maage_render_result maage_metal_ui_buffer(maage_metal_renderer *native,
    id<MTLBuffer> *buffer, size_t bytes)
{
    id<MTLBuffer> replacement;
    size_t capacity;
    if (*buffer && [*buffer length] >= bytes) return MAAGE_RENDER_OK;
    capacity = *buffer ? [*buffer length] : 4096;
    while (capacity < bytes) {
        if (capacity > (size_t)-1 / 2) { capacity = bytes; break; }
        capacity *= 2;
    }
    replacement = [native->device newBufferWithLength:capacity options:MTLResourceStorageModeShared];
    if (!replacement) return MAAGE_RENDER_OUT_OF_MEMORY;
    [*buffer release];
    *buffer = replacement;
    return MAAGE_RENDER_OK;
}
static maage_render_result maage_metal_frame(maage_renderer *renderer, const maage_window_state *state,
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
    maage_render_result result;
    maage_metal_mesh *geometry;
    size_t index;
    if (![NSThread isMainThread]) return MAAGE_RENDER_WINDOW_ERROR;
    if (!preparing && ([window isMiniaturized] || !([window occlusionState] & NSWindowOcclusionStateVisible)))
        return MAAGE_RENDER_SKIPPED;
    pool = [[NSAutoreleasePool alloc] init];
    if (ui && ui->index_count) {
        if (sizeof(unsigned int) != 4 || sizeof(maage_render_ui_vertex) != 32) {
            [pool drain]; return MAAGE_RENDER_UNSUPPORTED;
        }
        if (!native->pipeline) {
            result = maage_metal_pipeline(native);
            if (result != MAAGE_RENDER_OK) { [pool drain]; return result; }
        }
        result = maage_metal_ui_buffer(native, &native->ui_vertices,
            ui->vertex_count * sizeof(*ui->vertices));
        if (result == MAAGE_RENDER_OK)
            result = maage_metal_ui_buffer(native, &native->ui_indices,
                ui->index_count * sizeof(*ui->indices));
        if (result != MAAGE_RENDER_OK) { [pool drain]; return result; }
        /* The existing presentation path completes its command before returning,
         * so these persistent buffers can be reused without another frame wait. */
        memcpy([native->ui_vertices contents], ui->vertices, ui->vertex_count * sizeof(*ui->vertices));
        memcpy([native->ui_indices contents], ui->indices, ui->index_count * sizeof(*ui->indices));
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
    if (!drawable) { [pool drain]; return MAAGE_RENDER_SKIPPED; }
    if (count && (!native->depth || native->depth.width != drawable.texture.width ||
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
    if (count) {
        pass.depthAttachment.texture = native->depth;
        pass.depthAttachment.loadAction = MTLLoadActionClear;
        pass.depthAttachment.storeAction = MTLStoreActionDontCare;
        pass.depthAttachment.clearDepth = 1.0;
    }
    encoder = [command renderCommandEncoderWithDescriptor:pass];
    if (!encoder) { [pool drain]; return MAAGE_RENDER_PLATFORM_ERROR; }
    if (count) {
        MTLViewport viewport = {0, 0, drawable.texture.width, drawable.texture.height, 0, 1};
        [encoder setViewport:viewport];
        [encoder setRenderPipelineState:native->pipeline];
        [encoder setDepthStencilState:native->depth_state];
        [encoder setCullMode:MTLCullModeBack];
        [encoder setFrontFacingWinding:MTLWindingCounterClockwise];
        [encoder setVertexBytes:view_projection length:16 * sizeof(float) atIndex:1];
        for (index = 0; index < count; ++index) {
            geometry = meshes[index]->platform;
            [encoder setVertexBuffer:geometry->vertices offset:0 atIndex:0];
            [encoder setFragmentTexture:(id<MTLTexture>)meshes[index]->texture->platform atIndex:0];
            [encoder drawIndexedPrimitives:MTLPrimitiveTypeTriangle indexCount:geometry->index_count
                indexType:MTLIndexTypeUInt32 indexBuffer:geometry->indices indexBufferOffset:0];
        }
    }
    [encoder endEncoding];
    if (ui && ui->index_count) {
        float logical_size[2] = {ui->logical_width, ui->logical_height};
        MTLViewport viewport = {0, 0, drawable.texture.width, drawable.texture.height, 0, 1};
        /* A second pass loads the World color without attaching its depth buffer;
         * both passes share the command buffer and the single presentation. */
        pass = [MTLRenderPassDescriptor renderPassDescriptor];
        pass.colorAttachments[0].texture = drawable.texture;
        pass.colorAttachments[0].loadAction = MTLLoadActionLoad;
        pass.colorAttachments[0].storeAction = MTLStoreActionStore;
        encoder = [command renderCommandEncoderWithDescriptor:pass];
        if (!encoder) { [pool drain]; return MAAGE_RENDER_PLATFORM_ERROR; }
        [encoder setViewport:viewport];
        [encoder setRenderPipelineState:native->ui_pipeline];
        [encoder setDepthStencilState:nil];
        [encoder setCullMode:MTLCullModeNone];
        [encoder setVertexBuffer:native->ui_vertices offset:0 atIndex:0];
        [encoder setVertexBytes:logical_size length:sizeof(logical_size) atIndex:1];
        [encoder setFragmentTexture:(id<MTLTexture>)ui->texture->platform atIndex:0];
        [encoder drawIndexedPrimitives:MTLPrimitiveTypeTriangle indexCount:ui->index_count
            indexType:MTLIndexTypeUInt32 indexBuffer:native->ui_indices indexBufferOffset:0];
        [encoder endEncoding];
    }
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
    return maage_metal_frame(renderer, state, 0, NULL, 0, NULL, NULL);
}
static maage_render_result maage_metal_prepare_frame(maage_renderer *renderer, const maage_window_state *state)
{
    return maage_metal_frame(renderer, state, 1, NULL, 0, NULL, NULL);
}
static maage_render_result maage_metal_create_texture(maage_renderer *renderer,
    maage_render_texture *resource, const maage_pixel_buffer *atlas)
{
    maage_metal_renderer *native = renderer->platform;
    MTLTextureDescriptor *descriptor;
    id<MTLTexture> texture;
    NSAutoreleasePool *pool;
    if (![NSThread isMainThread]) return MAAGE_RENDER_WINDOW_ERROR;
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
    return texture ? MAAGE_RENDER_OK : MAAGE_RENDER_OUT_OF_MEMORY;
}
static void maage_metal_destroy_texture(maage_render_texture *texture)
{
    [(id<MTLTexture>)texture->platform release];
    texture->platform = NULL;
}
static maage_render_result maage_metal_upload_geometry(maage_renderer *renderer, maage_render_mesh *mesh,
    const maage_mesh_data *data)
{
    maage_metal_renderer *native = renderer->platform;
    maage_metal_mesh *geometry;
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
    [pool drain];
    return geometry->vertices && geometry->indices ? MAAGE_RENDER_OK : MAAGE_RENDER_OUT_OF_MEMORY;
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
static maage_render_result maage_metal_draw_meshes(maage_renderer *renderer,
    const maage_render_mesh *const *meshes, size_t count,
    const maage_window_state *state, const float view_projection[16])
{
    return maage_metal_frame(renderer, state, 0, meshes, count, view_projection, NULL);
}
static maage_render_result maage_metal_draw_frame(maage_renderer *renderer,
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
