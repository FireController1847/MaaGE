#import <AppKit/AppKit.h>
#import <Metal/Metal.h>
#import <QuartzCore/CAMetalLayer.h>
#import <QuartzCore/CATransaction.h>
#include "../render_internal.h"
#include "maage/log.h"
#include "maage/thread.h"
#include "maage_mesh.h"
#include <stdlib.h>
#include <string.h>

#define MAAGE_METAL_FRAME_SLOTS 4

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
    id<MTLBuffer> ui_vertices[MAAGE_METAL_FRAME_SLOTS][3];
    id<MTLBuffer> ui_indices[MAAGE_METAL_FRAME_SLOTS][3];
    id<MTLCommandBuffer> frames[MAAGE_METAL_FRAME_SLOTS];
    id<MTLTexture> colors[MAAGE_METAL_FRAME_SLOTS];
    id<MTLCommandQueue> present_queue;
    maage_mutex *present_mutex;
    maage_condition *present_condition;
    maage_thread *present_thread;
    unsigned long sequence[MAAGE_METAL_FRAME_SLOTS], submitted, presented;
    int asynchronous, stopping, presenting;
    maage_status present_result;
    maage_status_trace frame_traces[MAAGE_METAL_FRAME_SLOTS], present_trace;
    unsigned int frame_slot;
    double sample_start, drawable_seconds, wait_seconds, gpu_seconds;
    unsigned long sample_frames, gpu_samples;
} maage_metal_renderer;
typedef struct maage_metal_mesh {
    id<MTLBuffer> vertices;
    id<MTLBuffer> indices;
    NSUInteger index_count;
} maage_metal_mesh;

/* Only the cached layer and immutable Metal objects cross this boundary.
 * NSWindow/NSView and all geometry changes remain on the main thread. */
