#include "vulkan_internal.h"
#include <stdlib.h>
#include <string.h>

static maage_status maage_vulkan_status(maage_renderer *renderer,
    unsigned char code, unsigned char detail)
{
    return maage_status_make(code, MAAGE_CONTEXT_RENDER,
        renderer->trace_operation, detail);
}
maage_status maage_vulkan_record(maage_renderer *renderer, VkResult value,
    const char *file, unsigned long line)
{
    maage_vulkan_renderer *native = renderer->platform;
    unsigned char code = MAAGE_STATUS_ERROR, detail = 0;
    maage_status result;
    switch (value) {
    case VK_SUCCESS: case VK_SUBOPTIMAL_KHR: code = MAAGE_STATUS_SUCCESS; break;
    case VK_INCOMPLETE: code = MAAGE_STATUS_PARTIAL; break;
    case VK_NOT_READY: case VK_TIMEOUT: case VK_ERROR_OUT_OF_DATE_KHR:
        code = MAAGE_STATUS_NOOP; break;
    case VK_ERROR_SURFACE_LOST_KHR:
        code = MAAGE_STATUS_NOOP; detail = MAAGE_DETAIL_SURFACE_LOST;
        if (native) native->surface_lost = 1;
        break;
    case VK_ERROR_OUT_OF_HOST_MEMORY: case VK_ERROR_OUT_OF_DEVICE_MEMORY:
        code = MAAGE_STATUS_ALLOCATION_FAILED; break;
    case VK_ERROR_DEVICE_LOST:
        code = MAAGE_STATUS_FATAL; detail = MAAGE_DETAIL_DEVICE_LOST; break;
    case VK_ERROR_EXTENSION_NOT_PRESENT: case VK_ERROR_FEATURE_NOT_PRESENT:
    case VK_ERROR_FORMAT_NOT_SUPPORTED: case VK_ERROR_INCOMPATIBLE_DRIVER:
        code = MAAGE_STATUS_SYSTEM_NOT_SUPPORTED; break;
    case VK_ERROR_INITIALIZATION_FAILED: code = MAAGE_STATUS_INVALID_STATE; break;
    default: break;
    }
    result = maage_vulkan_status(renderer, code, detail);
    if (value != VK_SUCCESS && value != VK_NOT_READY && value != VK_TIMEOUT)
        maage_status_trace_record(renderer->operation_trace, result,
            MAAGE_CONTEXT_RENDER, renderer->trace_operation, file, line, (long)value);
    if (native && value == VK_ERROR_DEVICE_LOST) native->failure = result;
    if (value == VK_SUBOPTIMAL_KHR || value == VK_ERROR_OUT_OF_DATE_KHR ||
        value == VK_ERROR_SURFACE_LOST_KHR) renderer->refresh = 1;
    return result;
}
static maage_status maage_vulkan_instance_extension(maage_renderer *renderer,
    const char *name, int *found)
{
    maage_vulkan_renderer *native = renderer->platform;
    VkExtensionProperties *properties;
    uint32_t count = 0, index;
    VkResult result;
    (void)native;
    *found = 0;
    result = vkEnumerateInstanceExtensionProperties(NULL, &count, NULL);
    if (result != VK_SUCCESS) return maage_vulkan_result(result);
    if (!count) return maage_vulkan_status(renderer, MAAGE_STATUS_SUCCESS, 0);
    if ((size_t)count > (size_t)-1 / sizeof(*properties))
        return maage_vulkan_status(renderer, MAAGE_STATUS_BUFFER_OVERFLOW, 0);
    properties = calloc(count, sizeof(*properties));
    if (!properties) return maage_vulkan_status(renderer, MAAGE_STATUS_ALLOCATION_FAILED, 0);
    result = vkEnumerateInstanceExtensionProperties(NULL, &count, properties);
    if (result == VK_SUCCESS)
        for (index = 0; index < count; ++index)
            if (!strcmp(properties[index].extensionName, name)) *found = 1;
    free(properties);
    return maage_vulkan_result(result);
}
static maage_status maage_vulkan_surface(maage_renderer *renderer)
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
    return maage_vulkan_status(renderer, MAAGE_STATUS_SYSTEM_NOT_SUPPORTED, 0);
}
static VkFormat maage_vulkan_depth(maage_vulkan_renderer *native, VkPhysicalDevice physical)
{
    static const VkFormat formats[] = { VK_FORMAT_D32_SFLOAT,
        VK_FORMAT_D24_UNORM_S8_UINT, VK_FORMAT_D16_UNORM };
    VkFormatProperties properties;
    unsigned int index;
    (void)native;
    for (index = 0; index < sizeof(formats) / sizeof(formats[0]); ++index) {
        vkGetPhysicalDeviceFormatProperties(physical, formats[index], &properties);
        if (properties.optimalTilingFeatures & VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT)
            return formats[index];
    }
    return VK_FORMAT_UNDEFINED;
}
static maage_status maage_vulkan_suitable(maage_renderer *renderer,
    VkPhysicalDevice physical, uint32_t *graphics, uint32_t *present,
    int *maintenance, int *suitable)
{
    maage_vulkan_renderer *native = renderer->platform;
    VkExtensionProperties *extensions;
    VkQueueFamilyProperties *queues;
    VkSurfaceCapabilitiesKHR capabilities;
    VkFormatProperties format;
    VkResult result;
    VkBool32 supported;
    uint32_t count = 0, index, formats, modes;
    int swapchain = 0, has_graphics = 0, has_present = 0, unified = 0;
    *suitable = *maintenance = 0;
    result = vkEnumerateDeviceExtensionProperties(physical, NULL, &count, NULL);
    if (result != VK_SUCCESS) return maage_vulkan_result(result);
    if (!count) return maage_vulkan_status(renderer, MAAGE_STATUS_SUCCESS, 0);
    if ((size_t)count > (size_t)-1 / sizeof(*extensions))
        return maage_vulkan_status(renderer, MAAGE_STATUS_BUFFER_OVERFLOW, 0);
    extensions = calloc(count, sizeof(*extensions));
    if (!extensions) return maage_vulkan_status(renderer, MAAGE_STATUS_ALLOCATION_FAILED, 0);
    result = vkEnumerateDeviceExtensionProperties(physical, NULL, &count, extensions);
    if (result == VK_SUCCESS)
        for (index = 0; index < count; ++index) {
            if (!strcmp(extensions[index].extensionName, VK_KHR_SWAPCHAIN_EXTENSION_NAME)) swapchain = 1;
#if defined(VK_EXT_swapchain_maintenance1)
            if ((native->maintenance_instance & 1) && !*maintenance && !strcmp(extensions[index].extensionName,
                VK_EXT_SWAPCHAIN_MAINTENANCE_1_EXTENSION_NAME)) *maintenance = 1;
            if ((native->maintenance_instance & 2) && !strcmp(extensions[index].extensionName,
                "VK_KHR_swapchain_maintenance1")) *maintenance = 2;
#endif
        }
    free(extensions);
    if (result != VK_SUCCESS) return maage_vulkan_result(result);
    if (!swapchain || !*maintenance) return maage_vulkan_status(renderer, MAAGE_STATUS_SUCCESS, 0);
#if defined(VK_EXT_swapchain_maintenance1)
    {
        VkPhysicalDeviceSwapchainMaintenance1FeaturesEXT support = {0};
        VkPhysicalDeviceFeatures2KHR features = {0};
        PFN_vkGetPhysicalDeviceFeatures2KHR query = (PFN_vkGetPhysicalDeviceFeatures2KHR)
            vkGetInstanceProcAddr(native->instance, "vkGetPhysicalDeviceFeatures2KHR");
        if (!query) return maage_vulkan_status(renderer, MAAGE_STATUS_SUCCESS, 0);
        support.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SWAPCHAIN_MAINTENANCE_1_FEATURES_EXT;
        features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2_KHR;
        features.pNext = &support; query(physical, &features);
        if (!support.swapchainMaintenance1) return maage_vulkan_status(renderer, MAAGE_STATUS_SUCCESS, 0);
    }
#endif
    vkGetPhysicalDeviceQueueFamilyProperties(physical, &count, NULL);
    if (!count) return maage_vulkan_status(renderer, MAAGE_STATUS_SUCCESS, 0);
    if ((size_t)count > (size_t)-1 / sizeof(*queues))
        return maage_vulkan_status(renderer, MAAGE_STATUS_BUFFER_OVERFLOW, 0);
    queues = calloc(count, sizeof(*queues));
    if (!queues) return maage_vulkan_status(renderer, MAAGE_STATUS_ALLOCATION_FAILED, 0);
    vkGetPhysicalDeviceQueueFamilyProperties(physical, &count, queues);
    for (index = 0; index < count; ++index) {
        if (!queues[index].queueCount) continue;
        supported = VK_FALSE;
        result = vkGetPhysicalDeviceSurfaceSupportKHR(physical, index, native->surface, &supported);
        if (result != VK_SUCCESS) { free(queues); return maage_vulkan_result(result); }
        if (!has_graphics && (queues[index].queueFlags & VK_QUEUE_GRAPHICS_BIT)) {
            *graphics = index; has_graphics = 1;
        }
        if (!has_present && supported) { *present = index; has_present = 1; }
        if (!unified && supported && (queues[index].queueFlags & VK_QUEUE_GRAPHICS_BIT)) {
            *graphics = *present = index; has_graphics = has_present = unified = 1;
        }
    }
    free(queues);
    if (!has_graphics || !has_present) return maage_vulkan_status(renderer, MAAGE_STATUS_SUCCESS, 0);
    result = vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physical, native->surface, &capabilities);
    if (result != VK_SUCCESS) return maage_vulkan_result(result);
    if (!(capabilities.supportedUsageFlags & VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT))
        return maage_vulkan_status(renderer, MAAGE_STATUS_SUCCESS, 0);
    vkGetPhysicalDeviceFormatProperties(physical, VK_FORMAT_R8G8B8A8_UNORM, &format);
    if ((format.optimalTilingFeatures & (VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT |
        VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT | VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BLEND_BIT |
        VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT)) !=
        (VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT | VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT |
        VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BLEND_BIT | VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT))
        return maage_vulkan_status(renderer, MAAGE_STATUS_SUCCESS, 0);
    if (maage_vulkan_depth(native, physical) == VK_FORMAT_UNDEFINED)
        return maage_vulkan_status(renderer, MAAGE_STATUS_SUCCESS, 0);
    result = vkGetPhysicalDeviceSurfaceFormatsKHR(physical, native->surface, &formats, NULL);
    if (result != VK_SUCCESS) return maage_vulkan_result(result);
    result = vkGetPhysicalDeviceSurfacePresentModesKHR(physical, native->surface, &modes, NULL);
    if (result != VK_SUCCESS) return maage_vulkan_result(result);
    *suitable = formats && modes;
    return maage_vulkan_status(renderer, MAAGE_STATUS_SUCCESS, 0);
}
maage_status maage_vulkan_platform_create(maage_renderer *renderer)
{
    maage_vulkan_renderer *native;
    const char *extensions[6] = { VK_KHR_SURFACE_EXTENSION_NAME, NULL };
    const char *device_extensions[2] = { VK_KHR_SWAPCHAIN_EXTENSION_NAME, NULL };
    VkApplicationInfo application;
    VkInstanceCreateInfo instance;
    VkPhysicalDevice *devices;
    VkPhysicalDeviceProperties properties;
    VkDeviceQueueCreateInfo queues[2];
    VkDeviceCreateInfo device;
    VkCommandPoolCreateInfo pool;
    VkCommandBufferAllocateInfo allocate;
    VkFenceCreateInfo fence;
    VkSemaphoreCreateInfo semaphore;
    VkResult value;
    uint32_t count = 0, index, graphics, present, queue_count, extension_count = 2;
    int found, suitable, maintenance, score, selected_score = -1;
    float priority = 1.0f;
    maage_status result, first_failure = 0;
#if defined(VK_EXT_swapchain_maintenance1)
    VkPhysicalDeviceSwapchainMaintenance1FeaturesEXT maintenance_feature;
    VkPhysicalDeviceFeatures2KHR features;
    PFN_vkGetPhysicalDeviceFeatures2KHR get_features;
    int properties2, surface2, surface_maintenance, khr_surface_maintenance;
#endif
    native = calloc(1, sizeof(*native));
    if (!native) return maage_vulkan_status(renderer, MAAGE_STATUS_ALLOCATION_FAILED, 0);
    renderer->platform = native;
    native->vsync = 1;
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
    default: return maage_vulkan_status(renderer, MAAGE_STATUS_SYSTEM_NOT_SUPPORTED, 0);
    }
