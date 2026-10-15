/* Native graphics declarations and WSI headers are confined to this adapter. */
#if defined(MAAGE_RENDER_WIN32)
#define VK_USE_PLATFORM_WIN32_KHR
#endif
#if defined(MAAGE_RENDER_X11)
#define VK_USE_PLATFORM_XLIB_KHR
#endif
#if defined(MAAGE_RENDER_WAYLAND)
#define VK_USE_PLATFORM_WAYLAND_KHR
#endif
#include <vulkan/vulkan.h>
#include <stdlib.h>
#include <string.h>
#include "../render_internal.h"

typedef struct maage_vulkan_renderer {
    VkInstance instance;
    VkSurfaceKHR surface;
    VkPhysicalDevice physical;
    VkDevice device;
    uint32_t graphics_family, present_family;
    VkQueue graphics_queue, present_queue;
    VkCommandPool pool;
    VkCommandBuffer command;
    VkFence fence;
    VkSemaphore acquired;
    VkSwapchainKHR swapchain;
    VkImage *images;
    VkSemaphore *finished;
    uint32_t image_count;
    int requested_width, requested_height;
} maage_vulkan_renderer;

static maage_render_result maage_vulkan_result(VkResult result)
{
    switch (result) {
    case VK_SUCCESS: return MAAGE_RENDER_OK;
    case VK_NOT_READY: case VK_TIMEOUT: case VK_ERROR_OUT_OF_DATE_KHR: return MAAGE_RENDER_SKIPPED;
    case VK_ERROR_OUT_OF_HOST_MEMORY: case VK_ERROR_OUT_OF_DEVICE_MEMORY: return MAAGE_RENDER_OUT_OF_MEMORY;
    case VK_ERROR_DEVICE_LOST: return MAAGE_RENDER_DEVICE_LOST;
    case VK_ERROR_SURFACE_LOST_KHR: return MAAGE_RENDER_SURFACE_LOST;
    case VK_ERROR_EXTENSION_NOT_PRESENT: case VK_ERROR_FEATURE_NOT_PRESENT: return MAAGE_RENDER_UNSUPPORTED;
    case VK_ERROR_INCOMPATIBLE_DRIVER: case VK_ERROR_INITIALIZATION_FAILED: return MAAGE_RENDER_UNAVAILABLE;
    default: return MAAGE_RENDER_PLATFORM_ERROR;
    }
}
static int maage_vulkan_instance_extension(const char *name)
{
    uint32_t count, index;
    VkExtensionProperties *properties;
    int found = 0;
    if (vkEnumerateInstanceExtensionProperties(NULL, &count, NULL) != VK_SUCCESS) return 0;
    properties = calloc(count, sizeof(*properties));
    if (!properties) return 0;
    if (vkEnumerateInstanceExtensionProperties(NULL, &count, properties) == VK_SUCCESS)
        for (index = 0; index < count; ++index)
            if (!strcmp(properties[index].extensionName, name)) found = 1;
    free(properties);
    return found;
}
static maage_render_result maage_vulkan_surface(maage_renderer *renderer)
{
    maage_vulkan_renderer *native = renderer->platform;
    const maage_window_native *window = &renderer->native;
#if defined(MAAGE_RENDER_WIN32)
    if (window->backend == MAAGE_WINDOW_WIN32) {
        VkWin32SurfaceCreateInfoKHR info;
        memset(&info, 0, sizeof(info));
        info.sType = VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR;
        info.hinstance = window->handles.win32.instance;
        info.hwnd = window->handles.win32.window;
        return maage_vulkan_result(vkCreateWin32SurfaceKHR(native->instance, &info, NULL, &native->surface));
    }
#endif
#if defined(MAAGE_RENDER_X11)
    if (window->backend == MAAGE_WINDOW_X11) {
        VkXlibSurfaceCreateInfoKHR info;
        memset(&info, 0, sizeof(info));
        info.sType = VK_STRUCTURE_TYPE_XLIB_SURFACE_CREATE_INFO_KHR;
        info.dpy = window->handles.x11.display;
        info.window = window->handles.x11.window;
        return maage_vulkan_result(vkCreateXlibSurfaceKHR(native->instance, &info, NULL, &native->surface));
    }
#endif
#if defined(MAAGE_RENDER_WAYLAND)
    if (window->backend == MAAGE_WINDOW_WAYLAND) {
        VkWaylandSurfaceCreateInfoKHR info;
        memset(&info, 0, sizeof(info));
        info.sType = VK_STRUCTURE_TYPE_WAYLAND_SURFACE_CREATE_INFO_KHR;
        info.display = window->handles.wayland.display;
        info.surface = window->handles.wayland.surface;
        return maage_vulkan_result(vkCreateWaylandSurfaceKHR(native->instance, &info, NULL, &native->surface));
    }
#endif
    return MAAGE_RENDER_UNSUPPORTED;
}
static int maage_vulkan_device_suitable(maage_vulkan_renderer *native, VkPhysicalDevice physical,
    uint32_t *graphics, uint32_t *present)
{
    VkExtensionProperties *extensions;
    VkQueueFamilyProperties *queues;
    VkSurfaceCapabilitiesKHR capabilities;
    uint32_t count, index, format_count, mode_count;
    int has_extension = 0, has_graphics = 0, has_present = 0;
    VkBool32 supported;
    if (vkEnumerateDeviceExtensionProperties(physical, NULL, &count, NULL) != VK_SUCCESS) return 0;
    extensions = calloc(count, sizeof(*extensions));
    if (!extensions) return 0;
    if (vkEnumerateDeviceExtensionProperties(physical, NULL, &count, extensions) == VK_SUCCESS)
        for (index = 0; index < count; ++index)
            if (!strcmp(extensions[index].extensionName, VK_KHR_SWAPCHAIN_EXTENSION_NAME)) has_extension = 1;
    free(extensions);
    if (!has_extension) return 0;
    vkGetPhysicalDeviceQueueFamilyProperties(physical, &count, NULL);
    queues = calloc(count, sizeof(*queues));
    if (!queues) return 0;
    vkGetPhysicalDeviceQueueFamilyProperties(physical, &count, queues);
    for (index = 0; index < count; ++index) {
        if (!queues[index].queueCount) continue;
        if (!has_graphics && (queues[index].queueFlags & VK_QUEUE_GRAPHICS_BIT)) {
            *graphics = index;
            has_graphics = 1;
        }
        supported = VK_FALSE;
        if (vkGetPhysicalDeviceSurfaceSupportKHR(physical, index, native->surface, &supported) == VK_SUCCESS && supported) {
            *present = index;
            has_present = 1;
        }
    }
    free(queues);
    if (!has_graphics || !has_present) return 0;
    if (vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physical, native->surface, &capabilities) != VK_SUCCESS ||
        !(capabilities.supportedUsageFlags & VK_IMAGE_USAGE_TRANSFER_DST_BIT)) return 0;
    if (vkGetPhysicalDeviceSurfaceFormatsKHR(physical, native->surface, &format_count, NULL) != VK_SUCCESS || !format_count) return 0;
    if (vkGetPhysicalDeviceSurfacePresentModesKHR(physical, native->surface, &mode_count, NULL) != VK_SUCCESS || !mode_count) return 0;
    return 1;
}
static maage_render_result maage_vulkan_create(maage_renderer *renderer)
{
    maage_vulkan_renderer *native;
    const char *extensions[2] = { VK_KHR_SURFACE_EXTENSION_NAME, NULL };
    const char *device_extension = VK_KHR_SWAPCHAIN_EXTENSION_NAME;
    VkApplicationInfo application;
    VkInstanceCreateInfo instance;
    VkPhysicalDevice *devices;
    VkDeviceQueueCreateInfo queues[2];
    VkDeviceCreateInfo device;
    VkCommandPoolCreateInfo pool;
    VkCommandBufferAllocateInfo command;
    VkFenceCreateInfo fence;
    VkSemaphoreCreateInfo semaphore;
    VkResult result;
    maage_render_result surface_result;
    uint32_t count, index, queue_count;
    float priority = 1.0f;
    switch (renderer->native.backend) {
#if defined(MAAGE_RENDER_WIN32)
    case MAAGE_WINDOW_WIN32: extensions[1] = VK_KHR_WIN32_SURFACE_EXTENSION_NAME; break;
#endif
#if defined(MAAGE_RENDER_X11)
    case MAAGE_WINDOW_X11: extensions[1] = VK_KHR_XLIB_SURFACE_EXTENSION_NAME; break;
#endif
#if defined(MAAGE_RENDER_WAYLAND)
    case MAAGE_WINDOW_WAYLAND: extensions[1] = VK_KHR_WAYLAND_SURFACE_EXTENSION_NAME; break;
#endif
    default: return MAAGE_RENDER_UNSUPPORTED;
    }
    if (!maage_vulkan_instance_extension(extensions[0]) || !maage_vulkan_instance_extension(extensions[1]))
        return MAAGE_RENDER_UNSUPPORTED;
    native = calloc(1, sizeof(*native));
    if (!native) return MAAGE_RENDER_OUT_OF_MEMORY;
    renderer->platform = native;
    memset(&application, 0, sizeof(application));
    application.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    application.pApplicationName = "MaaGE";
    application.pEngineName = "MaaGE";
    application.apiVersion = VK_API_VERSION_1_0;
    memset(&instance, 0, sizeof(instance));
    instance.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    instance.pApplicationInfo = &application;
    instance.enabledExtensionCount = 2;
    instance.ppEnabledExtensionNames = extensions;
    result = vkCreateInstance(&instance, NULL, &native->instance);
    if (result != VK_SUCCESS) return maage_vulkan_result(result);
    surface_result = maage_vulkan_surface(renderer);
    if (surface_result != MAAGE_RENDER_OK) return surface_result;
    result = vkEnumeratePhysicalDevices(native->instance, &count, NULL);
    if (result != VK_SUCCESS) return maage_vulkan_result(result);
    if (!count) return MAAGE_RENDER_UNAVAILABLE;
    devices = calloc(count, sizeof(*devices));
    if (!devices) return MAAGE_RENDER_OUT_OF_MEMORY;
    result = vkEnumeratePhysicalDevices(native->instance, &count, devices);
    if (result == VK_SUCCESS) {
        for (index = 0; index < count; ++index) {
            if (maage_vulkan_device_suitable(native, devices[index], &native->graphics_family, &native->present_family)) {
                native->physical = devices[index];
                break;
            }
        }
    }
    free(devices);
    if (result != VK_SUCCESS) return maage_vulkan_result(result);
    if (!native->physical) return MAAGE_RENDER_UNAVAILABLE;
    memset(queues, 0, sizeof(queues));
    queues[0].sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    queues[0].queueFamilyIndex = native->graphics_family;
    queues[0].queueCount = 1;
    queues[0].pQueuePriorities = &priority;
    queue_count = 1;
    if (native->present_family != native->graphics_family) {
        queues[1] = queues[0];
        queues[1].queueFamilyIndex = native->present_family;
        queue_count = 2;
    }
    memset(&device, 0, sizeof(device));
    device.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    device.queueCreateInfoCount = queue_count;
    device.pQueueCreateInfos = queues;
    device.enabledExtensionCount = 1;
    device.ppEnabledExtensionNames = &device_extension;
    result = vkCreateDevice(native->physical, &device, NULL, &native->device);
    if (result != VK_SUCCESS) return maage_vulkan_result(result);
    vkGetDeviceQueue(native->device, native->graphics_family, 0, &native->graphics_queue);
    vkGetDeviceQueue(native->device, native->present_family, 0, &native->present_queue);
    memset(&pool, 0, sizeof(pool));
    pool.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    pool.queueFamilyIndex = native->graphics_family;
    pool.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    result = vkCreateCommandPool(native->device, &pool, NULL, &native->pool);
    if (result != VK_SUCCESS) return maage_vulkan_result(result);
    memset(&command, 0, sizeof(command));
    command.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    command.commandPool = native->pool;
    command.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    command.commandBufferCount = 1;
    result = vkAllocateCommandBuffers(native->device, &command, &native->command);
    if (result != VK_SUCCESS) return maage_vulkan_result(result);
    memset(&fence, 0, sizeof(fence));
    fence.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fence.flags = VK_FENCE_CREATE_SIGNALED_BIT;
    result = vkCreateFence(native->device, &fence, NULL, &native->fence);
    if (result != VK_SUCCESS) return maage_vulkan_result(result);
    memset(&semaphore, 0, sizeof(semaphore));
    semaphore.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
    return maage_vulkan_result(vkCreateSemaphore(native->device, &semaphore, NULL, &native->acquired));
}
static void maage_vulkan_drop_swapchain(maage_vulkan_renderer *native)
{
    uint32_t index;
    if (native->finished) {
        for (index = 0; index < native->image_count; ++index)
            if (native->finished[index]) vkDestroySemaphore(native->device, native->finished[index], NULL);
    }
    free(native->finished);
    free(native->images);
    native->finished = NULL;
    native->images = NULL;
    native->image_count = 0;
    if (native->swapchain) vkDestroySwapchainKHR(native->device, native->swapchain, NULL);
    native->swapchain = VK_NULL_HANDLE;
}
static void maage_vulkan_destroy(maage_renderer *renderer)
{
    maage_vulkan_renderer *native = renderer->platform;
    if (!native) return;
    if (native->device) {
        vkDeviceWaitIdle(native->device);
        maage_vulkan_drop_swapchain(native);
        if (native->acquired) vkDestroySemaphore(native->device, native->acquired, NULL);
        if (native->fence) vkDestroyFence(native->device, native->fence, NULL);
        if (native->pool) vkDestroyCommandPool(native->device, native->pool, NULL);
        vkDestroyDevice(native->device, NULL);
    }
    if (native->surface) vkDestroySurfaceKHR(native->instance, native->surface, NULL);
    if (native->instance) vkDestroyInstance(native->instance, NULL);
    free(native);
}
static maage_render_result maage_vulkan_swapchain(maage_renderer *renderer, const maage_window_state *state)
{
    maage_vulkan_renderer *native = renderer->platform;
    VkSurfaceCapabilitiesKHR capabilities;
    VkSurfaceFormatKHR *formats, selected;
    VkSwapchainCreateInfoKHR info;
    VkSemaphoreCreateInfo semaphore;
    VkExtent2D extent;
    VkCompositeAlphaFlagBitsKHR alpha;
    VkResult result;
    uint32_t count, index, families[2];
    result = vkGetPhysicalDeviceSurfaceCapabilitiesKHR(native->physical, native->surface, &capabilities);
    if (result != VK_SUCCESS) return maage_vulkan_result(result);
    if (!(capabilities.supportedUsageFlags & VK_IMAGE_USAGE_TRANSFER_DST_BIT)) return MAAGE_RENDER_UNSUPPORTED;
    extent = capabilities.currentExtent;
    if (extent.width == (uint32_t)-1) {
        extent.width = (uint32_t)state->framebuffer_width;
        extent.height = (uint32_t)state->framebuffer_height;
        if (extent.width < capabilities.minImageExtent.width) extent.width = capabilities.minImageExtent.width;
        if (extent.width > capabilities.maxImageExtent.width) extent.width = capabilities.maxImageExtent.width;
        if (extent.height < capabilities.minImageExtent.height) extent.height = capabilities.minImageExtent.height;
        if (extent.height > capabilities.maxImageExtent.height) extent.height = capabilities.maxImageExtent.height;
    }
    if (!extent.width || !extent.height) return MAAGE_RENDER_SKIPPED;
    result = vkGetPhysicalDeviceSurfaceFormatsKHR(native->physical, native->surface, &count, NULL);
    if (result != VK_SUCCESS) return maage_vulkan_result(result);
    if (!count) return MAAGE_RENDER_UNSUPPORTED;
    formats = calloc(count, sizeof(*formats));
    if (!formats) return MAAGE_RENDER_OUT_OF_MEMORY;
    result = vkGetPhysicalDeviceSurfaceFormatsKHR(native->physical, native->surface, &count, formats);
    if (result != VK_SUCCESS) { free(formats); return maage_vulkan_result(result); }
    selected = formats[0];
    selected.format = VK_FORMAT_UNDEFINED;
    if (count == 1 && formats[0].format == VK_FORMAT_UNDEFINED) selected.format = VK_FORMAT_B8G8R8A8_UNORM;
    for (index = 0; index < count; ++index) {
        if (selected.format == VK_FORMAT_UNDEFINED &&
            (formats[index].format == VK_FORMAT_R8G8B8A8_UNORM || formats[index].format == VK_FORMAT_R8G8B8A8_SRGB ||
             formats[index].format == VK_FORMAT_B8G8R8A8_UNORM || formats[index].format == VK_FORMAT_B8G8R8A8_SRGB))
            selected = formats[index];
        if (formats[index].format == VK_FORMAT_B8G8R8A8_UNORM && formats[index].colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR)
            selected = formats[index];
    }
    free(formats);
    if (selected.format == VK_FORMAT_UNDEFINED) return MAAGE_RENDER_UNSUPPORTED;
    for (index = 0; index < 4; ++index)
        if (capabilities.supportedCompositeAlpha & (1u << index)) break;
    if (index == 4) return MAAGE_RENDER_UNSUPPORTED;
    alpha = (VkCompositeAlphaFlagBitsKHR)(1u << index);
    result = vkDeviceWaitIdle(native->device);
    if (result != VK_SUCCESS) return maage_vulkan_result(result);
    /* Baseline Vulkan WSI: idle before replacing presentation resources. */
    maage_vulkan_drop_swapchain(native);
    memset(&info, 0, sizeof(info));
    info.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    info.surface = native->surface;
    info.minImageCount = capabilities.minImageCount;
    if (!capabilities.maxImageCount || info.minImageCount < capabilities.maxImageCount) ++info.minImageCount;
    info.imageFormat = selected.format;
    info.imageColorSpace = selected.colorSpace;
    info.imageExtent = extent;
    info.imageArrayLayers = 1;
    info.imageUsage = VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    families[0] = native->graphics_family;
    families[1] = native->present_family;
    info.imageSharingMode = families[0] == families[1] ? VK_SHARING_MODE_EXCLUSIVE : VK_SHARING_MODE_CONCURRENT;
    if (info.imageSharingMode == VK_SHARING_MODE_CONCURRENT) {
        info.queueFamilyIndexCount = 2;
        info.pQueueFamilyIndices = families;
    }
    info.preTransform = capabilities.currentTransform;
    info.compositeAlpha = alpha;
    info.presentMode = VK_PRESENT_MODE_FIFO_KHR;
    info.clipped = VK_TRUE;
    result = vkCreateSwapchainKHR(native->device, &info, NULL, &native->swapchain);
    if (result != VK_SUCCESS) return maage_vulkan_result(result);
    result = vkGetSwapchainImagesKHR(native->device, native->swapchain, &native->image_count, NULL);
    if (result != VK_SUCCESS) return maage_vulkan_result(result);
    native->images = calloc(native->image_count, sizeof(*native->images));
    native->finished = calloc(native->image_count, sizeof(*native->finished));
    if (!native->images || !native->finished) return MAAGE_RENDER_OUT_OF_MEMORY;
    result = vkGetSwapchainImagesKHR(native->device, native->swapchain, &native->image_count, native->images);
    if (result != VK_SUCCESS) return maage_vulkan_result(result);
    memset(&semaphore, 0, sizeof(semaphore));
    semaphore.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
    /* Presentation waits belong to swapchain images, not frame fences. */
    for (index = 0; index < native->image_count; ++index) {
        result = vkCreateSemaphore(native->device, &semaphore, NULL, &native->finished[index]);
        if (result != VK_SUCCESS) return maage_vulkan_result(result);
    }
    native->requested_width = state->framebuffer_width;
    native->requested_height = state->framebuffer_height;
    renderer->refresh = 0;
    return MAAGE_RENDER_OK;
}
static maage_render_result maage_vulkan_present(maage_renderer *renderer, const maage_window_state *state)
{
    maage_vulkan_renderer *native = renderer->platform;
    VkResult result, presented;
    maage_render_result rebuilt;
    VkCommandBufferBeginInfo begin;
    VkImageMemoryBarrier barrier;
    VkClearColorValue color;
    VkSubmitInfo submit;
    VkPresentInfoKHR present;
    VkPipelineStageFlags wait_stage = VK_PIPELINE_STAGE_TRANSFER_BIT;
    uint32_t image, index;
    int suboptimal;
    if (renderer->refresh || !native->swapchain || native->requested_width != state->framebuffer_width ||
        native->requested_height != state->framebuffer_height) {
        rebuilt = maage_vulkan_swapchain(renderer, state);
        if (rebuilt != MAAGE_RENDER_OK) return rebuilt;
    }
    result = vkWaitForFences(native->device, 1, &native->fence, VK_TRUE, 16000000);
    if (result != VK_SUCCESS) return maage_vulkan_result(result);
    result = vkAcquireNextImageKHR(native->device, native->swapchain, 0, native->acquired, VK_NULL_HANDLE, &image);
    if (result == VK_ERROR_OUT_OF_DATE_KHR) { renderer->refresh = 1; return MAAGE_RENDER_SKIPPED; }
    suboptimal = result == VK_SUBOPTIMAL_KHR;
    if (result != VK_SUCCESS && !suboptimal) return maage_vulkan_result(result);
    result = vkResetCommandBuffer(native->command, 0);
    if (result != VK_SUCCESS) return maage_vulkan_result(result);
    memset(&begin, 0, sizeof(begin));
    begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    result = vkBeginCommandBuffer(native->command, &begin);
    if (result != VK_SUCCESS) return maage_vulkan_result(result);
    memset(&barrier, 0, sizeof(barrier));
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = native->images[image];
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.levelCount = 1;
    barrier.subresourceRange.layerCount = 1;
    barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    vkCmdPipelineBarrier(native->command, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
        0, 0, NULL, 0, NULL, 1, &barrier);
    for (index = 0; index < 4; ++index) color.float32[index] = renderer->clear[index];
    vkCmdClearColorImage(native->command, native->images[image], VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        &color, 1, &barrier.subresourceRange);
    barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    barrier.dstAccessMask = 0;
    vkCmdPipelineBarrier(native->command, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
        0, 0, NULL, 0, NULL, 1, &barrier);
    result = vkEndCommandBuffer(native->command);
    if (result != VK_SUCCESS) return maage_vulkan_result(result);
    memset(&submit, 0, sizeof(submit));
    submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submit.waitSemaphoreCount = 1;
    submit.pWaitSemaphores = &native->acquired;
    submit.pWaitDstStageMask = &wait_stage;
    submit.commandBufferCount = 1;
    submit.pCommandBuffers = &native->command;
    submit.signalSemaphoreCount = 1;
    submit.pSignalSemaphores = &native->finished[image];
    result = vkResetFences(native->device, 1, &native->fence);
    if (result != VK_SUCCESS) return maage_vulkan_result(result);
    result = vkQueueSubmit(native->graphics_queue, 1, &submit, native->fence);
    if (result != VK_SUCCESS) return maage_vulkan_result(result);
    memset(&present, 0, sizeof(present));
    present.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    present.waitSemaphoreCount = 1;
    present.pWaitSemaphores = &native->finished[image];
    present.swapchainCount = 1;
    present.pSwapchains = &native->swapchain;
    present.pImageIndices = &image;
    presented = vkQueuePresentKHR(native->present_queue, &present);
    if (suboptimal || presented == VK_SUBOPTIMAL_KHR || presented == VK_ERROR_OUT_OF_DATE_KHR) renderer->refresh = 1;
    if (presented == VK_ERROR_OUT_OF_DATE_KHR) return MAAGE_RENDER_SKIPPED;
    return presented == VK_SUBOPTIMAL_KHR ? MAAGE_RENDER_OK : maage_vulkan_result(presented);
}
const maage_render_backend_api maage_render_vulkan_api = {
    MAAGE_RENDER_VULKAN, maage_vulkan_create, maage_vulkan_destroy, maage_vulkan_present
};