static void maage_metal_present_loop(void *context)
{
    maage_metal_renderer *native = context;
    for (;;) {
        NSAutoreleasePool *pool = [[NSAutoreleasePool alloc] init];
        id<MTLCommandBuffer> rendered = nil, command = nil;
        id<CAMetalDrawable> drawable;
        id<MTLTexture> color, previous_color = nil;
        id<MTLBlitCommandEncoder> copy;
        unsigned int slot, newest = 0;
        unsigned long sequence = 0, pending = 0;
        unsigned int pending_slot = 0;
        maage_status_trace trace;
        double acquired, waited;
        maage_status result = maage_status_make(MAAGE_STATUS_SUCCESS, 0, MAAGE_OPERATION_PRESENT, 0);
        long native_error = 0;
        int cancelled;
        maage_mutex_lock(native->present_mutex);
        for (;;) {
            for (slot = 0; slot < MAAGE_METAL_FRAME_SLOTS; ++slot) {
                if (native->sequence[slot] > pending) {
                    pending = native->sequence[slot]; pending_slot = slot;
                }
                if (native->sequence[slot] > sequence &&
                    ([native->frames[slot] status] == MTLCommandBufferStatusCompleted ||
                     [native->frames[slot] status] == MTLCommandBufferStatusError)) {
                    sequence = native->sequence[slot]; newest = slot;
                }
            }
            if (native->stopping || pending) break;
            maage_condition_wait(native->present_condition, native->present_mutex);
        }
        if (native->stopping) {
            maage_mutex_unlock(native->present_mutex); [pool drain]; break;
        }
        if (!sequence) {
            /* GPU completion, never drawable availability, may delay selection.
             * Retaining the command lets the owner recycle unrelated slots. */
            rendered = [native->frames[pending_slot] retain];
            maage_mutex_unlock(native->present_mutex);
            [rendered waitUntilCompleted]; [rendered release]; [pool drain]; continue;
        }
        rendered = [native->frames[newest] retain];
        trace = native->frame_traces[newest];
        if ([rendered status] == MTLCommandBufferStatusError) {
            native_error = (long)[[rendered error] code];
            native->present_result = maage_status_trace_record(&trace,
                maage_status_make(MAAGE_STATUS_ERROR, MAAGE_CONTEXT_RENDER, MAAGE_OPERATION_PRESENT, 0),
                MAAGE_CONTEXT_RENDER, MAAGE_OPERATION_PRESENT, __FILE__, __LINE__, native_error);
            native->present_trace = trace;
            native->stopping = 1;
            maage_mutex_unlock(native->present_mutex);
            [rendered release]; [pool drain]; break;
        }
        native->presenting = (int)newest;
        color = [native->colors[newest] retain];
        for (slot = 0; slot < MAAGE_METAL_FRAME_SLOTS; ++slot)
            if (native->sequence[slot] <= sequence) native->sequence[slot] = 0;
        maage_mutex_unlock(native->present_mutex);
        [rendered release];
        acquired = CACurrentMediaTime();
        drawable = [native->layer nextDrawable];
        waited = CACurrentMediaTime() - acquired;
        /* Drawable acquisition can span a refresh. Use the newest completed
         * image now available, rather than the image selected before the wait. */
        maage_mutex_lock(native->present_mutex);
        cancelled = native->stopping;
        if (!cancelled && drawable) {
            for (slot = 0; slot < MAAGE_METAL_FRAME_SLOTS; ++slot)
                if (native->sequence[slot] > sequence &&
                    [native->frames[slot] status] == MTLCommandBufferStatusCompleted) {
                    sequence = native->sequence[slot]; newest = slot;
                }
            if ((int)newest != native->presenting) {
                previous_color = color;
                color = [native->colors[newest] retain];
                trace = native->frame_traces[newest];
                native->presenting = (int)newest;
                for (slot = 0; slot < MAAGE_METAL_FRAME_SLOTS; ++slot)
                    if (native->sequence[slot] <= sequence) native->sequence[slot] = 0;
            }
        }
        maage_mutex_unlock(native->present_mutex);
        [previous_color release];
        if (!cancelled && drawable && color.width == drawable.texture.width && color.height == drawable.texture.height) {
            command = [native->present_queue commandBuffer];
            copy = [command blitCommandEncoder];
            if (command && copy) {
                [copy copyFromTexture:color sourceSlice:0 sourceLevel:0 sourceOrigin:MTLOriginMake(0, 0, 0)
                    sourceSize:MTLSizeMake(color.width, color.height, 1) toTexture:drawable.texture
                    destinationSlice:0 destinationLevel:0 destinationOrigin:MTLOriginMake(0, 0, 0)];
                [copy endEncoding];
                [command presentDrawable:drawable];
                [command commit];
                /* Keep this color slot immutable through the GPU copy. */
                [command waitUntilCompleted];
                if ([command status] == MTLCommandBufferStatusError) {
                    result = maage_status_make(MAAGE_STATUS_ERROR, MAAGE_CONTEXT_RENDER, MAAGE_OPERATION_PRESENT, 0);
                    native_error = (long)[[command error] code];
                }
            } else result = maage_status_make(MAAGE_STATUS_ALLOCATION_FAILED,
                MAAGE_CONTEXT_RENDER, MAAGE_OPERATION_PRESENT, 0);
        }
        [color release];
        maage_mutex_lock(native->present_mutex);
        native->drawable_seconds += waited;
        if (command && !maage_status_failed(result)) ++native->presented;
        native->presenting = -1;
        if (maage_status_failed(result)) {
            native->present_result = maage_status_trace_record(&trace, result,
                MAAGE_CONTEXT_RENDER, MAAGE_OPERATION_PRESENT, __FILE__, __LINE__, native_error);
            native->present_trace = trace; native->stopping = 1;
        }
        maage_mutex_unlock(native->present_mutex);
        [pool drain];
    }
}