#if defined(MAAGE_RENDER_WIN32)
    result = maage_vulkan_load_win32(renderer, &native->library, &native->dispatch);
    if (maage_status_code(result) != MAAGE_STATUS_SUCCESS) return result;
#endif
    for (index = 0; index < 2; ++index) {
        result = maage_vulkan_instance_extension(renderer, extensions[index], &found);
        if (maage_status_code(result) != MAAGE_STATUS_SUCCESS) return result;
        if (!found) return maage_vulkan_status(renderer, MAAGE_STATUS_SYSTEM_NOT_SUPPORTED, 0);
    }
#if defined(VK_EXT_swapchain_maintenance1)
    result = maage_vulkan_instance_extension(renderer, VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME, &properties2);
    if (maage_status_code(result) != MAAGE_STATUS_SUCCESS) return result;
    result = maage_vulkan_instance_extension(renderer, VK_KHR_GET_SURFACE_CAPABILITIES_2_EXTENSION_NAME, &surface2);
    if (maage_status_code(result) != MAAGE_STATUS_SUCCESS) return result;
    result = maage_vulkan_instance_extension(renderer, VK_EXT_SURFACE_MAINTENANCE_1_EXTENSION_NAME, &surface_maintenance);
    if (maage_status_code(result) != MAAGE_STATUS_SUCCESS) return result;
    result = maage_vulkan_instance_extension(renderer, "VK_KHR_surface_maintenance1", &khr_surface_maintenance);
    if (maage_status_code(result) != MAAGE_STATUS_SUCCESS) return result;
    if (properties2 && surface2 && (surface_maintenance || khr_surface_maintenance)) {
        extensions[extension_count++] = VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME;
        extensions[extension_count++] = VK_KHR_GET_SURFACE_CAPABILITIES_2_EXTENSION_NAME;
        if (surface_maintenance) {
            extensions[extension_count++] = VK_EXT_SURFACE_MAINTENANCE_1_EXTENSION_NAME;
            native->maintenance_instance |= 1;
        }
        if (khr_surface_maintenance) {
            extensions[extension_count++] = "VK_KHR_surface_maintenance1";
            native->maintenance_instance |= 2;
        }
    }
