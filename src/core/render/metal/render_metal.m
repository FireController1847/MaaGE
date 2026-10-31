#import <AppKit/AppKit.h>
#import <Metal/Metal.h>
#import <MetalPerformanceShaders/MetalPerformanceShaders.h>
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
    id<MTLRenderPipelineState> pipelines[MAAGE_RENDER_PIPELINE_COUNT];
    id<MTLDepthStencilState> depth_state;
    id<MTLDepthStencilState> translucent_depth;
    id<MTLTexture> depth;
    id<MTLBuffer> ui_vertices[MAAGE_METAL_FRAME_SLOTS][3];
    id<MTLBuffer> ui_indices[MAAGE_METAL_FRAME_SLOTS][3];
    id<MTLCommandBuffer> frames[MAAGE_METAL_FRAME_SLOTS];
    id<MTLTexture> colors[MAAGE_METAL_FRAME_SLOTS];
    /* Per-slot targets never overwrite in-flight GPU work. */
    id<MTLTexture> panorama_source[MAAGE_METAL_FRAME_SLOTS];
    id<MTLTexture> panorama_blurred[MAAGE_METAL_FRAME_SLOTS];
    MPSImageGaussianBlur *panorama_filter;
    id<MTLCommandQueue> present_queue;
    maage_mutex *present_mutex;
    maage_condition *present_condition;
    maage_thread *present_thread;
    /* All publication, lease and pacing fields are protected by present_mutex.
     * A 64-bit serial avoids the 32-bit unsigned-long rollover during long runs. */
    unsigned long long sequence[MAAGE_METAL_FRAME_SLOTS], submitted;
    unsigned long presented, deferred_frames, drawable_misses, size_misses;
    double drawable_retry_after;
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
    id<MTLBuffer> sorted_indices[MAAGE_METAL_FRAME_SLOTS];
    unsigned long sorted_revision[MAAGE_METAL_FRAME_SLOTS];
} maage_metal_mesh;

/* Only the cached layer and immutable Metal objects cross this boundary.
 * NSWindow/NSView and all geometry changes remain on the main thread. */
static void maage_metal_present_loop(void *context) {
    maage_metal_renderer *native = context;
    for (;;) {
        NSAutoreleasePool *pool = [[NSAutoreleasePool alloc] init];
        id<MTLCommandBuffer> rendered = nil, command = nil;
        id<CAMetalDrawable> drawable;
        id<MTLTexture> color, previous_color = nil;
        id<MTLBlitCommandEncoder> copy;
        unsigned int slot, newest = 0;
        unsigned long long sequence = 0, pending = 0;
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
                    pending = native->sequence[slot];
                    pending_slot = slot;
                }
                if (native->sequence[slot] > sequence && ([native->frames[slot] status] == MTLCommandBufferStatusCompleted || [native->frames[slot] status] == MTLCommandBufferStatusError)) {
                    sequence = native->sequence[slot];
                    newest = slot;
                }
            }
            if (native->stopping || pending) break;
            maage_condition_wait(native->present_condition, native->present_mutex);
        }
        if (native->stopping) {
            maage_mutex_unlock(native->present_mutex);
            [pool drain];
            break;
        }
        if (!sequence) {
            /* GPU completion, never drawable availability, may delay selection.
             * Retaining the command lets the owner recycle unrelated slots. */
            rendered = [native->frames[pending_slot] retain];
            maage_mutex_unlock(native->present_mutex);
            [rendered waitUntilCompleted];
            [rendered release];
            [pool drain];
            continue;
        }
        rendered = [native->frames[newest] retain];
        trace = native->frame_traces[newest];
        if ([rendered status] == MTLCommandBufferStatusError) {
            native_error = (long) [[rendered error] code];
            native->present_result = maage_status_trace_record(&trace, maage_status_make(MAAGE_STATUS_ERROR, MAAGE_CONTEXT_RENDER, MAAGE_OPERATION_PRESENT, 0), MAAGE_CONTEXT_RENDER, MAAGE_OPERATION_PRESENT, __FILE__, __LINE__, native_error);
            native->present_trace = trace;
            native->stopping = 1;
            maage_mutex_unlock(native->present_mutex);
            [rendered release];
            [pool drain];
            break;
        }
        native->presenting = (int) newest;
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
                if (native->sequence[slot] > sequence && [native->frames[slot] status] == MTLCommandBufferStatusCompleted) {
                    sequence = native->sequence[slot];
                    newest = slot;
                }
            if ((int) newest != native->presenting) {
                previous_color = color;
                color = [native->colors[newest] retain];
                trace = native->frame_traces[newest];
                native->presenting = (int) newest;
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
                [copy copyFromTexture:color sourceSlice:0 sourceLevel:0 sourceOrigin:MTLOriginMake(0, 0, 0) sourceSize:MTLSizeMake(color.width, color.height, 1) toTexture:drawable.texture destinationSlice:0 destinationLevel:0 destinationOrigin:MTLOriginMake(0, 0, 0)];
                [copy endEncoding];
                [command presentDrawable:drawable];
                [command commit];
                /* Keep this color slot immutable through the GPU copy. */
                [command waitUntilCompleted];
                if ([command status] == MTLCommandBufferStatusError) {
                    result = maage_status_make(MAAGE_STATUS_ERROR, MAAGE_CONTEXT_RENDER, MAAGE_OPERATION_PRESENT, 0);
                    native_error = (long) [[command error] code];
                }
            } else
                result = maage_status_make(MAAGE_STATUS_ALLOCATION_FAILED, MAAGE_CONTEXT_RENDER, MAAGE_OPERATION_PRESENT, 0);
        }
        [color release];
        maage_mutex_lock(native->present_mutex);
        native->drawable_seconds += waited;
        if (command && !maage_status_failed(result)) {
            /* A successful GPU copy allows another bounded burst of renders.
             * This is presentation-resource backpressure, not an FPS ceiling. */
            ++native->presented;
            native->drawable_retry_after = 0;
        } else if (!cancelled && !maage_status_failed(result)) {
            /* Retry unavailable drawables without flooding the GPU. A resize
             * mismatch is retried immediately with newly sized render slots. */
            if (!drawable) {
                ++native->drawable_misses;
                native->drawable_retry_after = CACurrentMediaTime() + 0.05;
            } else {
                ++native->size_misses;
                native->drawable_retry_after = 0;
            }
        }
        native->presenting = -1;
        if (maage_status_failed(result)) {
            native->present_result = maage_status_trace_record(&trace, result, MAAGE_CONTEXT_RENDER, MAAGE_OPERATION_PRESENT, __FILE__, __LINE__, native_error);
            native->present_trace = trace;
            native->stopping = 1;
        }
        maage_mutex_unlock(native->present_mutex);
        [pool drain];
    }
}