static maage_status maage_metal_stop_presenter(maage_metal_renderer *native)
{
    maage_status result;
    if (!native->present_thread) return maage_status_make(MAAGE_STATUS_NOOP, 0, MAAGE_OPERATION_STOP, 0);
    maage_mutex_lock(native->present_mutex);
    native->stopping = 1;
    maage_condition_signal(native->present_condition);
    maage_mutex_unlock(native->present_mutex);
    result = maage_thread_join(native->present_thread);
    if (!maage_status_failed(result)) {
        native->present_thread = NULL; native->asynchronous = 0;
        memset(native->sequence, 0, sizeof(native->sequence));
    }
    return result;
}

static maage_status maage_metal_present_result(maage_renderer *renderer)
{
    maage_metal_renderer *native = renderer->platform;
    maage_status result;
    maage_mutex_lock(native->present_mutex);
    result = native->present_result;
    if (maage_status_failed(result) && renderer->operation_trace)
        *renderer->operation_trace = native->present_trace;
    maage_mutex_unlock(native->present_mutex);
    return result;
}

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
    maage_status result;
    if (![NSThread isMainThread]) return MAAGE_STATUS_VALUE(MAAGE_STATUS_DEPENDENCY_FAILURE, MAAGE_CONTEXT_RENDER, MAAGE_OPERATION_INITIALIZE, MAAGE_STATUS_INVALID_STATE);
    if (renderer->native.backend != MAAGE_WINDOW_COCOA) return MAAGE_STATUS_VALUE(MAAGE_STATUS_NOT_IMPLEMENTED, MAAGE_CONTEXT_RENDER, MAAGE_OPERATION_INITIALIZE, 0);
    native = calloc(1, sizeof(*native));
    if (!native) return MAAGE_STATUS_VALUE(MAAGE_STATUS_ALLOCATION_FAILED, MAAGE_CONTEXT_RENDER, MAAGE_OPERATION_INITIALIZE, 0);
    renderer->platform = native;
    native->presenting = -1;
    result = maage_mutex_create(&native->present_mutex);
    if (maage_status_failed(result)) return result;
    result = maage_condition_create(&native->present_condition);
    if (maage_status_failed(result)) return result;
    native->device = MTLCreateSystemDefaultDevice();
    if (!native->device) return MAAGE_STATUS_VALUE(MAAGE_STATUS_SYSTEM_NOT_SUPPORTED, MAAGE_CONTEXT_RENDER, MAAGE_OPERATION_INITIALIZE, 0);
    if ([native->device name]) {
        strncpy(renderer->device_name, [[native->device name] UTF8String], sizeof(renderer->device_name) - 1);
        renderer->device_name[sizeof(renderer->device_name) - 1] = 0;
    }
    native->queue = [native->device newCommandQueue];
    if (!native->queue) return MAAGE_STATUS_VALUE(MAAGE_STATUS_ALLOCATION_FAILED, MAAGE_CONTEXT_RENDER, MAAGE_OPERATION_INITIALIZE, 0);
    native->present_queue = [native->device newCommandQueue];
    if (!native->present_queue) return MAAGE_STATUS_VALUE(MAAGE_STATUS_ALLOCATION_FAILED, MAAGE_CONTEXT_RENDER, MAAGE_OPERATION_INITIALIZE, 0);
    native->view = renderer->native.handles.cocoa.view;
    if (!native->view) return MAAGE_STATUS_VALUE(MAAGE_STATUS_DEPENDENCY_FAILURE, MAAGE_CONTEXT_RENDER, MAAGE_OPERATION_INITIALIZE, MAAGE_STATUS_INVALID_STATE);
    native->layer = [[CAMetalLayer alloc] init];
    if (!native->layer) return MAAGE_STATUS_VALUE(MAAGE_STATUS_ALLOCATION_FAILED, MAAGE_CONTEXT_RENDER, MAAGE_OPERATION_INITIALIZE, 0);
    native->layer.device = native->device;
    native->layer.pixelFormat = MTLPixelFormatBGRA8Unorm;
    native->layer.framebufferOnly = YES;
    native->layer.opaque = YES;
    native->layer.displaySyncEnabled = YES;
    native->layer.maximumDrawableCount = 3;
    native->layer.allowsNextDrawableTimeout = YES;
    native->layer.autoresizingMask = kCALayerWidthSizable | kCALayerHeightSizable;
    native->previous_background = [[(NSWindow *)renderer->native.handles.cocoa.window backgroundColor] retain];
    native->previous_wants_layer = [native->view wantsLayer];
    native->previous_layer = [[native->view layer] retain];
    [native->view setLayer:native->layer];
    [native->view setWantsLayer:YES];
    native->installed = 1;
    renderer->capabilities.features |= MAAGE_RENDER_CAP_SHADERS | MAAGE_RENDER_CAP_VERTEX_BUFFERS |
        MAAGE_RENDER_CAP_INDEX_BUFFERS | MAAGE_RENDER_CAP_RENDER_TARGETS | MAAGE_RENDER_CAP_DEPTH |
        MAAGE_RENDER_CAP_STENCIL | MAAGE_RENDER_CAP_INSTANCING | MAAGE_RENDER_CAP_COMPUTE |
        MAAGE_RENDER_CAP_TEXTURED_MESH | MAAGE_RENDER_CAP_UI | MAAGE_RENDER_CAP_SCENE;
    renderer->capabilities.texture_formats = MAAGE_RENDER_FORMAT_RGBA8 | MAAGE_RENDER_FORMAT_BGRA8;
    renderer->capabilities.synchronization = MAAGE_RENDER_SYNC_CPU_WAIT;
    renderer->capabilities.max_frames_in_flight = MAAGE_METAL_FRAME_SLOTS;
    /* Texture limits vary by GPU family; leave unknown until resource creation
     * needs family-specific queries rather than promising an unqueried limit. */
    return MAAGE_STATUS_VALUE(MAAGE_STATUS_SUCCESS, MAAGE_CONTEXT_RENDER, MAAGE_OPERATION_INITIALIZE, 0);
}
static maage_status maage_metal_complete(maage_renderer *renderer, unsigned int slot)
{
    maage_metal_renderer *native = renderer->platform;
    id<MTLCommandBuffer> command = native->frames[slot];
    maage_status result = maage_status_make(MAAGE_STATUS_SUCCESS, 0, MAAGE_OPERATION_PRESENT, 0);
    if (!command) return result;
    [command waitUntilCompleted];
    if ([command status] == MTLCommandBufferStatusError) {
        if (renderer->operation_trace) *renderer->operation_trace = native->frame_traces[slot];
        result = maage_status_trace_record(renderer->operation_trace,
            maage_status_make(MAAGE_STATUS_ERROR, MAAGE_CONTEXT_RENDER, MAAGE_OPERATION_PRESENT, 0),
            MAAGE_CONTEXT_RENDER, MAAGE_OPERATION_PRESENT, __FILE__, __LINE__, (long)[[command error] code]);
    }
    if (@available(macOS 10.15, *)) {
        if (command.GPUStartTime > 0 && command.GPUEndTime >= command.GPUStartTime) {
            native->gpu_seconds += command.GPUEndTime - command.GPUStartTime;
            ++native->gpu_samples;
        }
    }
    [command release];
    native->frames[slot] = nil;
    return result;
}
static maage_status maage_metal_destroy(maage_renderer *renderer)
{
    maage_metal_renderer *native = renderer->platform;
    maage_status result = maage_status_make(MAAGE_STATUS_SUCCESS, 0, MAAGE_OPERATION_DESTROY, 0), completed;
    maage_status_trace failure_trace;
    int have_failure_trace = 0;
    unsigned int slot;
    if (!native) return maage_status_make(MAAGE_STATUS_NOOP, 0, MAAGE_OPERATION_DESTROY, 0);
    result = maage_metal_stop_presenter(native);
    if (maage_status_failed(result)) return result;
    if (maage_status_failed(native->present_result)) {
        failure_trace = native->present_trace; have_failure_trace = 1;
        if (renderer->operation_trace) *renderer->operation_trace = failure_trace;
        result = MAAGE_STATUS_PROPAGATE(renderer->operation_trace, native->present_result,
            MAAGE_CONTEXT_RENDER, MAAGE_OPERATION_DESTROY);
    }
    for (slot = 0; slot < MAAGE_METAL_FRAME_SLOTS; ++slot) {
        completed = maage_metal_complete(renderer, slot);
        if (!maage_status_failed(result)) {
            result = completed;
            if (maage_status_failed(result) && renderer->operation_trace) {
                failure_trace = *renderer->operation_trace; have_failure_trace = 1;
            }
        }
        [native->colors[slot] release];
    }
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
        for (slot = 0; slot < MAAGE_METAL_FRAME_SLOTS; ++slot)
            for (layer = 0; layer < 3; ++layer) {
                [native->ui_vertices[slot][layer] release]; [native->ui_indices[slot][layer] release];
            }
    }
    [native->queue release];
    [native->present_queue release];
    [native->device release];
    completed = maage_condition_destroy(native->present_condition);
    if (!maage_status_failed(result)) result = completed;
    if (have_failure_trace && renderer->operation_trace) *renderer->operation_trace = failure_trace;
    completed = maage_mutex_destroy(native->present_mutex);
    if (!maage_status_failed(result)) result = completed;
    free(native);
    return MAAGE_STATUS_PROPAGATE(renderer->operation_trace, maage_status_failed(result) ? result :
        maage_status_make(MAAGE_STATUS_SUCCESS, 0, MAAGE_OPERATION_DESTROY, 0), MAAGE_CONTEXT_RENDER, MAAGE_OPERATION_DESTROY);
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
    id<MTLTexture> color;
    id<MTLCommandBuffer> command;
    id<MTLRenderCommandEncoder> encoder;
    MTLRenderPassDescriptor *pass;
    maage_status result;
    maage_metal_mesh *geometry;
    size_t index;
    const maage_render_ui_data *layers[3], *batch;
    int layer;
    CGRect frame;
    CGSize drawable_size;
    CGFloat scale;
    BOOL transaction;
    unsigned int slot = native->frame_slot;
    unsigned int candidate;
    int asynchronous = native->asynchronous && !preparing;
    double started, acquired;
    layers[0] = preparing ? NULL : renderer->background;
    layers[1] = preparing ? NULL : renderer->overlay; layers[2] = ui;
    if (![NSThread isMainThread]) return MAAGE_STATUS_VALUE(MAAGE_STATUS_DEPENDENCY_FAILURE, MAAGE_CONTEXT_RENDER, MAAGE_OPERATION_PRESENT, MAAGE_STATUS_INVALID_STATE);
    result = maage_metal_present_result(renderer);
    if (maage_status_failed(result)) return result;
    if (!preparing && ([window isMiniaturized] || !([window occlusionState] & NSWindowOcclusionStateVisible)))
        return MAAGE_STATUS_VALUE(MAAGE_STATUS_NOOP, MAAGE_CONTEXT_RENDER, MAAGE_OPERATION_PRESENT, 0);
    pool = [[NSAutoreleasePool alloc] init];
    started = CACurrentMediaTime();
    if (asynchronous) {
        maage_mutex_lock(native->present_mutex);
        result = native->present_result;
        if (!maage_status_failed(result)) {
            /* One presenter leases at most one slot. Other slots can be
             * recycled after rendering completes, regardless of display pace. */
            for (candidate = 0; candidate < MAAGE_METAL_FRAME_SLOTS; ++candidate) {
                slot = (native->frame_slot + candidate) % MAAGE_METAL_FRAME_SLOTS;
                if ((int)slot != native->presenting) break;
            }
            native->sequence[slot] = 0;
        } else if (renderer->operation_trace) *renderer->operation_trace = native->present_trace;
        maage_mutex_unlock(native->present_mutex);
        if (maage_status_failed(result)) { [pool drain]; return result; }
    }
    result = maage_metal_complete(renderer, slot);
    native->wait_seconds += CACurrentMediaTime() - started;
    if (maage_status_failed(result)) { [pool drain]; return result; }
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
        result = maage_metal_ui_buffer(native, &native->ui_vertices[slot][layer],
            batch->vertex_count * sizeof(*batch->vertices));
        if (maage_status_code(result) == MAAGE_STATUS_SUCCESS)
            result = maage_metal_ui_buffer(native, &native->ui_indices[slot][layer],
                batch->index_count * sizeof(*batch->indices));
        if (maage_status_code(result) != MAAGE_STATUS_SUCCESS) { [pool drain]; return result; }
        /* Only this slot has completed; other frames retain their own data. */
        memcpy([native->ui_vertices[slot][layer] contents], batch->vertices, batch->vertex_count * sizeof(*batch->vertices));
        memcpy([native->ui_indices[slot][layer] contents], batch->indices, batch->index_count * sizeof(*batch->indices));
    }
    /* Use the entire content view, including permitted notch-adjacent regions.
     * Safe-area insets are for UI placement, not drawable cropping. */
    frame = NSRectToCGRect([native->view bounds]);
    scale = state->width > 0 ?
        (double)state->framebuffer_width / state->width : [window backingScaleFactor];
    drawable_size = CGSizeMake(state->framebuffer_width, state->framebuffer_height);
    transaction = preparing != 0;
    /* Reconfiguring the drawable pool every frame can stall acquisition even
     * with display sync disabled. Publish actual changes before acquisition. */
    if (preparing || native->layer.presentsWithTransaction != transaction ||
        !CGRectEqualToRect(native->layer.frame, frame) || native->layer.contentsScale != scale ||
        !CGSizeEqualToSize(native->layer.drawableSize, drawable_size)) {
        /* Geometry transitions retire the old drawable acquisition before
         * changing its pool. This wait is absent from steady-state frames. */
        if (asynchronous) {
            result = maage_metal_stop_presenter(native);
            if (!maage_status_failed(result)) result = maage_metal_present_result(renderer);
            if (maage_status_failed(result)) { [pool drain]; return result; }
        }
        [CATransaction begin];
        [CATransaction setDisableActions:YES];
        native->layer.presentsWithTransaction = transaction;
        if (preparing) {
            NSColor *color = [NSColor colorWithSRGBRed:renderer->clear[0] green:renderer->clear[1]
                blue:renderer->clear[2] alpha:renderer->clear[3]];
            [window setBackgroundColor:color];
            native->layer.backgroundColor = [color CGColor];
        }
        if (!CGRectEqualToRect(native->layer.frame, frame)) native->layer.frame = frame;
        if (native->layer.contentsScale != scale) native->layer.contentsScale = scale;
        if (!CGSizeEqualToSize(native->layer.drawableSize, drawable_size)) native->layer.drawableSize = drawable_size;
        [CATransaction commit];
        [CATransaction flush];
        if (asynchronous) {
            native->stopping = 0;
            result = maage_thread_start(maage_metal_present_loop, native, &native->present_thread);
            if (maage_status_failed(result)) { [pool drain]; return result; }
            native->asynchronous = 1;
        }
    }
    renderer->refresh = 0;
    drawable = nil;
    if (asynchronous) {
        if (!native->colors[slot] || native->colors[slot].width != (NSUInteger)state->framebuffer_width ||
            native->colors[slot].height != (NSUInteger)state->framebuffer_height) {
            MTLTextureDescriptor *descriptor = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:
                MTLPixelFormatBGRA8Unorm width:state->framebuffer_width height:state->framebuffer_height mipmapped:NO];
            descriptor.storageMode = MTLStorageModePrivate;
            descriptor.usage = MTLTextureUsageRenderTarget;
            [native->colors[slot] release];
            native->colors[slot] = [native->device newTextureWithDescriptor:descriptor];
            if (!native->colors[slot]) { [pool drain]; return MAAGE_STATUS_VALUE(MAAGE_STATUS_ALLOCATION_FAILED, MAAGE_CONTEXT_RENDER, MAAGE_OPERATION_PRESENT, 0); }
        }
        color = native->colors[slot];
    } else {
        acquired = CACurrentMediaTime();
        drawable = [native->layer nextDrawable];
        native->drawable_seconds += CACurrentMediaTime() - acquired;
        if (!drawable) { [pool drain]; return MAAGE_STATUS_VALUE(MAAGE_STATUS_NOOP, MAAGE_CONTEXT_RENDER, MAAGE_OPERATION_PRESENT, 0); }
        color = drawable.texture;
    }
    if (count && (!native->depth || native->depth.width != color.width ||
        native->depth.height != color.height)) {
        MTLTextureDescriptor *descriptor = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:
            MTLPixelFormatDepth32Float width:color.width height:color.height mipmapped:NO];
        [native->depth release];
        descriptor.storageMode = MTLStorageModePrivate;
        descriptor.usage = MTLTextureUsageRenderTarget;
        native->depth = [native->device newTextureWithDescriptor:descriptor];
        if (!native->depth) { [pool drain]; return MAAGE_STATUS_VALUE(MAAGE_STATUS_ALLOCATION_FAILED, MAAGE_CONTEXT_RENDER, MAAGE_OPERATION_PRESENT, 0); }
    }
    command = [native->queue commandBuffer];
    pass = [MTLRenderPassDescriptor renderPassDescriptor];
    pass.colorAttachments[0].texture = color;
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
        [encoder setVertexBuffer:native->ui_vertices[slot][0] offset:0 atIndex:0];
        [encoder setVertexBytes:logical_size length:sizeof(logical_size) atIndex:1];
        [encoder setFragmentTexture:(id<MTLTexture>)layers[0]->texture->platform atIndex:0];
        [encoder drawIndexedPrimitives:MTLPrimitiveTypeTriangle indexCount:layers[0]->index_count
            indexType:MTLIndexTypeUInt32 indexBuffer:native->ui_indices[slot][0] indexBufferOffset:0];
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
        MTLViewport viewport = {0, 0, color.width, color.height, 0, 1};
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
        MTLViewport viewport = {0, 0, color.width, color.height, 0, 1};
        pass = [MTLRenderPassDescriptor renderPassDescriptor];
        pass.colorAttachments[0].texture = color;
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
            [encoder setVertexBuffer:native->ui_vertices[slot][layer] offset:0 atIndex:0];
            [encoder setVertexBytes:logical_size length:sizeof(logical_size) atIndex:1];
            [encoder setFragmentTexture:(id<MTLTexture>)batch->texture->platform atIndex:0];
            [encoder drawIndexedPrimitives:MTLPrimitiveTypeTriangle indexCount:batch->index_count
                indexType:MTLIndexTypeUInt32 indexBuffer:native->ui_indices[slot][layer] indexBufferOffset:0];
        }
        [encoder endEncoding];
    }
    if (!preparing && !asynchronous) [command presentDrawable:drawable];
    if (renderer->operation_trace) native->frame_traces[slot] = *renderer->operation_trace;
    else maage_status_trace_init(&native->frame_traces[slot]);
    MAAGE_STATUS_TRACE(&native->frame_traces[slot], 0, MAAGE_CONTEXT_RENDER, MAAGE_OPERATION_PRESENT);
    native->frames[slot] = [command retain];
    [command commit];
    native->frame_slot = (slot + 1) % MAAGE_METAL_FRAME_SLOTS;
    if (asynchronous) {
        maage_mutex_lock(native->present_mutex);
        native->sequence[slot] = ++native->submitted;
        maage_condition_signal(native->present_condition);
        maage_mutex_unlock(native->present_mutex);
    } else ++native->presented;
    result = preparing ? maage_metal_complete(renderer, slot) :
        maage_status_make(MAAGE_STATUS_SUCCESS, 0, MAAGE_OPERATION_PRESENT, 0);
    if (preparing && maage_status_code(result) == MAAGE_STATUS_SUCCESS) {
        /* Present completed content with the layer transaction before reveal. */
        [CATransaction begin];
        [CATransaction setDisableActions:YES];
        [drawable present];
        [CATransaction commit];
        [CATransaction flush];
    }
    if (!native->sample_start) native->sample_start = started;
    ++native->sample_frames;
    acquired = CACurrentMediaTime();
    if (acquired - native->sample_start >= 5.0) {
        double drawable_seconds;
        unsigned long presented;
        maage_mutex_lock(native->present_mutex);
        drawable_seconds = native->drawable_seconds; presented = native->presented;
        native->drawable_seconds = 0; native->presented = 0;
        maage_mutex_unlock(native->present_mutex);
        maage_log_verbose("maage.render.metal", "Rendered=%.1f FPS; presented=%.1f FPS; display sync=%s; path=%s; drawable wait=%.3fms/present; GPU slot wait=%.3fms/frame; GPU render=%.3fms/frame.",
            native->sample_frames / (acquired - native->sample_start),
            presented / (acquired - native->sample_start),
            native->layer.displaySyncEnabled ? "on" : "off", asynchronous ? "mailbox" : "direct",
            presented ? drawable_seconds * 1000.0 / presented : 0,
            native->wait_seconds * 1000.0 / native->sample_frames,
            native->gpu_samples ? native->gpu_seconds * 1000.0 / native->gpu_samples : 0);
        native->sample_start = acquired; native->sample_frames = 0;
        native->wait_seconds = native->gpu_seconds = 0; native->gpu_samples = 0;
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
static maage_status maage_metal_set_vsync(maage_renderer *renderer, int enabled)
{
    maage_metal_renderer *native = (maage_metal_renderer *)renderer->platform;
    maage_status result;
    unsigned int slot;
    if (![NSThread isMainThread]) return MAAGE_STATUS_VALUE(MAAGE_STATUS_INVALID_STATE, MAAGE_CONTEXT_RENDER, MAAGE_OPERATION_SET, MAAGE_DETAIL_WRONG_THREAD);
    if (!native || !native->layer) return MAAGE_STATUS_VALUE(MAAGE_STATUS_INVALID_STATE, MAAGE_CONTEXT_RENDER, MAAGE_OPERATION_SET, 0);
    result = maage_metal_present_result(renderer);
    if (maage_status_failed(result)) return result;
    if ((enabled && native->asynchronous) || (!enabled && !native->asynchronous)) {
        result = maage_metal_stop_presenter(native);
        if (!maage_status_failed(result)) result = maage_metal_present_result(renderer);
        if (maage_status_failed(result)) return result;
        for (slot = 0; slot < MAAGE_METAL_FRAME_SLOTS; ++slot) {
            result = maage_metal_complete(renderer, slot);
            if (maage_status_failed(result)) return result;
        }
    }
    [CATransaction begin];
    [CATransaction setDisableActions:YES];
    native->layer.displaySyncEnabled = enabled ? YES : NO;
    native->layer.framebufferOnly = enabled ? YES : NO;
    [CATransaction commit];
    /* The polling loop need not idle long enough to flush implicit changes. */
    [CATransaction flush];
    if (!enabled && !native->asynchronous) {
        native->stopping = 0;
        result = maage_thread_start(maage_metal_present_loop, native, &native->present_thread);
        if (maage_status_failed(result)) {
            native->layer.displaySyncEnabled = YES; native->layer.framebufferOnly = YES;
            [CATransaction flush];
            return result;
        }
        native->asynchronous = 1;
    }
    return MAAGE_STATUS_VALUE(MAAGE_STATUS_SUCCESS, MAAGE_CONTEXT_RENDER, MAAGE_OPERATION_SET, 0);
}
const maage_render_backend_api maage_render_metal_api = {
    MAAGE_RENDER_METAL, maage_metal_create, maage_metal_destroy, maage_metal_present,
    MAAGE_RENDER_CAPABILITIES_VERSION, maage_metal_prepare_frame, maage_metal_upload_geometry,
    maage_metal_destroy_mesh, maage_metal_draw_meshes,
    maage_metal_create_texture, maage_metal_destroy_texture, maage_metal_draw_frame,
    maage_metal_set_vsync
};