#endif
    memset(&application, 0, sizeof(application));
    application.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    application.pApplicationName = application.pEngineName = "MaaGE";
    application.apiVersion = VK_API_VERSION_1_0;
    memset(&instance, 0, sizeof(instance));
    instance.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    instance.pApplicationInfo = &application;
    instance.enabledExtensionCount = extension_count;
    instance.ppEnabledExtensionNames = extensions;
    value = vkCreateInstance(&instance, NULL, &native->instance);
    if (value != VK_SUCCESS) return maage_vulkan_result(value);
    result = maage_vulkan_surface(renderer);
    if (maage_status_code(result) != MAAGE_STATUS_SUCCESS) return result;
    value = vkEnumeratePhysicalDevices(native->instance, &count, NULL);
    if (value != VK_SUCCESS) return maage_vulkan_result(value);
    if (!count) return maage_vulkan_status(renderer, MAAGE_STATUS_SYSTEM_NOT_SUPPORTED, 0);
    if ((size_t)count > (size_t)-1 / sizeof(*devices))
        return maage_vulkan_status(renderer, MAAGE_STATUS_BUFFER_OVERFLOW, 0);
    devices = calloc(count, sizeof(*devices));
    if (!devices) return maage_vulkan_status(renderer, MAAGE_STATUS_ALLOCATION_FAILED, 0);
    value = vkEnumeratePhysicalDevices(native->instance, &count, devices);
    if (value == VK_SUCCESS)
        for (index = 0; index < count; ++index) {
            result = maage_vulkan_suitable(renderer, devices[index], &graphics, &present, &maintenance, &suitable);
            if (maage_status_code(result) != MAAGE_STATUS_SUCCESS) {
                if (!first_failure) first_failure = result;
                continue;
            }
            if (!suitable) continue;
            vkGetPhysicalDeviceProperties(devices[index], &properties);
            score = properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU ? 3 :
                properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU ? 2 : 1;
            if (score > selected_score) {
                native->physical = devices[index];
                native->graphics_family = graphics;
                native->present_family = present;
                native->maintenance = maintenance;
                native->properties = properties;
                selected_score = score;
            }
        }
    free(devices);
    if (value != VK_SUCCESS) return maage_vulkan_result(value);
    if (!native->physical) return first_failure ? first_failure :
        maage_vulkan_status(renderer, MAAGE_STATUS_SYSTEM_NOT_SUPPORTED, 0);
    native->depth_format = maage_vulkan_depth(native, native->physical);
    vkGetPhysicalDeviceMemoryProperties(native->physical, &native->memory);
    memcpy(renderer->device_name, native->properties.deviceName, sizeof(renderer->device_name));
    renderer->device_name[sizeof(renderer->device_name) - 1] = '\0';
    renderer->capabilities.max_texture_dimension_2d = native->properties.limits.maxImageDimension2D;
    renderer->capabilities.synchronization = MAAGE_RENDER_SYNC_CPU_WAIT |
        MAAGE_RENDER_SYNC_FENCE | MAAGE_RENDER_SYNC_BINARY_SEMAPHORE;