static maage_status maage_metal_stop_presenter(maage_metal_renderer *native) {
    maage_status result;
    if (!native->present_thread) return maage_status_make(MAAGE_STATUS_NOOP, 0, MAAGE_OPERATION_STOP, 0);
    maage_mutex_lock(native->present_mutex);
    native->stopping = 1;
    maage_condition_signal(native->present_condition);
    maage_mutex_unlock(native->present_mutex);
    result = maage_thread_join(native->present_thread);
    if (!maage_status_failed(result)) {
        native->present_thread = NULL;
        native->asynchronous = 0;
        memset(native->sequence, 0, sizeof(native->sequence));
    }
    return result;
}

static maage_status maage_metal_present_result(maage_renderer *renderer) {
    maage_metal_renderer *native = renderer->platform;
    maage_status result;
    maage_mutex_lock(native->present_mutex);
    result = native->present_result;
    if (maage_status_failed(result) && renderer->operation_trace) *renderer->operation_trace = native->present_trace;
    maage_mutex_unlock(native->present_mutex);
    return result;
}

static maage_status maage_metal_compile_pipeline(maage_renderer *renderer, id<MTLLibrary> library, int mode, id<MTLRenderPipelineState> *output) {
    maage_metal_renderer *native = renderer->platform;
    id<MTLFunction> vertex, fragment;
    id<MTLRenderPipelineState> compiled;
    MTLRenderPipelineDescriptor *pipeline;
    NSError *error = nil;
    static const MTLBlendFactor factors[] = {
        MTLBlendFactorOne, MTLBlendFactorSourceAlpha, MTLBlendFactorOneMinusSourceAlpha,
        MTLBlendFactorSourceColor, MTLBlendFactorDestinationColor,
        MTLBlendFactorOneMinusSourceColor, MTLBlendFactorOneMinusDestinationColor};
    const maage_render_pipeline_state *state = &maage_render_pipeline_states[mode];
    int ui = state->ui || state->fullscreen;
    *output = nil;
    vertex = [library newFunctionWithName:state->fullscreen ? @"maage_fullscreen_vertex_shader" : (ui ? @"maage_ui_vertex_shader" : @"maage_mesh_vertex_shader")];
    fragment = [library newFunctionWithName:ui ? @"maage_ui_fragment_shader" : @"maage_mesh_fragment_shader"];
    pipeline = [[MTLRenderPipelineDescriptor alloc] init];
    pipeline.vertexFunction = vertex;
    pipeline.fragmentFunction = fragment;
    pipeline.colorAttachments[0].pixelFormat = MTLPixelFormatBGRA8Unorm;
    pipeline.colorAttachments[0].blendingEnabled = state->blend;
    pipeline.colorAttachments[0].sourceRGBBlendFactor = factors[state->source_rgb];
    pipeline.colorAttachments[0].destinationRGBBlendFactor = factors[state->destination_rgb];
    pipeline.colorAttachments[0].sourceAlphaBlendFactor = MTLBlendFactorOne;
    pipeline.colorAttachments[0].destinationAlphaBlendFactor = MTLBlendFactorOneMinusSourceAlpha;
    if (state->depth_test) pipeline.depthAttachmentPixelFormat = MTLPixelFormatDepth32Float;
    compiled = vertex && fragment && pipeline ? [native->device newRenderPipelineStateWithDescriptor:pipeline error:&error] : nil;
    [pipeline release];
    [vertex release];
    [fragment release];
    if (!compiled) return maage_status_trace_record(renderer->operation_trace, maage_status_make(MAAGE_STATUS_ERROR, MAAGE_CONTEXT_RENDER, MAAGE_OPERATION_INITIALIZE, 0), MAAGE_CONTEXT_RENDER, MAAGE_OPERATION_INITIALIZE, __FILE__, __LINE__, error ? (long) [error code] : 0);
    *output = compiled;
    return maage_status_make(MAAGE_STATUS_SUCCESS, 0, MAAGE_OPERATION_INITIALIZE, 0);
}
static maage_status maage_metal_pipeline(maage_renderer *renderer) {
    maage_metal_renderer *native = renderer->platform;
    NSString *source;
    id<MTLLibrary> library;
    id<MTLRenderPipelineState> compiled[MAAGE_RENDER_PIPELINE_COUNT] = {nil};
    maage_status result;
    id<MTLDepthStencilState> depth_state;
    MTLDepthStencilDescriptor *depth;
    NSError *error = nil;
    int mode;
    source = [[NSString alloc] initWithUTF8String:maage_metal_mesh_source];
    if (!source) return MAAGE_STATUS_VALUE(MAAGE_STATUS_ALLOCATION_FAILED, MAAGE_CONTEXT_RENDER, MAAGE_OPERATION_INITIALIZE, 0);
    library = [native->device newLibraryWithSource:source options:nil error:&error];
    [source release];
    if (!library) {
        return maage_status_trace_record(renderer->operation_trace, maage_status_make(MAAGE_STATUS_ERROR, MAAGE_CONTEXT_RENDER, MAAGE_OPERATION_INITIALIZE, 0), MAAGE_CONTEXT_RENDER, MAAGE_OPERATION_INITIALIZE, __FILE__, __LINE__, error ? (long) [error code] : 0);
    }
    for (mode = 0; mode < MAAGE_RENDER_PIPELINE_COUNT; ++mode) {
        result = maage_metal_compile_pipeline(renderer, library, mode, &compiled[mode]);
        if (maage_status_failed(result)) break;
    }
    [library release];
    if (mode != MAAGE_RENDER_PIPELINE_COUNT) {
        for (mode = 0; mode < MAAGE_RENDER_PIPELINE_COUNT; ++mode)
            [compiled[mode] release];
        return MAAGE_STATUS_PROPAGATE(renderer->operation_trace, result, MAAGE_CONTEXT_RENDER, MAAGE_OPERATION_INITIALIZE);
    }
    depth = [[MTLDepthStencilDescriptor alloc] init];
    depth.depthCompareFunction = MTLCompareFunctionLessEqual;
    depth.depthWriteEnabled = maage_render_pipeline_states[MAAGE_RENDER_SOLID].depth_write;
    depth_state = depth ? [native->device newDepthStencilStateWithDescriptor:depth] : nil;
    depth.depthWriteEnabled = maage_render_pipeline_states[MAAGE_RENDER_TRANSLUCENT].depth_write;
    native->translucent_depth = depth ? [native->device newDepthStencilStateWithDescriptor:depth] : nil;
    [depth release];
    if (!depth_state || !native->translucent_depth) {
        [depth_state release];
        for (mode = 0; mode < MAAGE_RENDER_PIPELINE_COUNT; ++mode)
            [compiled[mode] release];
        return MAAGE_STATUS_VALUE(MAAGE_STATUS_ALLOCATION_FAILED, MAAGE_CONTEXT_RENDER, MAAGE_OPERATION_INITIALIZE, 0);
    }
    memcpy(native->pipelines, compiled, sizeof(compiled));
    native->depth_state = depth_state;
    return MAAGE_STATUS_VALUE(MAAGE_STATUS_SUCCESS, MAAGE_CONTEXT_RENDER, MAAGE_OPERATION_INITIALIZE, 0);
}

