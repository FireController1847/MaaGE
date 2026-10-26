#import <AppKit/AppKit.h>
#import <QuartzCore/CAMetalLayer.h>
#import <QuartzCore/CATransaction.h>
#include "vulkan_internal.h"
#include <stdlib.h>

typedef struct maage_vulkan_cocoa {
    NSView *view;
    CAMetalLayer *layer;
    CALayer *previous_layer;
    BOOL previous_wants_layer, installed;
} maage_vulkan_cocoa;

static maage_status maage_vulkan_cocoa_status(maage_renderer *renderer, unsigned char code)
{
    return MAAGE_STATUS_PROPAGATE(renderer->operation_trace,
        maage_status_make(code, MAAGE_CONTEXT_RENDER, renderer->trace_operation, 0),
        MAAGE_CONTEXT_RENDER, renderer->trace_operation);
}
maage_status maage_vulkan_cocoa_update(maage_renderer *renderer, const maage_window_state *state)
{
    maage_vulkan_renderer *native = renderer->platform;
    maage_vulkan_cocoa *cocoa = native->cocoa;
    CGFloat scale;
    CGSize size;
    if (![NSThread isMainThread])
        return maage_vulkan_cocoa_status(renderer, MAAGE_STATUS_INVALID_STATE);
    if (!cocoa || !cocoa->installed)
        return maage_vulkan_cocoa_status(renderer, MAAGE_STATUS_INVALID_STATE);
    /* Let AppKit finish a Space transition before recreating the swapchain.
     * The window module retains this same viewport across those transitions. */
    if (state->mode_transitioning)
        return maage_vulkan_cocoa_status(renderer, MAAGE_STATUS_NOOP);
    scale = [[cocoa->view window] backingScaleFactor];
    if (scale <= 0) scale = 1;
    size = CGSizeMake(state->framebuffer_width, state->framebuffer_height);
    if (CGRectEqualToRect(cocoa->layer.frame, NSRectToCGRect([cocoa->view bounds])) &&
        cocoa->layer.contentsScale == scale &&
        (size.width <= 0 || size.height <= 0 || CGSizeEqualToSize(cocoa->layer.drawableSize, size)))
        return maage_vulkan_cocoa_status(renderer, MAAGE_STATUS_SUCCESS);
    [CATransaction begin];
    [CATransaction setDisableActions:YES];
    if (!CGRectEqualToRect(cocoa->layer.frame, NSRectToCGRect([cocoa->view bounds])))
        cocoa->layer.frame = NSRectToCGRect([cocoa->view bounds]);
    if (cocoa->layer.contentsScale != scale) cocoa->layer.contentsScale = scale;
    if (size.width > 0 && size.height > 0 && !CGSizeEqualToSize(cocoa->layer.drawableSize, size))
        cocoa->layer.drawableSize = size;
    [CATransaction commit];
    return maage_vulkan_cocoa_status(renderer, MAAGE_STATUS_SUCCESS);
}
maage_status maage_vulkan_cocoa_surface(maage_renderer *renderer)
{
    maage_vulkan_renderer *native = renderer->platform;
    maage_vulkan_cocoa *cocoa = native->cocoa;
    VkMetalSurfaceCreateInfoEXT info = {0};
    maage_window_state state;
    maage_status result;
    if (![NSThread isMainThread] || renderer->native.backend != MAAGE_WINDOW_COCOA ||
        !renderer->native.handles.cocoa.view)
        return maage_vulkan_cocoa_status(renderer, MAAGE_STATUS_INVALID_STATE);
    if (!cocoa) {
        cocoa = calloc(1, sizeof(*cocoa));
        if (!cocoa) return maage_vulkan_cocoa_status(renderer, MAAGE_STATUS_ALLOCATION_FAILED);
        native->cocoa = cocoa;
        cocoa->view = [(NSView *)renderer->native.handles.cocoa.view retain];
        cocoa->previous_layer = [[cocoa->view layer] retain];
        cocoa->previous_wants_layer = [cocoa->view wantsLayer];
        cocoa->layer = [[CAMetalLayer alloc] init];
        if (!cocoa->layer) return maage_vulkan_cocoa_status(renderer, MAAGE_STATUS_ALLOCATION_FAILED);
        cocoa->layer.opaque = YES;
        cocoa->layer.delegate = (id<CALayerDelegate>)cocoa->view;
        cocoa->layer.autoresizingMask = kCALayerWidthSizable | kCALayerHeightSizable;
        [cocoa->view setLayer:cocoa->layer];
        [cocoa->view setWantsLayer:YES];
        cocoa->installed = YES;
    }
    result = maage_window_get_state(renderer->window, &state);
    if (maage_status_failed(result)) return result;
    result = maage_vulkan_cocoa_update(renderer, &state);
    if (maage_status_code(result) != MAAGE_STATUS_SUCCESS) return result;
    info.sType = VK_STRUCTURE_TYPE_METAL_SURFACE_CREATE_INFO_EXT;
    info.pLayer = cocoa->layer;
    return maage_vulkan_result(vkCreateMetalSurfaceEXT(native->instance, &info, NULL, &native->surface));
}
maage_status maage_vulkan_cocoa_destroy(maage_renderer *renderer)
{
    maage_vulkan_renderer *native = renderer->platform;
    maage_vulkan_cocoa *cocoa = native->cocoa;
    if (!cocoa) return maage_vulkan_cocoa_status(renderer, MAAGE_STATUS_NOOP);
    if (![NSThread isMainThread])
        return maage_vulkan_cocoa_status(renderer, MAAGE_STATUS_INVALID_STATE);
    if (cocoa->installed) {
        [cocoa->view setLayer:cocoa->previous_layer];
        [cocoa->view setWantsLayer:cocoa->previous_wants_layer];
    }
    cocoa->layer.delegate = nil;
    [cocoa->previous_layer release];
    [cocoa->layer release];
    [cocoa->view release];
    free(cocoa); native->cocoa = NULL;
    return maage_vulkan_cocoa_status(renderer, MAAGE_STATUS_SUCCESS);
}