#if defined(VK_EXT_swapchain_maintenance1)
    memset(&maintenance_feature, 0, sizeof(maintenance_feature));
    maintenance_feature.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SWAPCHAIN_MAINTENANCE_1_FEATURES_EXT;
    if (native->maintenance) {
        get_features = (PFN_vkGetPhysicalDeviceFeatures2KHR)vkGetInstanceProcAddr(native->instance,
            "vkGetPhysicalDeviceFeatures2KHR");
        if (get_features) {
            memset(&features, 0, sizeof(features));
            features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2_KHR;
            features.pNext = &maintenance_feature;
            get_features(native->physical, &features);
            if (!maintenance_feature.swapchainMaintenance1) native->maintenance = 0;
        } else native->maintenance = 0;
    }
#endif
    /* Base Vulkan has no presentation-completion proof for terminal teardown.
     * Require native present fences rather than releasing in-use semaphores. */
    if (!native->maintenance)
        return maage_vulkan_status(renderer, MAAGE_STATUS_SYSTEM_NOT_SUPPORTED, 0);
    memset(queues, 0, sizeof(queues));
    queues[0].sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    queues[0].queueFamilyIndex = native->graphics_family;
    queues[0].queueCount = 1;
    queues[0].pQueuePriorities = &priority;
    queue_count = 1;
    if (native->present_family != native->graphics_family) {
        queues[1] = queues[0]; queues[1].queueFamilyIndex = native->present_family;
        queue_count = 2;
    }
    memset(&device, 0, sizeof(device));
    device.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    device.queueCreateInfoCount = queue_count;
    device.pQueueCreateInfos = queues;
    device.enabledExtensionCount = 1;
    device.ppEnabledExtensionNames = device_extensions;
#if defined(VK_EXT_swapchain_maintenance1)
    if (native->maintenance) {
        device_extensions[1] = native->maintenance == 2 ?
            "VK_KHR_swapchain_maintenance1" : VK_EXT_SWAPCHAIN_MAINTENANCE_1_EXTENSION_NAME;
        device.enabledExtensionCount = 2;
        device.pNext = &maintenance_feature;
    }