static maage_status maage_metal_create(maage_renderer *renderer) {
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
    native->panorama_filter = [[MPSImageGaussianBlur alloc] initWithDevice:native->device sigma:MAAGE_RENDER_BLUR_SIGMA];
    if (!native->panorama_filter) return MAAGE_STATUS_VALUE(MAAGE_STATUS_ALLOCATION_FAILED, MAAGE_CONTEXT_RENDER, MAAGE_OPERATION_INITIALIZE, 0);
    native->panorama_filter.edgeMode = MPSImageEdgeModeClamp;
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
    native->previous_background = [[(NSWindow *) renderer->native.handles.cocoa.window backgroundColor] retain];
    native->previous_wants_layer = [native->view wantsLayer];
    native->previous_layer = [[native->view layer] retain];
    [native->view setLayer:native->layer];
    [native->view setWantsLayer:YES];
    native->installed = 1;
    renderer->capabilities.features |= MAAGE_RENDER_CAP_SHADERS | MAAGE_RENDER_CAP_VERTEX_BUFFERS | MAAGE_RENDER_CAP_INDEX_BUFFERS | MAAGE_RENDER_CAP_RENDER_TARGETS | MAAGE_RENDER_CAP_DEPTH | MAAGE_RENDER_CAP_STENCIL | MAAGE_RENDER_CAP_INSTANCING | MAAGE_RENDER_CAP_COMPUTE | MAAGE_RENDER_CAP_TEXTURED_MESH | MAAGE_RENDER_CAP_UI | MAAGE_RENDER_CAP_SCENE | MAAGE_RENDER_CAP_FOG;
    renderer->capabilities.texture_formats = MAAGE_RENDER_FORMAT_RGBA8 | MAAGE_RENDER_FORMAT_BGRA8;
    renderer->capabilities.synchronization = MAAGE_RENDER_SYNC_CPU_WAIT;
    renderer->capabilities.max_frames_in_flight = MAAGE_METAL_FRAME_SLOTS;
    /* Texture limits vary by GPU family; leave unknown until resource creation
     * needs family-specific queries rather than promising an unqueried limit. */
    return MAAGE_STATUS_VALUE(MAAGE_STATUS_SUCCESS, MAAGE_CONTEXT_RENDER, MAAGE_OPERATION_INITIALIZE, 0);
}
static maage_status maage_metal_complete(maage_renderer *renderer, unsigned int slot) {
    maage_metal_renderer *native = renderer->platform;
    id<MTLCommandBuffer> command;
    maage_status result = maage_status_make(MAAGE_STATUS_SUCCESS, 0, MAAGE_OPERATION_PRESENT, 0);
    /* Atomically detach the slot's owned command reference. The presenter may
     * inspect or retain it only while holding this same lock. Never wait for
     * GPU execution while holding the lock. */
    maage_mutex_lock(native->present_mutex);
    command = native->frames[slot];
    native->frames[slot] = nil;
    maage_mutex_unlock(native->present_mutex);
    if (!command) return result;
    [command waitUntilCompleted];
    if ([command status] == MTLCommandBufferStatusError) {
        if (renderer->operation_trace) *renderer->operation_trace = native->frame_traces[slot];
        result = maage_status_trace_record(renderer->operation_trace, maage_status_make(MAAGE_STATUS_ERROR, MAAGE_CONTEXT_RENDER, MAAGE_OPERATION_PRESENT, 0), MAAGE_CONTEXT_RENDER, MAAGE_OPERATION_PRESENT, __FILE__, __LINE__, (long) [[command error] code]);
    }
    if (@available(macOS 10.15, *)) {
        if (command.GPUStartTime > 0 && command.GPUEndTime >= command.GPUStartTime) {
            native->gpu_seconds += command.GPUEndTime - command.GPUStartTime;
            ++native->gpu_samples;
        }
    }
    [command release];
    return result;
}
static maage_status maage_metal_destroy(maage_renderer *renderer) {
    maage_metal_renderer *native = renderer->platform;
    maage_status result = maage_status_make(MAAGE_STATUS_SUCCESS, 0, MAAGE_OPERATION_DESTROY, 0), completed;
    maage_status_trace failure_trace;
    int have_failure_trace = 0;
    unsigned int slot;
    if (!native) return maage_status_make(MAAGE_STATUS_NOOP, 0, MAAGE_OPERATION_DESTROY, 0);
    result = maage_metal_stop_presenter(native);
    if (maage_status_failed(result)) return result;
    if (maage_status_failed(native->present_result)) {
        failure_trace = native->present_trace;
        have_failure_trace = 1;
        if (renderer->operation_trace) *renderer->operation_trace = failure_trace;
        result = MAAGE_STATUS_PROPAGATE(renderer->operation_trace, native->present_result, MAAGE_CONTEXT_RENDER, MAAGE_OPERATION_DESTROY);
    }
    for (slot = 0; slot < MAAGE_METAL_FRAME_SLOTS; ++slot) {
        completed = maage_metal_complete(renderer, slot);
        if (!maage_status_failed(result)) {
            result = completed;
            if (maage_status_failed(result) && renderer->operation_trace) {
                failure_trace = *renderer->operation_trace;
                have_failure_trace = 1;
            }
        }
        [native->colors[slot] release];
        [native->panorama_source[slot] release];
        [native->panorama_blurred[slot] release];
    }
    [native->panorama_filter release];
    if (native->installed) {
        [(NSWindow *) renderer->native.handles.cocoa.window setBackgroundColor:native->previous_background];
        [native->view setLayer:native->previous_layer];
        [native->view setWantsLayer:native->previous_wants_layer];
    }
    [native->previous_background release];
    [native->previous_layer release];
    [native->layer release];
    [native->depth release];
    [native->depth_state release];
    [native->translucent_depth release];
    {
        int mode;
        for (mode = 0; mode < MAAGE_RENDER_PIPELINE_COUNT; ++mode)
            [native->pipelines[mode] release];
    }
    {
        int layer;
        for (slot = 0; slot < MAAGE_METAL_FRAME_SLOTS; ++slot)
            for (layer = 0; layer < 3; ++layer) {
                [native->ui_vertices[slot][layer] release];
                [native->ui_indices[slot][layer] release];
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
    return MAAGE_STATUS_PROPAGATE(renderer->operation_trace, maage_status_failed(result) ? result : maage_status_make(MAAGE_STATUS_SUCCESS, 0, MAAGE_OPERATION_DESTROY, 0), MAAGE_CONTEXT_RENDER, MAAGE_OPERATION_DESTROY);
}
static maage_status maage_metal_host_buffer(maage_metal_renderer *native, id<MTLBuffer> *buffer, size_t bytes) {
    id<MTLBuffer> replacement;
    size_t capacity;
    if (*buffer && [*buffer length] >= bytes) return MAAGE_STATUS_VALUE(MAAGE_STATUS_SUCCESS, MAAGE_CONTEXT_RENDER, MAAGE_OPERATION_UPLOAD, 0);
    capacity = maage_render_transient_capacity(*buffer ? [*buffer length] : 0, bytes);
    replacement = [native->device newBufferWithLength:capacity options:MTLResourceStorageModeShared];
    if (!replacement) return MAAGE_STATUS_VALUE(MAAGE_STATUS_ALLOCATION_FAILED, MAAGE_CONTEXT_RENDER, MAAGE_OPERATION_UPLOAD, 0);
    [*buffer release];
    *buffer = replacement;
    return MAAGE_STATUS_VALUE(MAAGE_STATUS_SUCCESS, MAAGE_CONTEXT_RENDER, MAAGE_OPERATION_UPLOAD, 0);
}
static maage_status maage_metal_sort_geometry(maage_metal_renderer *native, const maage_render_mesh *mesh, const float matrix[16], unsigned int slot) {
    maage_metal_mesh *geometry = mesh->platform;
    maage_status result = maage_render_geometry_sort_slot(mesh, matrix, geometry->sorted_revision, MAAGE_METAL_FRAME_SLOTS, slot);
    if (maage_status_code(result) != MAAGE_STATUS_SUCCESS) return result;
    result = maage_metal_host_buffer(native, &geometry->sorted_indices[slot], mesh->geometry.translucent_count * sizeof(unsigned int));
    if (maage_status_failed(result)) return result;
    result = maage_render_geometry_write_sorted(&mesh->geometry, [geometry->sorted_indices[slot] contents]);
    if (maage_status_failed(result)) return result;
    geometry->sorted_revision[slot] = mesh->geometry.revision;
    return maage_status_make(MAAGE_STATUS_SUCCESS, 0, MAAGE_OPERATION_UPLOAD, 0);
}
static void maage_metal_draw_geometry(id<MTLRenderCommandEncoder> encoder, const maage_render_mesh *mesh, int layer, unsigned int slot) {
    maage_metal_mesh *geometry = mesh->platform;
    size_t offset, count = maage_render_geometry_range(&mesh->geometry, layer, &offset);
    if (!count) return;
    [encoder setVertexBuffer:geometry->vertices offset:0 atIndex:0];
    [encoder setFragmentTexture:(id<MTLTexture>) mesh->texture->platform atIndex:0];
    [encoder drawIndexedPrimitives:MTLPrimitiveTypeTriangle indexCount:count indexType:MTLIndexTypeUInt32 indexBuffer:layer == MAAGE_RENDER_TRANSLUCENT ? geometry->sorted_indices[slot] : geometry->indices indexBufferOffset:offset * sizeof(unsigned int)];
}
static void maage_metal_draw_mesh_passes(maage_metal_renderer *native, id<MTLRenderCommandEncoder> encoder, const maage_render_frame_plan *plan, int foreground, unsigned int slot) {
    maage_render_uniforms uniforms;
    maage_render_mesh_cursor cursor = {0, 0};
    const maage_render_mesh *mesh;
    unsigned int previous = MAAGE_RENDER_FULLSCREEN;
    [encoder setFrontFacingWinding:MTLWindingCounterClockwise];
    while ((mesh = maage_render_frame_next_mesh(plan, foreground, &cursor)) != NULL) {
        if (previous != cursor.layer) {
            const maage_render_pipeline_state *state = &maage_render_pipeline_states[cursor.layer];
            [encoder setRenderPipelineState:native->pipelines[cursor.layer]];
            [encoder setDepthStencilState:state->depth_write ? native->depth_state : native->translucent_depth];
            [encoder setCullMode:state->cull ? MTLCullModeBack : MTLCullModeNone];
            previous = cursor.layer;
        }
        maage_render_draw_uniforms(plan, foreground, &cursor, &uniforms);
        [encoder setVertexBytes:&uniforms length:sizeof(uniforms) atIndex:1];
        [encoder setFragmentBytes:&uniforms length:sizeof(uniforms) atIndex:0];
        if (cursor.instance && cursor.layer == MAAGE_RENDER_TRANSLUCENT) {
            maage_metal_mesh *geometry = mesh->platform;
            [encoder setVertexBuffer:geometry->vertices offset:0 atIndex:0];
            [encoder setFragmentTexture:(id<MTLTexture>) mesh->texture->platform atIndex:0];
            [encoder drawIndexedPrimitives:MTLPrimitiveTypeTriangle indexCount:3 indexType:MTLIndexTypeUInt32 indexBuffer:geometry->indices indexBufferOffset:cursor.triangle * sizeof(unsigned int)];
        } else
            maage_metal_draw_geometry(encoder, mesh, cursor.layer, slot);
    }
}
static void maage_metal_draw_ui(maage_metal_renderer *native, id<MTLRenderCommandEncoder> encoder, unsigned int slot, unsigned int layer, const maage_render_ui_data *batch, size_t first, size_t count, int panorama) {
    maage_render_uniforms uniforms;
    maage_render_ui_segment segments[3];
    unsigned int n, segment_count;
    if (!batch || !count) return;
    segment_count = maage_render_ui_segments(batch, first, count, segments);
    maage_render_ui_uniforms(batch, &uniforms);
    [encoder setDepthStencilState:nil];
    [encoder setCullMode:MTLCullModeNone];
    [encoder setVertexBuffer:native->ui_vertices[slot][layer] offset:0 atIndex:0];
    [encoder setVertexBytes:&uniforms length:sizeof(uniforms) atIndex:1];
    [encoder setFragmentTexture:(id<MTLTexture>) batch->texture->platform atIndex:0];
    for (n = 0; n < segment_count; ++n) {
        [encoder setRenderPipelineState:native->pipelines[MAAGE_RENDER_UI + (panorama ? MAAGE_RENDER_BLEND_ALPHA : segments[n].blend)]];
        [encoder drawIndexedPrimitives:MTLPrimitiveTypeTriangle indexCount:segments[n].count indexType:MTLIndexTypeUInt32 indexBuffer:native->ui_indices[slot][layer] indexBufferOffset:segments[n].first * sizeof(unsigned int)];
    }
}
static maage_status maage_metal_frame(maage_renderer *renderer, const maage_window_state *state, const maage_render_frame_plan *plan) {
    maage_metal_renderer *native = renderer->platform;
    NSWindow *window = renderer->native.handles.cocoa.window;
    NSAutoreleasePool *pool;
    id<CAMetalDrawable> drawable;
    id<MTLTexture> color;
    id<MTLCommandBuffer> command;
    id<MTLRenderCommandEncoder> encoder;
    MTLRenderPassDescriptor *pass;
    maage_status result;
    size_t index, count = plan->mesh_count + plan->instance_count;
    const maage_render_mesh *foreground = plan->foreground;
    const float *view_projection = plan->world.matrix;
    const maage_render_ui_data *const *layers = plan->layers, *batch, *ui = plan->layers[2];
    size_t panorama_count = plan->panorama_count;
    int preparing = plan->preparing;
    int layer;
    CGRect frame;
    CGSize drawable_size;
    CGFloat scale;
    BOOL transaction;
    unsigned int slot = native->frame_slot;
    unsigned int candidate;
    int asynchronous = native->asynchronous && !preparing;
    double started, acquired;
    if (![NSThread isMainThread]) return MAAGE_STATUS_VALUE(MAAGE_STATUS_DEPENDENCY_FAILURE, MAAGE_CONTEXT_RENDER, MAAGE_OPERATION_PRESENT, MAAGE_STATUS_INVALID_STATE);
    result = maage_metal_present_result(renderer);
    if (maage_status_failed(result)) return result;
    if (!preparing && (state->mode_transitioning || state->framebuffer_width < 1 ||
                       state->framebuffer_height < 1 || [window isMiniaturized] ||
                       !([window occlusionState] & NSWindowOcclusionStateVisible)))
        return MAAGE_STATUS_VALUE(MAAGE_STATUS_NOOP, MAAGE_CONTEXT_RENDER, MAAGE_OPERATION_PRESENT, 0);
    /* Initial hidden-window priming is a one-off synchronous transaction. */
    if (preparing && native->asynchronous) return MAAGE_STATUS_VALUE(MAAGE_STATUS_NOOP, MAAGE_CONTEXT_RENDER, MAAGE_OPERATION_PRESENT, 0);
    pool = [[NSAutoreleasePool alloc] init];
    started = CACurrentMediaTime();
    /* Use the entire content view, including permitted notch-adjacent regions.
     * Safe-area insets are for UI placement, not drawable cropping. */
    frame = NSRectToCGRect([native->view bounds]);
    scale = state->width > 0 ? (double) state->framebuffer_width / state->width : [window backingScaleFactor];
    drawable_size = CGSizeMake(state->framebuffer_width, state->framebuffer_height);
    transaction = preparing != 0;
    /* Publish only actual layer geometry changes on the AppKit thread. The
     * presenter leases its drawable independently; dimension mismatches are
     * recoverable. Never join it from the main thread during live resize or
     * native-fullscreen Space transitions. */
    if (preparing || native->layer.presentsWithTransaction != transaction ||
        !CGRectEqualToRect(native->layer.frame, frame) || native->layer.contentsScale != scale ||
        !CGSizeEqualToSize(native->layer.drawableSize, drawable_size)) {
        [CATransaction begin];
        [CATransaction setDisableActions:YES];
        native->layer.presentsWithTransaction = transaction;
        if (preparing) {
            NSColor *color = [NSColor colorWithSRGBRed:renderer->clear[0] green:renderer->clear[1] blue:renderer->clear[2] alpha:renderer->clear[3]];
            [window setBackgroundColor:color];
            native->layer.backgroundColor = [color CGColor];
        }
        if (!CGRectEqualToRect(native->layer.frame, frame)) native->layer.frame = frame;
        if (native->layer.contentsScale != scale) native->layer.contentsScale = scale;
        if (!CGSizeEqualToSize(native->layer.drawableSize, drawable_size)) native->layer.drawableSize = drawable_size;
        [CATransaction commit];
        [CATransaction flush];
    }
    renderer->refresh = 0;
    if (asynchronous) {
        maage_mutex_lock(native->present_mutex);
        result = native->present_result;
        if (!maage_status_failed(result) && started < native->drawable_retry_after) {
            /* Only the temporary drawable retry gate applies. Uncapped FPS
             * must not be limited to a fixed number of renders per refresh.
             * The four GPU slots already bound outstanding GPU work. */
            ++native->deferred_frames;
            maage_mutex_unlock(native->present_mutex);
            [pool drain];
            return MAAGE_STATUS_VALUE(MAAGE_STATUS_NOOP, MAAGE_CONTEXT_RENDER, MAAGE_OPERATION_PRESENT, 0);
        }
        if (!maage_status_failed(result)) {
            /* The presenter leases one slot at a time. Reservation hides a
             * stale publication before its command buffer can be retired. */
            for (candidate = 0; candidate < MAAGE_METAL_FRAME_SLOTS; ++candidate) {
                id<MTLCommandBuffer> old;
                MTLCommandBufferStatus status;
                slot = (native->frame_slot + candidate) % MAAGE_METAL_FRAME_SLOTS;
                if ((int) slot == native->presenting) continue;
                old = native->frames[slot];
                status = old ? [old status] : MTLCommandBufferStatusCompleted;
                if (status == MTLCommandBufferStatusCompleted || status == MTLCommandBufferStatusError) break;
            }
            if (candidate == MAAGE_METAL_FRAME_SLOTS) {
                /* No completed free slot: yield instead of blocking AppKit on
                 * GPU completion. A subsequent frame rechecks availability. */
                ++native->deferred_frames;
                maage_mutex_unlock(native->present_mutex);
                [pool drain];
                return MAAGE_STATUS_VALUE(MAAGE_STATUS_NOOP, MAAGE_CONTEXT_RENDER, MAAGE_OPERATION_PRESENT, 0);
            }
            native->sequence[slot] = 0;
        } else if (renderer->operation_trace) *renderer->operation_trace = native->present_trace;
        maage_mutex_unlock(native->present_mutex);
        if (maage_status_failed(result)) {
            [pool drain];
            return result;
        }
    }
    acquired = CACurrentMediaTime();
    result = maage_metal_complete(renderer, slot);
    native->wait_seconds += CACurrentMediaTime() - acquired;
    if (maage_status_failed(result)) {
        [pool drain];
        return result;
    }
    for (layer = 0; layer < 3; ++layer) {
        batch = layers[layer];
        if (!batch || !batch->index_count) continue;
        if (!maage_render_gpu_layout_supported()) {
            [pool drain];
            return MAAGE_STATUS_VALUE(MAAGE_STATUS_NOT_IMPLEMENTED, MAAGE_CONTEXT_RENDER, MAAGE_OPERATION_PRESENT, 0);
        }
        if (!native->pipelines[MAAGE_RENDER_SOLID]) {
            result = maage_metal_pipeline(renderer);
            if (maage_status_code(result) != MAAGE_STATUS_SUCCESS) {
                [pool drain];
                return result;
            }
        }
        result = maage_metal_host_buffer(native, &native->ui_vertices[slot][layer], batch->vertex_count * sizeof(maage_render_ui_gpu_vertex));
        if (maage_status_code(result) == MAAGE_STATUS_SUCCESS) result = maage_metal_host_buffer(native, &native->ui_indices[slot][layer], batch->index_count * sizeof(*batch->indices));
        if (maage_status_code(result) != MAAGE_STATUS_SUCCESS) {
            [pool drain];
            return result;
        }
        /* Only this slot has completed; other frames retain their own data. */
        result = maage_render_ui_pack(batch, [native->ui_vertices[slot][layer] contents], [native->ui_indices[slot][layer] contents]);
        if (maage_status_failed(result)) {
            [pool drain];
            return result;
        }
    }
    drawable = nil;
    if (asynchronous) {
        if (!native->colors[slot] || native->colors[slot].width != (NSUInteger) state->framebuffer_width || native->colors[slot].height != (NSUInteger) state->framebuffer_height) {
            MTLTextureDescriptor *descriptor = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatBGRA8Unorm width:state->framebuffer_width height:state->framebuffer_height mipmapped:NO];
            descriptor.storageMode = MTLStorageModePrivate;
            descriptor.usage = MTLTextureUsageRenderTarget;
            [native->colors[slot] release];
            native->colors[slot] = [native->device newTextureWithDescriptor:descriptor];
            if (!native->colors[slot]) {
                [pool drain];
                return MAAGE_STATUS_VALUE(MAAGE_STATUS_ALLOCATION_FAILED, MAAGE_CONTEXT_RENDER, MAAGE_OPERATION_PRESENT, 0);
            }
        }
        color = native->colors[slot];
    } else {
        acquired = CACurrentMediaTime();
        drawable = [native->layer nextDrawable];
        native->drawable_seconds += CACurrentMediaTime() - acquired;
        if (!drawable) {
            [pool drain];
            return MAAGE_STATUS_VALUE(MAAGE_STATUS_NOOP, MAAGE_CONTEXT_RENDER, MAAGE_OPERATION_PRESENT, 0);
        }
        color = drawable.texture;
    }
    if ((count || foreground) && (!native->depth || native->depth.width != color.width || native->depth.height != color.height)) {
        MTLTextureDescriptor *descriptor = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatDepth32Float width:color.width height:color.height mipmapped:NO];
        [native->depth release];
        descriptor.storageMode = MTLStorageModePrivate;
        descriptor.usage = MTLTextureUsageRenderTarget;
        native->depth = [native->device newTextureWithDescriptor:descriptor];
        if (!native->depth) {
            [pool drain];
            return MAAGE_STATUS_VALUE(MAAGE_STATUS_ALLOCATION_FAILED, MAAGE_CONTEXT_RENDER, MAAGE_OPERATION_PRESENT, 0);
        }
    }
    for (index = 0; index < plan->ordered_count; ++index) {
        if (plan->ordered[index].instance) continue;
        result = maage_metal_sort_geometry(native, plan->ordered[index].mesh, view_projection, slot);
        if (maage_status_failed(result)) {
            [pool drain];
            return result;
        }
    }
    if (foreground) {
        result = maage_metal_sort_geometry(native, foreground, plan->hand.matrix, slot);
        if (maage_status_failed(result)) {
            [pool drain];
            return result;
        }
    }
    command = [native->queue commandBuffer];
    pass = [MTLRenderPassDescriptor renderPassDescriptor];
    pass.colorAttachments[0].texture = color;
    pass.colorAttachments[0].loadAction = MTLLoadActionClear;
    pass.colorAttachments[0].storeAction = MTLStoreActionStore;
    pass.colorAttachments[0].clearColor = MTLClearColorMake(renderer->clear[0], renderer->clear[1], renderer->clear[2], renderer->clear[3]);
    if (!command || !pass) {
        [pool drain];
        return MAAGE_STATUS_VALUE(MAAGE_STATUS_ALLOCATION_FAILED, MAAGE_CONTEXT_RENDER, MAAGE_OPERATION_PRESENT, 0);
    }
    if (layers[0] && layers[0]->index_count) {
        encoder = [command renderCommandEncoderWithDescriptor:pass];
        if (!encoder) {
            [pool drain];
            return MAAGE_STATUS_VALUE(MAAGE_STATUS_ERROR, MAAGE_CONTEXT_RENDER, MAAGE_OPERATION_PRESENT, 0);
        }
        maage_metal_draw_ui(native, encoder, slot, 0, layers[0], 0, layers[0]->index_count, 0);
        [encoder endEncoding];
        pass.colorAttachments[0].loadAction = MTLLoadActionLoad;
    }
    if (count) {
        pass.depthAttachment.texture = native->depth;
        pass.depthAttachment.loadAction = MTLLoadActionClear;
        pass.depthAttachment.storeAction = MTLStoreActionDontCare;
        pass.depthAttachment.clearDepth = 1.0;
    }
    encoder = [command renderCommandEncoderWithDescriptor:pass];
    if (!encoder) {
        [pool drain];
        return MAAGE_STATUS_VALUE(MAAGE_STATUS_ERROR, MAAGE_CONTEXT_RENDER, MAAGE_OPERATION_PRESENT, 0);
    }
    if (count) {
        MTLViewport viewport = {0, 0, color.width, color.height, 0, 1};
        [encoder setViewport:viewport];
        maage_metal_draw_mesh_passes(native, encoder, plan, 0, slot);
    }
    [encoder endEncoding];
    if (panorama_count) {
        id<MTLTexture> source, blurred;
        MTLTextureDescriptor *description;
        MTLRenderPassDescriptor *offscreen;
        if (!native->panorama_source[slot] || native->panorama_source[slot].width != color.width || native->panorama_source[slot].height != color.height) {
            description = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatBGRA8Unorm width:color.width height:color.height mipmapped:NO];
            description.storageMode = MTLStorageModePrivate;
            description.usage = MTLTextureUsageRenderTarget | MTLTextureUsageShaderRead;
            source = [native->device newTextureWithDescriptor:description];
            description.usage = MTLTextureUsageShaderRead | MTLTextureUsageShaderWrite;
            blurred = [native->device newTextureWithDescriptor:description];
            if (!source || !blurred) {
                [source release];
                [blurred release];
                [pool drain];
                return MAAGE_STATUS_VALUE(MAAGE_STATUS_ALLOCATION_FAILED, MAAGE_CONTEXT_RENDER, MAAGE_OPERATION_PRESENT, 0);
            }
            [native->panorama_source[slot] release];
            [native->panorama_blurred[slot] release];
            native->panorama_source[slot] = source;
            native->panorama_blurred[slot] = blurred;
        }
        source = native->panorama_source[slot];
        blurred = native->panorama_blurred[slot];
        /* Compose all six cube faces in framebuffer space first. */
        offscreen = [MTLRenderPassDescriptor renderPassDescriptor];
        offscreen.colorAttachments[0].texture = source;
        offscreen.colorAttachments[0].loadAction = MTLLoadActionClear;
        offscreen.colorAttachments[0].storeAction = MTLStoreActionStore;
        offscreen.colorAttachments[0].clearColor = MTLClearColorMake(renderer->clear[0], renderer->clear[1], renderer->clear[2], renderer->clear[3]);
        encoder = [command renderCommandEncoderWithDescriptor:offscreen];
        if (!encoder) {
            [pool drain];
            return MAAGE_STATUS_VALUE(MAAGE_STATUS_ERROR, MAAGE_CONTEXT_RENDER, MAAGE_OPERATION_PRESENT, 0);
        }
        maage_metal_draw_ui(native, encoder, slot, 2, ui, 0, panorama_count, 1);
        [encoder endEncoding];
        /* Gaussian blur AFTER full compositing, with zero CPU readback. */
        [native->panorama_filter encodeToCommandBuffer:command sourceTexture:source destinationTexture:blurred];
        /* Draw filtered image as an opaque full-screen image, before UI. */
        offscreen = [MTLRenderPassDescriptor renderPassDescriptor];
        offscreen.colorAttachments[0].texture = color;
        offscreen.colorAttachments[0].loadAction = MTLLoadActionLoad;
        offscreen.colorAttachments[0].storeAction = MTLStoreActionStore;
        encoder = [command renderCommandEncoderWithDescriptor:offscreen];
        if (!encoder) {
            [pool drain];
            return MAAGE_STATUS_VALUE(MAAGE_STATUS_ERROR, MAAGE_CONTEXT_RENDER, MAAGE_OPERATION_PRESENT, 0);
        }
        [encoder setRenderPipelineState:native->pipelines[MAAGE_RENDER_FULLSCREEN]];
        [encoder setDepthStencilState:nil];
        [encoder setCullMode:MTLCullModeNone];
        [encoder setFragmentTexture:blurred atIndex:0];
        [encoder drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:3];
        [encoder endEncoding];
    }
    for (layer = 1; layer < 3; ++layer) {
        MTLViewport viewport = {0, 0, color.width, color.height, 0, 1};
        if (layer == (layers[1] && layers[1]->after_foreground ? 1 : 2) && foreground) {
            pass = [MTLRenderPassDescriptor renderPassDescriptor];
            pass.colorAttachments[0].texture = color;
            pass.colorAttachments[0].loadAction = MTLLoadActionLoad;
            pass.colorAttachments[0].storeAction = MTLStoreActionStore;
            pass.depthAttachment.texture = native->depth;
            pass.depthAttachment.loadAction = MTLLoadActionClear;
            pass.depthAttachment.storeAction = MTLStoreActionDontCare;
            pass.depthAttachment.clearDepth = 1;
            encoder = [command renderCommandEncoderWithDescriptor:pass];
            if (!encoder) {
                [pool drain];
                return maage_status_make(MAAGE_STATUS_ERROR, MAAGE_CONTEXT_RENDER, MAAGE_OPERATION_PRESENT, 0);
            }
            [encoder setViewport:viewport];
            maage_metal_draw_mesh_passes(native, encoder, plan, 1, slot);
            [encoder endEncoding];
        }
        batch = layers[layer];
        if (!batch || batch->index_count <= (layer == 2 ? panorama_count : 0)) continue;
        pass = [MTLRenderPassDescriptor renderPassDescriptor];
        pass.colorAttachments[0].texture = color;
        pass.colorAttachments[0].loadAction = MTLLoadActionLoad;
        pass.colorAttachments[0].storeAction = MTLStoreActionStore;
        encoder = [command renderCommandEncoderWithDescriptor:pass];
        if (!encoder) {
            [pool drain];
            return MAAGE_STATUS_VALUE(MAAGE_STATUS_ERROR, MAAGE_CONTEXT_RENDER, MAAGE_OPERATION_PRESENT, 0);
        }
        [encoder setViewport:viewport];
        {
            size_t first = layer == 2 ? panorama_count : 0;
            maage_metal_draw_ui(native, encoder, slot, layer, batch, first, batch->index_count - first, 0);
        }
        [encoder endEncoding];
    }
    if (!preparing && !asynchronous) [command presentDrawable:drawable];
    [command commit];
    /* Publish the owned buffer, trace and sequence as a single transaction.
     * The worker never observes a sequence with a nil/dangling command. */
    maage_mutex_lock(native->present_mutex);
    if (renderer->operation_trace) native->frame_traces[slot] = *renderer->operation_trace;
    else
        maage_status_trace_init(&native->frame_traces[slot]);
    MAAGE_STATUS_TRACE(&native->frame_traces[slot], 0, MAAGE_CONTEXT_RENDER, MAAGE_OPERATION_PRESENT);
    native->frames[slot] = [command retain];
    if (asynchronous) {
        native->sequence[slot] = ++native->submitted;
        maage_condition_signal(native->present_condition);
    } else
        ++native->presented;
    maage_mutex_unlock(native->present_mutex);
    native->frame_slot = (slot + 1) % MAAGE_METAL_FRAME_SLOTS;
    result = preparing ? maage_metal_complete(renderer, slot) : maage_status_make(MAAGE_STATUS_SUCCESS, 0, MAAGE_OPERATION_PRESENT, 0);
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
        unsigned long presented, deferred, unavailable, mismatched;
        maage_mutex_lock(native->present_mutex);
        drawable_seconds = native->drawable_seconds;
        presented = native->presented;
        deferred = native->deferred_frames;
        unavailable = native->drawable_misses;
        mismatched = native->size_misses;
        native->drawable_seconds = 0;
        native->presented = 0;
        native->deferred_frames = native->drawable_misses = native->size_misses = 0;
        maage_mutex_unlock(native->present_mutex);
        maage_log_verbose("maage.render.metal", "GPU submissions=%.1f/s; presentation submissions=%.1f/s (not scanout); display sync=%s; path=%s; drawable wait=%.3fms/presentation; GPU slot wait=%.3fms/frame; GPU render=%.3fms/frame; backpressure skips=%lu; drawable unavailable=%lu; size mismatches=%lu.",
                          native->sample_frames / (acquired - native->sample_start),
                          presented / (acquired - native->sample_start),
                          native->layer.displaySyncEnabled ? "on" : "off", asynchronous ? "mailbox" : "direct",
                          presented ? drawable_seconds * 1000.0 / presented : 0,
                          native->wait_seconds * 1000.0 / native->sample_frames,
                          native->gpu_samples ? native->gpu_seconds * 1000.0 / native->gpu_samples : 0,
                          deferred, unavailable, mismatched);
        native->sample_start = acquired;
        native->sample_frames = 0;
        native->wait_seconds = native->gpu_seconds = 0;
        native->gpu_samples = 0;
    }
    [pool drain];
    return result;
}
static maage_status maage_metal_create_texture(maage_renderer *renderer, maage_render_texture *resource, const maage_pixel_buffer *atlas) {
    maage_metal_renderer *native = renderer->platform;
    MTLTextureDescriptor *descriptor;
    id<MTLTexture> texture;
    NSAutoreleasePool *pool;
    if (![NSThread isMainThread]) return MAAGE_STATUS_VALUE(MAAGE_STATUS_DEPENDENCY_FAILURE, MAAGE_CONTEXT_RENDER, MAAGE_OPERATION_CREATE, MAAGE_STATUS_INVALID_STATE);
    pool = [[NSAutoreleasePool alloc] init];
    descriptor = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA8Unorm width:atlas->width height:atlas->height mipmapped:NO];
    descriptor.usage = MTLTextureUsageShaderRead;
    descriptor.storageMode = MTLStorageModeManaged;
    if (@available(macOS 10.15, *)) {
        if ([native->device supportsFamily:MTLGPUFamilyApple1]) descriptor.storageMode = MTLStorageModeShared;
    }
    texture = [native->device newTextureWithDescriptor:descriptor];
    if (texture) [texture replaceRegion:MTLRegionMake2D(0, 0, atlas->width, atlas->height) mipmapLevel:0 withBytes:atlas->pixels bytesPerRow:atlas->stride];
    resource->platform = texture;
    [pool drain];
    return texture ? MAAGE_STATUS_VALUE(MAAGE_STATUS_SUCCESS, MAAGE_CONTEXT_RENDER, MAAGE_OPERATION_CREATE, 0) : MAAGE_STATUS_VALUE(MAAGE_STATUS_ALLOCATION_FAILED, MAAGE_CONTEXT_RENDER, MAAGE_OPERATION_CREATE, 0);
}
static void maage_metal_destroy_texture(maage_render_texture *texture) {
    [(id<MTLTexture>) texture->platform release];
    texture->platform = NULL;
}
static maage_status maage_metal_upload_geometry(maage_renderer *renderer, maage_render_mesh *mesh, const maage_mesh_data *data) {
    maage_metal_renderer *native = renderer->platform;
    maage_metal_mesh *geometry;
    NSAutoreleasePool *pool;
    maage_status result;
    if (![NSThread isMainThread]) return MAAGE_STATUS_VALUE(MAAGE_STATUS_DEPENDENCY_FAILURE, MAAGE_CONTEXT_RENDER, MAAGE_OPERATION_UPLOAD, MAAGE_STATUS_INVALID_STATE);
    if (!maage_render_gpu_layout_supported()) return MAAGE_STATUS_VALUE(MAAGE_STATUS_NOT_IMPLEMENTED, MAAGE_CONTEXT_RENDER, MAAGE_OPERATION_UPLOAD, 0);
    pool = [[NSAutoreleasePool alloc] init];
    /* Clear-only renderers do not need the mesh shaders or depth state. */
    if (!native->pipelines[MAAGE_RENDER_SOLID]) {
        result = maage_metal_pipeline(renderer);
        if (maage_status_code(result) != MAAGE_STATUS_SUCCESS) {
            [pool drain];
            return result;
        }
    }
    geometry = calloc(1, sizeof(*geometry));
    if (!geometry) {
        [pool drain];
        return MAAGE_STATUS_VALUE(MAAGE_STATUS_ALLOCATION_FAILED, MAAGE_CONTEXT_RENDER, MAAGE_OPERATION_UPLOAD, 0);
    }
    mesh->platform = geometry;
    geometry->vertices = [native->device newBufferWithBytes:data->vertices length:data->vertex_count * sizeof(*data->vertices) options:MTLResourceStorageModeShared];
    geometry->indices = [native->device newBufferWithLength:data->index_count * sizeof(*data->indices) options:MTLResourceStorageModeShared];
    if (!geometry->vertices || !geometry->indices) {
        [pool drain];
        return maage_status_make(MAAGE_STATUS_ALLOCATION_FAILED, MAAGE_CONTEXT_RENDER, MAAGE_OPERATION_UPLOAD, 0);
    }
    result = maage_render_geometry_prepare(&mesh->geometry, data, [geometry->indices contents]);
    [pool drain];
    return result;
}
static void maage_metal_destroy_mesh(maage_render_mesh *mesh) {
    maage_metal_mesh *geometry = mesh->platform;
    unsigned int slot;
    if (!geometry) return;
    [geometry->vertices release];
    [geometry->indices release];
    for (slot = 0; slot < MAAGE_METAL_FRAME_SLOTS; ++slot)
        [geometry->sorted_indices[slot] release];
    free(geometry);
    mesh->platform = NULL;
}
static maage_status maage_metal_set_vsync(maage_renderer *renderer, int enabled) {
    maage_metal_renderer *native = (maage_metal_renderer *) renderer->platform;
    maage_status result;
    int start_presenter;
    maage_window_state state;
    if (![NSThread isMainThread]) return MAAGE_STATUS_VALUE(MAAGE_STATUS_INVALID_STATE, MAAGE_CONTEXT_RENDER, MAAGE_OPERATION_SET, MAAGE_DETAIL_WRONG_THREAD);
    if (!native || !native->layer) return MAAGE_STATUS_VALUE(MAAGE_STATUS_INVALID_STATE, MAAGE_CONTEXT_RENDER, MAAGE_OPERATION_SET, 0);
    result = maage_metal_present_result(renderer);
    if (maage_status_failed(result)) return result;
    /* Switching back on must leave mailbox mode. Simply setting
     * displaySyncEnabled=YES leaves the asynchronous presenter active and the
     * Client FPS counter no longer reflects display-paced frames. Avoid
     * joining the presenter while AppKit is changing fullscreen Spaces. */
    result = maage_window_get_state(renderer->window, &state);
    if (maage_status_failed(result)) return result;
    if (state.mode_transitioning || [(NSWindow *) renderer->native.handles.cocoa.window inLiveResize]) return MAAGE_STATUS_VALUE(MAAGE_STATUS_INVALID_STATE, MAAGE_CONTEXT_RENDER, MAAGE_OPERATION_SET, 0);
    if (enabled && native->asynchronous) {
        result = maage_metal_stop_presenter(native);
        if (maage_status_failed(result)) return result;
        {
            unsigned int slot;
            for (slot = 0; slot < MAAGE_METAL_FRAME_SLOTS; ++slot) {
                result = maage_metal_complete(renderer, slot);
                if (maage_status_failed(result)) return result;
            }
        }
        native->frame_slot = 0;
        native->drawable_retry_after = 0;
        native->presenting = -1;
    }
    start_presenter = !enabled && !native->asynchronous;
    [CATransaction begin];
    [CATransaction setDisableActions:YES];
    native->layer.displaySyncEnabled = enabled ? YES : NO;
    native->layer.framebufferOnly = (native->asynchronous || start_presenter) ? NO : YES;
    [CATransaction commit];
    [CATransaction flush];
    if (start_presenter) {
        maage_mutex_lock(native->present_mutex);
        native->stopping = 0;
        maage_mutex_unlock(native->present_mutex);
        result = maage_thread_start(maage_metal_present_loop, native, &native->present_thread);
        if (maage_status_failed(result)) {
            [CATransaction begin];
            native->layer.displaySyncEnabled = YES;
            native->layer.framebufferOnly = YES;
            [CATransaction commit];
            [CATransaction flush];
            return result;
        }
        native->asynchronous = 1;
    }
    return MAAGE_STATUS_VALUE(MAAGE_STATUS_SUCCESS, MAAGE_CONTEXT_RENDER, MAAGE_OPERATION_SET, 0);
}
const maage_render_backend_api maage_render_metal_api = {
    MAAGE_RENDER_METAL, maage_metal_create, maage_metal_destroy,
    MAAGE_RENDER_CAPABILITIES_VERSION, NULL, maage_metal_frame, maage_metal_upload_geometry,
    maage_metal_destroy_mesh, maage_metal_create_texture, maage_metal_destroy_texture,
    maage_metal_set_vsync};