#endif
    value = vkCreateDevice(native->physical, &device, NULL, &native->device);
    if (value != VK_SUCCESS) return maage_vulkan_result(value);
    vkGetDeviceQueue(native->device, native->graphics_family, 0, &native->graphics_queue);
    vkGetDeviceQueue(native->device, native->present_family, 0, &native->present_queue);
    memset(&pool, 0, sizeof(pool));
    pool.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    pool.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    pool.queueFamilyIndex = native->graphics_family;
    value = vkCreateCommandPool(native->device, &pool, NULL, &native->pool);
    if (value != VK_SUCCESS) return maage_vulkan_result(value);
    memset(&allocate, 0, sizeof(allocate));
    allocate.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocate.commandPool = native->pool;
    allocate.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocate.commandBufferCount = 1;
    memset(&fence, 0, sizeof(fence));
    fence.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fence.flags = VK_FENCE_CREATE_SIGNALED_BIT;
    memset(&semaphore, 0, sizeof(semaphore));
    semaphore.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
    for (index = 0; index < MAAGE_VULKAN_FRAME_SLOTS; ++index) {
        value = vkAllocateCommandBuffers(native->device, &allocate, &native->frames[index].command);
        if (value != VK_SUCCESS) return maage_vulkan_result(value);
        value = vkCreateFence(native->device, &fence, NULL, &native->frames[index].fence);
        if (value != VK_SUCCESS) return maage_vulkan_result(value);
        value = vkCreateSemaphore(native->device, &semaphore, NULL, &native->frames[index].acquired);
        if (value != VK_SUCCESS) return maage_vulkan_result(value);
    }
    return maage_vulkan_status(renderer, MAAGE_STATUS_SUCCESS, 0);
}
static void maage_vulkan_drop_swapchain(maage_renderer *renderer)
{
    maage_vulkan_renderer *native = renderer->platform;
    uint32_t index;
    maage_vulkan_targets_destroy(renderer);
    for (index = 0; index < native->image_count; ++index) {
        if (native->views && native->views[index]) vkDestroyImageView(native->device, native->views[index], NULL);
        if (native->finished && native->finished[index]) vkDestroySemaphore(native->device, native->finished[index], NULL);
        if (native->present_fences && native->present_fences[index]) vkDestroyFence(native->device, native->present_fences[index], NULL);
    }
    free(native->images); native->images = NULL;
    free(native->views); native->views = NULL;
    free(native->finished); native->finished = NULL;
    free(native->present_fences); native->present_fences = NULL;
    free(native->present_pending); native->present_pending = NULL;
    free(native->present_traces); native->present_traces = NULL;
    native->image_count = 0;
    if (native->swapchain) vkDestroySwapchainKHR(native->device, native->swapchain, NULL);
    native->swapchain = VK_NULL_HANDLE;
}
static maage_status maage_vulkan_present_complete(maage_renderer *renderer, uint64_t timeout)
{
    maage_vulkan_renderer *native = renderer->platform;
    uint32_t index;
    VkResult value;
    if (native->present_fences && native->present_pending)
        for (index = 0; index < native->image_count; ++index) {
            if (!native->present_pending[index]) continue;
            value = vkWaitForFences(native->device, 1, &native->present_fences[index], VK_TRUE, timeout);
            if (value != VK_SUCCESS) {
                if (value != VK_TIMEOUT && renderer->operation_trace && native->present_traces)
                    *renderer->operation_trace = native->present_traces[index];
                return maage_vulkan_result(value);
            }
            native->present_pending[index] = 0;
        }
    return maage_vulkan_status(renderer, MAAGE_STATUS_SUCCESS, 0);
}
maage_status maage_vulkan_platform_destroy(maage_renderer *renderer)
{
    maage_vulkan_renderer *native = renderer->platform;
    maage_status result = maage_vulkan_status(renderer, MAAGE_STATUS_SUCCESS, 0), waited;
    uint32_t index;
    if (!native) return maage_vulkan_status(renderer, MAAGE_STATUS_NOOP, 0);
    if (native->device) {
        result = maage_vulkan_result(vkDeviceWaitIdle(native->device));
        if (!maage_status_failed(result)) {
            waited = maage_vulkan_present_complete(renderer, UINT64_MAX);
            if (maage_status_failed(waited)) result = waited;
        }
        maage_vulkan_drop_swapchain(renderer);
        for (index = 0; index < MAAGE_VULKAN_FRAME_SLOTS; ++index) {
            if (native->frames[index].acquired) vkDestroySemaphore(native->device, native->frames[index].acquired, NULL);
            if (native->frames[index].fence) vkDestroyFence(native->device, native->frames[index].fence, NULL);
        }
        if (native->pool) vkDestroyCommandPool(native->device, native->pool, NULL);
        vkDestroyDevice(native->device, NULL);
    }
    if (native->surface) vkDestroySurfaceKHR(native->instance, native->surface, NULL);
    if (native->instance) vkDestroyInstance(native->instance, NULL);
#if defined(MAAGE_RENDER_WIN32)
    if (native->library && !FreeLibrary(native->library)) {
        DWORD error = GetLastError();
        waited = error ? maage_status_from_win32(error, MAAGE_CONTEXT_RENDER, MAAGE_OPERATION_DESTROY) :
            maage_vulkan_status(renderer, MAAGE_STATUS_IO_ERROR, 0);
        maage_status_trace_record(renderer->operation_trace, waited, MAAGE_CONTEXT_RENDER,
            MAAGE_OPERATION_DESTROY, __FILE__, __LINE__, (long)error);
        if (!maage_status_failed(result)) result = waited;
    }
#endif
    free(native); renderer->platform = NULL;
    return result;
}
static maage_status maage_vulkan_rebuild(maage_renderer *renderer, const maage_window_state *state)
{
    maage_vulkan_renderer *native = renderer->platform;
    VkSurfaceCapabilitiesKHR capabilities;
    VkSurfaceFormatKHR *formats, selected;
    VkPresentModeKHR *modes, mode = VK_PRESENT_MODE_FIFO_KHR;
    VkExtent2D extent;
    VkSwapchainCreateInfoKHR info;
    VkImageViewCreateInfo view;
    VkSemaphoreCreateInfo semaphore;
    VkFenceCreateInfo fence;
    VkResult value;
    VkBool32 supported;
    uint32_t count = 0, index, families[2], image_count;
    int score, selected_score = -1, mailbox = 0, immediate = 0;
    maage_status result;
    result = maage_vulkan_present_complete(renderer, 0);
    if (maage_status_code(result) != MAAGE_STATUS_SUCCESS) return result;
    if (native->surface_lost) {
        value = vkDeviceWaitIdle(native->device);
        if (value != VK_SUCCESS) return maage_vulkan_result(value);
        maage_vulkan_drop_swapchain(renderer);
        if (native->surface) vkDestroySurfaceKHR(native->instance, native->surface, NULL);
        native->surface = VK_NULL_HANDLE;
        result = maage_window_get_native(renderer->window, &renderer->native);
        if (maage_status_code(result) != MAAGE_STATUS_SUCCESS) return result;
        result = maage_vulkan_surface(renderer);
        if (maage_status_code(result) != MAAGE_STATUS_SUCCESS) return result;
        native->surface_lost = 0;
        value = vkGetPhysicalDeviceSurfaceSupportKHR(native->physical, native->present_family,
            native->surface, &supported);
        if (value != VK_SUCCESS) return maage_vulkan_result(value);
        if (!supported) return maage_vulkan_status(renderer, MAAGE_STATUS_SYSTEM_NOT_SUPPORTED, 0);
    }
    value = vkGetPhysicalDeviceSurfaceCapabilitiesKHR(native->physical, native->surface, &capabilities);
    if (value != VK_SUCCESS) return maage_vulkan_result(value);
    if (!(capabilities.supportedUsageFlags & VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT))
        return maage_vulkan_status(renderer, MAAGE_STATUS_SYSTEM_NOT_SUPPORTED, 0);
    extent = capabilities.currentExtent;
    if (extent.width == UINT32_MAX) {
        extent.width = (uint32_t)state->framebuffer_width;
        extent.height = (uint32_t)state->framebuffer_height;
        if (extent.width < capabilities.minImageExtent.width) extent.width = capabilities.minImageExtent.width;
        if (extent.width > capabilities.maxImageExtent.width) extent.width = capabilities.maxImageExtent.width;
        if (extent.height < capabilities.minImageExtent.height) extent.height = capabilities.minImageExtent.height;
        if (extent.height > capabilities.maxImageExtent.height) extent.height = capabilities.maxImageExtent.height;
    }
    if (!extent.width || !extent.height) return maage_vulkan_status(renderer, MAAGE_STATUS_NOOP, 0);
    /* The scene, blur and depth images use the drawable extent too. A surface
     * extent alone does not validate those ordinary images and framebuffers. */
    if (extent.width > native->properties.limits.maxImageDimension2D ||
        extent.height > native->properties.limits.maxImageDimension2D ||
        extent.width > native->properties.limits.maxFramebufferWidth ||
        extent.height > native->properties.limits.maxFramebufferHeight ||
        extent.width > native->properties.limits.maxViewportDimensions[0] ||
        extent.height > native->properties.limits.maxViewportDimensions[1])
        return maage_vulkan_status(renderer, MAAGE_STATUS_SYSTEM_NOT_SUPPORTED, 0);
    value = vkGetPhysicalDeviceSurfaceFormatsKHR(native->physical, native->surface, &count, NULL);
    if (value != VK_SUCCESS) return maage_vulkan_result(value);
    if (!count) return maage_vulkan_status(renderer, MAAGE_STATUS_SYSTEM_NOT_SUPPORTED, 0);
    if ((size_t)count > (size_t)-1 / sizeof(*formats))
        return maage_vulkan_status(renderer, MAAGE_STATUS_BUFFER_OVERFLOW, 0);
    formats = calloc(count, sizeof(*formats));
    if (!formats) return maage_vulkan_status(renderer, MAAGE_STATUS_ALLOCATION_FAILED, 0);
    value = vkGetPhysicalDeviceSurfaceFormatsKHR(native->physical, native->surface, &count, formats);
    memset(&selected, 0, sizeof(selected));
    if (value == VK_SUCCESS)
        for (index = 0; index < count; ++index) {
            if (formats[index].colorSpace != VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) continue;
            score = -1;
            switch (formats[index].format) {
            case VK_FORMAT_UNDEFINED: formats[index].format = VK_FORMAT_B8G8R8A8_UNORM; score = 4; break;
            case VK_FORMAT_B8G8R8A8_UNORM: score = 4; break;
            case VK_FORMAT_R8G8B8A8_UNORM: score = 3; break;
            case VK_FORMAT_B8G8R8A8_SRGB: score = 2; break;
            case VK_FORMAT_R8G8B8A8_SRGB: score = 1; break;
            default: break;
            }
            if (score > selected_score) { selected = formats[index]; selected_score = score; }
        }
    free(formats);
    if (value != VK_SUCCESS) return maage_vulkan_result(value);
    if (selected_score < 0) return maage_vulkan_status(renderer, MAAGE_STATUS_SYSTEM_NOT_SUPPORTED, 0);
    value = vkGetPhysicalDeviceSurfacePresentModesKHR(native->physical, native->surface, &count, NULL);
    if (value != VK_SUCCESS) return maage_vulkan_result(value);
    if (!count) return maage_vulkan_status(renderer, MAAGE_STATUS_SYSTEM_NOT_SUPPORTED, 0);
    if ((size_t)count > (size_t)-1 / sizeof(*modes))
        return maage_vulkan_status(renderer, MAAGE_STATUS_BUFFER_OVERFLOW, 0);
    modes = calloc(count, sizeof(*modes));
    if (!modes) return maage_vulkan_status(renderer, MAAGE_STATUS_ALLOCATION_FAILED, 0);
    value = vkGetPhysicalDeviceSurfacePresentModesKHR(native->physical, native->surface, &count, modes);
    if (value == VK_SUCCESS)
        for (index = 0; index < count; ++index) {
            if (modes[index] == VK_PRESENT_MODE_MAILBOX_KHR) mailbox = 1;
            if (modes[index] == VK_PRESENT_MODE_IMMEDIATE_KHR) immediate = 1;
        }
    free(modes);
    if (value != VK_SUCCESS) return maage_vulkan_result(value);
    if (!native->vsync) {
        if (mailbox) mode = VK_PRESENT_MODE_MAILBOX_KHR;
        else if (immediate) mode = VK_PRESENT_MODE_IMMEDIATE_KHR;
    }
    if (!capabilities.supportedCompositeAlpha || capabilities.minImageCount == UINT32_MAX)
        return maage_vulkan_status(renderer, MAAGE_STATUS_INVALID_STATE, 0);
    image_count = capabilities.minImageCount + 1;
    if (capabilities.maxImageCount && image_count > capabilities.maxImageCount)
        image_count = capabilities.maxImageCount;
    if (image_count < capabilities.minImageCount)
        return maage_vulkan_status(renderer, MAAGE_STATUS_INVALID_STATE, 0);
    value = vkDeviceWaitIdle(native->device);
    if (value != VK_SUCCESS) return maage_vulkan_result(value);
    for (index = 0; index < MAAGE_VULKAN_FRAME_SLOTS; ++index) {
        value = vkResetCommandBuffer(native->frames[index].command, 0);
        if (value != VK_SUCCESS) return maage_vulkan_result(value);
        native->frames[index].in_flight = 0;
    }
    maage_vulkan_drop_swapchain(renderer);
    memset(&info, 0, sizeof(info));
    info.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    info.surface = native->surface;
    info.minImageCount = image_count;
    info.imageFormat = selected.format;
    info.imageColorSpace = selected.colorSpace;
    info.imageExtent = extent;
    info.imageArrayLayers = 1;
    info.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    families[0] = native->graphics_family; families[1] = native->present_family;
    info.imageSharingMode = families[0] == families[1] ? VK_SHARING_MODE_EXCLUSIVE : VK_SHARING_MODE_CONCURRENT;
    if (info.imageSharingMode == VK_SHARING_MODE_CONCURRENT) {
        info.queueFamilyIndexCount = 2; info.pQueueFamilyIndices = families;
    }
    info.preTransform = capabilities.currentTransform;
    for (index = 0; index < 4; ++index)
        if (capabilities.supportedCompositeAlpha & (1u << index)) break;
    info.compositeAlpha = (VkCompositeAlphaFlagBitsKHR)(1u << index);
    info.presentMode = mode;
    info.clipped = VK_TRUE;
    value = vkCreateSwapchainKHR(native->device, &info, NULL, &native->swapchain);
    if (value != VK_SUCCESS) return maage_vulkan_result(value);
    value = vkGetSwapchainImagesKHR(native->device, native->swapchain, &native->image_count, NULL);
    if (value != VK_SUCCESS) return maage_vulkan_result(value);
    if (!native->image_count) return maage_vulkan_status(renderer, MAAGE_STATUS_INVALID_STATE, 0);
    if ((size_t)native->image_count > (size_t)-1 / sizeof(*native->images) ||
        (size_t)native->image_count > (size_t)-1 / sizeof(*native->views) ||
        (size_t)native->image_count > (size_t)-1 / sizeof(*native->finished) ||
        (size_t)native->image_count > (size_t)-1 / sizeof(*native->present_fences))
        return maage_vulkan_status(renderer, MAAGE_STATUS_BUFFER_OVERFLOW, 0);
    native->images = calloc(native->image_count, sizeof(*native->images));
    native->views = calloc(native->image_count, sizeof(*native->views));
    native->finished = calloc(native->image_count, sizeof(*native->finished));
    if (!native->images || !native->views || !native->finished)
        return maage_vulkan_status(renderer, MAAGE_STATUS_ALLOCATION_FAILED, 0);
    if (native->maintenance) {
        native->present_fences = calloc(native->image_count, sizeof(*native->present_fences));
        native->present_pending = calloc(native->image_count, sizeof(*native->present_pending));
        native->present_traces = calloc(native->image_count, sizeof(*native->present_traces));
        if (!native->present_fences || !native->present_pending || !native->present_traces)
            return maage_vulkan_status(renderer, MAAGE_STATUS_ALLOCATION_FAILED, 0);
    }
    value = vkGetSwapchainImagesKHR(native->device, native->swapchain, &native->image_count, native->images);
    if (value != VK_SUCCESS) return maage_vulkan_result(value);
    memset(&view, 0, sizeof(view));
    view.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    view.viewType = VK_IMAGE_VIEW_TYPE_2D;
    view.format = selected.format;
    view.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    view.subresourceRange.levelCount = view.subresourceRange.layerCount = 1;
    memset(&semaphore, 0, sizeof(semaphore));
    semaphore.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
    memset(&fence, 0, sizeof(fence)); fence.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    for (index = 0; index < native->image_count; ++index) {
        view.image = native->images[index];
        value = vkCreateImageView(native->device, &view, NULL, &native->views[index]);
        if (value != VK_SUCCESS) return maage_vulkan_result(value);
        value = vkCreateSemaphore(native->device, &semaphore, NULL, &native->finished[index]);
        if (value != VK_SUCCESS) return maage_vulkan_result(value);
        if (native->maintenance) {
            value = vkCreateFence(native->device, &fence, NULL, &native->present_fences[index]);
            if (value != VK_SUCCESS) return maage_vulkan_result(value);
        }
    }
    native->format = selected.format; native->color_space = selected.colorSpace;
    native->extent = extent; native->present_mode = mode;
    native->requested_width = state->framebuffer_width;
    native->requested_height = state->framebuffer_height;
    ++native->generation;
    renderer->refresh = 0;
    return maage_vulkan_status(renderer, MAAGE_STATUS_SUCCESS, 0);
}
maage_status maage_vulkan_frame_poll(maage_renderer *renderer, maage_vulkan_frame *frame)
{
    maage_vulkan_renderer *native = renderer->platform;
    VkResult value = frame->in_flight ? vkGetFenceStatus(native->device, frame->fence) : VK_SUCCESS;
    if (value != VK_SUCCESS && value != VK_NOT_READY && renderer->operation_trace)
        *renderer->operation_trace = frame->trace;
    return maage_vulkan_result(value);
}
maage_status maage_vulkan_begin(maage_renderer *renderer,
    const maage_window_state *state, int preparing,
    maage_vulkan_frame **output, uint32_t *image)
{
    maage_vulkan_renderer *native = renderer->platform;
    maage_vulkan_frame *frame = &native->frames[native->frame_index];
    VkCommandBufferBeginInfo begin;
    VkResult value;
    maage_status result;
    *output = NULL; *image = 0;
    if (native->failure) return native->failure;
    /* Preserve any acquired image across the rare host-side presentation-fence
     * poll. Its semaphore must be consumed before recreation or reuse. */
    if (!frame->has_image) {
        if ((!preparing && !state->visible) || state->framebuffer_width <= 0 || state->framebuffer_height <= 0)
            return maage_vulkan_status(renderer, MAAGE_STATUS_NOOP, 0);
        if (renderer->refresh || !native->swapchain ||
            native->requested_width != state->framebuffer_width ||
            native->requested_height != state->framebuffer_height) {
            result = maage_vulkan_rebuild(renderer, state);
            if (maage_status_code(result) != MAAGE_STATUS_SUCCESS) return result;
        }
        if (frame->in_flight) {
            result = maage_vulkan_frame_poll(renderer, frame);
            if (maage_status_code(result) != MAAGE_STATUS_SUCCESS) return result;
            frame->in_flight = 0;
        }
    }
    if (preparing == 1) { *output = frame; return maage_vulkan_status(renderer, MAAGE_STATUS_SUCCESS, 0); }
    if (!frame->has_image) {
        value = vkAcquireNextImageKHR(native->device, native->swapchain, 0,
            frame->acquired, VK_NULL_HANDLE, &frame->image);
        if (value != VK_SUCCESS && value != VK_SUBOPTIMAL_KHR) return maage_vulkan_result(value);
        frame->has_image = 1;
        if (value == VK_SUBOPTIMAL_KHR) maage_vulkan_result(value);
    }
    if (native->present_fences && native->present_pending[frame->image]) {
        value = vkGetFenceStatus(native->device, native->present_fences[frame->image]);
        if (value != VK_SUCCESS) {
            if (value != VK_NOT_READY && renderer->operation_trace)
                *renderer->operation_trace = native->present_traces[frame->image];
            return maage_vulkan_result(value);
        }
        native->present_pending[frame->image] = 0;
    }
    value = vkResetCommandBuffer(frame->command, 0);
    if (value != VK_SUCCESS) {
        native->failure = maage_vulkan_result(value);
        return native->failure;
    }
    memset(&begin, 0, sizeof(begin));
    begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    value = vkBeginCommandBuffer(frame->command, &begin);
    if (value != VK_SUCCESS) {
        native->failure = maage_vulkan_result(value);
        return native->failure;
    }
    *output = frame; *image = frame->image;
    return maage_vulkan_status(renderer, MAAGE_STATUS_SUCCESS, 0);
}
maage_status maage_vulkan_submit(maage_renderer *renderer,
    maage_vulkan_frame *frame, uint32_t image)
{
    maage_vulkan_renderer *native = renderer->platform;
    VkSubmitInfo submit;
    VkPresentInfoKHR present;
    VkPipelineStageFlags stage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    VkResult value;
#if defined(VK_EXT_swapchain_maintenance1)
    VkSwapchainPresentFenceInfoEXT present_fence;
#endif
    if (!frame || !frame->has_image || frame->image != image || image >= native->image_count)
        return maage_vulkan_status(renderer, MAAGE_STATUS_INVALID_STATE, 0);
    value = vkEndCommandBuffer(frame->command);
    if (value != VK_SUCCESS) { native->failure = maage_vulkan_result(value); return native->failure; }
    memset(&submit, 0, sizeof(submit));
    submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submit.waitSemaphoreCount = 1;
    submit.pWaitSemaphores = &frame->acquired;
    submit.pWaitDstStageMask = &stage;
    submit.commandBufferCount = 1; submit.pCommandBuffers = &frame->command;
    submit.signalSemaphoreCount = 1; submit.pSignalSemaphores = &native->finished[image];
    value = vkResetFences(native->device, 1, &frame->fence);
    if (value != VK_SUCCESS) { native->failure = maage_vulkan_result(value); return native->failure; }
    value = vkQueueSubmit(native->graphics_queue, 1, &submit, frame->fence);
    if (value != VK_SUCCESS) { native->failure = maage_vulkan_result(value); return native->failure; }
    if (renderer->operation_trace) frame->trace = *renderer->operation_trace;
    else maage_status_trace_init(&frame->trace);
    MAAGE_STATUS_TRACE(&frame->trace, 0, MAAGE_CONTEXT_RENDER, MAAGE_OPERATION_SUBMIT);
    frame->in_flight = 1; frame->has_image = 0;
    native->frame_index = (native->frame_index + 1) % MAAGE_VULKAN_FRAME_SLOTS;
    memset(&present, 0, sizeof(present));
    present.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    present.waitSemaphoreCount = 1; present.pWaitSemaphores = &native->finished[image];
    present.swapchainCount = 1; present.pSwapchains = &native->swapchain;
    present.pImageIndices = &image;
#if defined(VK_EXT_swapchain_maintenance1)
    if (native->maintenance) {
        value = vkResetFences(native->device, 1, &native->present_fences[image]);
        if (value != VK_SUCCESS) { native->failure = maage_vulkan_result(value); return native->failure; }
        memset(&present_fence, 0, sizeof(present_fence));
        present_fence.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_PRESENT_FENCE_INFO_EXT;
        present_fence.swapchainCount = 1; present_fence.pFences = &native->present_fences[image];
        present.pNext = &present_fence;
        native->present_traces[image] = frame->trace;
    }
#endif
    value = vkQueuePresentKHR(native->present_queue, &present);
    /* Rejected out-of-date/surface-lost presents still enqueue semaphore waits.
     * Their maintenance fences are required before dropping presentation data. */
    if (native->maintenance && (value == VK_SUCCESS || value == VK_SUBOPTIMAL_KHR ||
        value == VK_ERROR_OUT_OF_DATE_KHR || value == VK_ERROR_SURFACE_LOST_KHR))
        native->present_pending[image] = 1;
    if (value != VK_SUCCESS && value != VK_SUBOPTIMAL_KHR &&
        value != VK_ERROR_OUT_OF_DATE_KHR && value != VK_ERROR_SURFACE_LOST_KHR)
        native->failure = maage_vulkan_result(value);
    return maage_vulkan_result(value);
}
maage_status maage_vulkan_abort(maage_renderer *renderer,
    maage_vulkan_frame *frame, uint32_t image)
{
    maage_vulkan_renderer *native = renderer->platform;
    VkCommandBufferBeginInfo begin;
    VkImageMemoryBarrier barrier;
    VkResult value;
    if (!frame || !frame->has_image || image >= native->image_count)
        return maage_vulkan_status(renderer, MAAGE_STATUS_NOOP, 0);
    value = vkResetCommandBuffer(frame->command, 0);
    if (value != VK_SUCCESS) { native->failure = maage_vulkan_result(value); return native->failure; }
    memset(&begin, 0, sizeof(begin));
    begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    value = vkBeginCommandBuffer(frame->command, &begin);
    if (value != VK_SUCCESS) { native->failure = maage_vulkan_result(value); return native->failure; }
    memset(&barrier, 0, sizeof(barrier));
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    barrier.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    barrier.srcQueueFamilyIndex = barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = native->images[image];
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.levelCount = barrier.subresourceRange.layerCount = 1;
    vkCmdPipelineBarrier(frame->command, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
        VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, 0, 0, NULL, 0, NULL, 1, &barrier);
    return maage_vulkan_submit(renderer, frame, image);
}
