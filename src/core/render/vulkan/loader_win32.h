#ifndef MAAGE_VULKAN_LOADER_WIN32_H
#define MAAGE_VULKAN_LOADER_WIN32_H
/* Per-renderer dispatch avoids load-time Vulkan DLL and mutable global state.
 * Core 1.0 and installed Win32/swapchain entry points come from the system loader;
 * optional extension commands are queried through vkGetInstanceProcAddr. */
#define MAAGE_VULKAN_FUNCTIONS(MAAGE_VULKAN_FUNCTION) \
    MAAGE_VULKAN_FUNCTION(vkAcquireNextImageKHR) \
    MAAGE_VULKAN_FUNCTION(vkAllocateCommandBuffers) \
    MAAGE_VULKAN_FUNCTION(vkAllocateDescriptorSets) \
    MAAGE_VULKAN_FUNCTION(vkAllocateMemory) \
    MAAGE_VULKAN_FUNCTION(vkBeginCommandBuffer) \
    MAAGE_VULKAN_FUNCTION(vkBindBufferMemory) \
    MAAGE_VULKAN_FUNCTION(vkBindImageMemory) \
    MAAGE_VULKAN_FUNCTION(vkCmdBeginRenderPass) \
    MAAGE_VULKAN_FUNCTION(vkCmdBindDescriptorSets) \
    MAAGE_VULKAN_FUNCTION(vkCmdBindIndexBuffer) \
    MAAGE_VULKAN_FUNCTION(vkCmdBindPipeline) \
    MAAGE_VULKAN_FUNCTION(vkCmdBindVertexBuffers) \
    MAAGE_VULKAN_FUNCTION(vkCmdClearAttachments) \
    MAAGE_VULKAN_FUNCTION(vkCmdClearColorImage) \
    MAAGE_VULKAN_FUNCTION(vkCmdCopyBuffer) \
    MAAGE_VULKAN_FUNCTION(vkCmdCopyBufferToImage) \
    MAAGE_VULKAN_FUNCTION(vkCmdCopyImage) \
    MAAGE_VULKAN_FUNCTION(vkCmdDraw) \
    MAAGE_VULKAN_FUNCTION(vkCmdDrawIndexed) \
    MAAGE_VULKAN_FUNCTION(vkCmdEndRenderPass) \
    MAAGE_VULKAN_FUNCTION(vkCmdPipelineBarrier) \
    MAAGE_VULKAN_FUNCTION(vkCmdPushConstants) \
    MAAGE_VULKAN_FUNCTION(vkCmdSetScissor) \
    MAAGE_VULKAN_FUNCTION(vkCmdSetViewport) \
    MAAGE_VULKAN_FUNCTION(vkCreateBuffer) \
    MAAGE_VULKAN_FUNCTION(vkCreateCommandPool) \
    MAAGE_VULKAN_FUNCTION(vkCreateDescriptorPool) \
    MAAGE_VULKAN_FUNCTION(vkCreateDescriptorSetLayout) \
    MAAGE_VULKAN_FUNCTION(vkCreateDevice) \
    MAAGE_VULKAN_FUNCTION(vkCreateFence) \
    MAAGE_VULKAN_FUNCTION(vkCreateFramebuffer) \
    MAAGE_VULKAN_FUNCTION(vkCreateGraphicsPipelines) \
    MAAGE_VULKAN_FUNCTION(vkCreateImage) \
    MAAGE_VULKAN_FUNCTION(vkCreateImageView) \
    MAAGE_VULKAN_FUNCTION(vkCreateInstance) \
    MAAGE_VULKAN_FUNCTION(vkCreatePipelineCache) \
    MAAGE_VULKAN_FUNCTION(vkCreatePipelineLayout) \
    MAAGE_VULKAN_FUNCTION(vkCreateRenderPass) \
    MAAGE_VULKAN_FUNCTION(vkCreateSampler) \
    MAAGE_VULKAN_FUNCTION(vkCreateSemaphore) \
    MAAGE_VULKAN_FUNCTION(vkCreateShaderModule) \
    MAAGE_VULKAN_FUNCTION(vkCreateSwapchainKHR) \
    MAAGE_VULKAN_FUNCTION(vkCreateWin32SurfaceKHR) \
    MAAGE_VULKAN_FUNCTION(vkDestroyBuffer) \
    MAAGE_VULKAN_FUNCTION(vkDestroyCommandPool) \
    MAAGE_VULKAN_FUNCTION(vkDestroyDescriptorPool) \
    MAAGE_VULKAN_FUNCTION(vkDestroyDescriptorSetLayout) \
    MAAGE_VULKAN_FUNCTION(vkDestroyDevice) \
    MAAGE_VULKAN_FUNCTION(vkDestroyFence) \
    MAAGE_VULKAN_FUNCTION(vkDestroyFramebuffer) \
    MAAGE_VULKAN_FUNCTION(vkDestroyImage) \
    MAAGE_VULKAN_FUNCTION(vkDestroyImageView) \
    MAAGE_VULKAN_FUNCTION(vkDestroyInstance) \
    MAAGE_VULKAN_FUNCTION(vkDestroyPipeline) \
    MAAGE_VULKAN_FUNCTION(vkDestroyPipelineCache) \
    MAAGE_VULKAN_FUNCTION(vkDestroyPipelineLayout) \
    MAAGE_VULKAN_FUNCTION(vkDestroyRenderPass) \
    MAAGE_VULKAN_FUNCTION(vkDestroySampler) \
    MAAGE_VULKAN_FUNCTION(vkDestroySemaphore) \
    MAAGE_VULKAN_FUNCTION(vkDestroyShaderModule) \
    MAAGE_VULKAN_FUNCTION(vkDestroySurfaceKHR) \
    MAAGE_VULKAN_FUNCTION(vkDestroySwapchainKHR) \
    MAAGE_VULKAN_FUNCTION(vkDeviceWaitIdle) \
    MAAGE_VULKAN_FUNCTION(vkEndCommandBuffer) \
    MAAGE_VULKAN_FUNCTION(vkEnumerateDeviceExtensionProperties) \
    MAAGE_VULKAN_FUNCTION(vkEnumerateInstanceExtensionProperties) \
    MAAGE_VULKAN_FUNCTION(vkEnumeratePhysicalDevices) \
    MAAGE_VULKAN_FUNCTION(vkFlushMappedMemoryRanges) \
    MAAGE_VULKAN_FUNCTION(vkFreeCommandBuffers) \
    MAAGE_VULKAN_FUNCTION(vkFreeDescriptorSets) \
    MAAGE_VULKAN_FUNCTION(vkFreeMemory) \
    MAAGE_VULKAN_FUNCTION(vkGetBufferMemoryRequirements) \
    MAAGE_VULKAN_FUNCTION(vkGetDeviceProcAddr) \
    MAAGE_VULKAN_FUNCTION(vkGetDeviceQueue) \
    MAAGE_VULKAN_FUNCTION(vkGetFenceStatus) \
    MAAGE_VULKAN_FUNCTION(vkGetImageMemoryRequirements) \
    MAAGE_VULKAN_FUNCTION(vkGetInstanceProcAddr) \
    MAAGE_VULKAN_FUNCTION(vkGetPhysicalDeviceFeatures) \
    MAAGE_VULKAN_FUNCTION(vkGetPhysicalDeviceFormatProperties) \
    MAAGE_VULKAN_FUNCTION(vkGetPhysicalDeviceMemoryProperties) \
    MAAGE_VULKAN_FUNCTION(vkGetPhysicalDeviceProperties) \
    MAAGE_VULKAN_FUNCTION(vkGetPhysicalDeviceQueueFamilyProperties) \
    MAAGE_VULKAN_FUNCTION(vkGetPhysicalDeviceSurfaceCapabilitiesKHR) \
    MAAGE_VULKAN_FUNCTION(vkGetPhysicalDeviceSurfaceFormatsKHR) \
    MAAGE_VULKAN_FUNCTION(vkGetPhysicalDeviceSurfacePresentModesKHR) \
    MAAGE_VULKAN_FUNCTION(vkGetPhysicalDeviceSurfaceSupportKHR) \
    MAAGE_VULKAN_FUNCTION(vkGetSwapchainImagesKHR) \
    MAAGE_VULKAN_FUNCTION(vkMapMemory) \
    MAAGE_VULKAN_FUNCTION(vkQueuePresentKHR) \
    MAAGE_VULKAN_FUNCTION(vkQueueSubmit) \
    MAAGE_VULKAN_FUNCTION(vkQueueWaitIdle) \
    MAAGE_VULKAN_FUNCTION(vkResetCommandBuffer) \
    MAAGE_VULKAN_FUNCTION(vkResetFences) \
    MAAGE_VULKAN_FUNCTION(vkUnmapMemory) \
    MAAGE_VULKAN_FUNCTION(vkUpdateDescriptorSets) \
    MAAGE_VULKAN_FUNCTION(vkWaitForFences)

typedef struct maage_vulkan_dispatch {
#define MAAGE_VULKAN_DECLARE(name) PFN_##name name;
    MAAGE_VULKAN_FUNCTIONS(MAAGE_VULKAN_DECLARE)
#undef MAAGE_VULKAN_DECLARE
} maage_vulkan_dispatch;

static maage_status maage_vulkan_load_win32(maage_renderer *renderer,
    HMODULE *library, maage_vulkan_dispatch *dispatch)
{
    DWORD error;
    maage_status result;
    *library = LoadLibraryExA("vulkan-1.dll", NULL, LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (!*library) {
        error = GetLastError();
        result = error ? maage_status_from_win32(error, MAAGE_CONTEXT_RENDER, MAAGE_OPERATION_LOAD) :
            MAAGE_STATUS_VALUE(MAAGE_STATUS_SYSTEM_NOT_SUPPORTED, MAAGE_CONTEXT_RENDER, MAAGE_OPERATION_LOAD, 0);
        return maage_status_trace_record(renderer->operation_trace, result, MAAGE_CONTEXT_RENDER,
            MAAGE_OPERATION_LOAD, __FILE__, __LINE__, (long)error);
    }
#define MAAGE_VULKAN_LOAD(name) \
    dispatch->name = (PFN_##name)GetProcAddress(*library, #name); \
    if (!dispatch->name) { \
        error = GetLastError(); \
        result = error ? maage_status_from_win32(error, MAAGE_CONTEXT_RENDER, MAAGE_OPERATION_RESOLVE) : \
            MAAGE_STATUS_VALUE(MAAGE_STATUS_NOT_IMPLEMENTED, MAAGE_CONTEXT_RENDER, MAAGE_OPERATION_RESOLVE, 0); \
        return maage_status_trace_record(renderer->operation_trace, result, MAAGE_CONTEXT_RENDER, \
            MAAGE_OPERATION_RESOLVE, __FILE__, __LINE__, (long)error); \
    }
    MAAGE_VULKAN_FUNCTIONS(MAAGE_VULKAN_LOAD)
#undef MAAGE_VULKAN_LOAD
    return MAAGE_STATUS_VALUE(MAAGE_STATUS_SUCCESS, 0, MAAGE_OPERATION_LOAD, 0);
}
#undef MAAGE_VULKAN_FUNCTIONS
#define vkAcquireNextImageKHR native->dispatch.vkAcquireNextImageKHR
#define vkAllocateCommandBuffers native->dispatch.vkAllocateCommandBuffers
#define vkAllocateDescriptorSets native->dispatch.vkAllocateDescriptorSets
#define vkAllocateMemory native->dispatch.vkAllocateMemory
#define vkBeginCommandBuffer native->dispatch.vkBeginCommandBuffer
#define vkBindBufferMemory native->dispatch.vkBindBufferMemory
#define vkBindImageMemory native->dispatch.vkBindImageMemory
#define vkCmdBeginRenderPass native->dispatch.vkCmdBeginRenderPass
#define vkCmdBindDescriptorSets native->dispatch.vkCmdBindDescriptorSets
#define vkCmdBindIndexBuffer native->dispatch.vkCmdBindIndexBuffer
#define vkCmdBindPipeline native->dispatch.vkCmdBindPipeline
#define vkCmdBindVertexBuffers native->dispatch.vkCmdBindVertexBuffers
#define vkCmdClearAttachments native->dispatch.vkCmdClearAttachments
#define vkCmdClearColorImage native->dispatch.vkCmdClearColorImage
#define vkCmdCopyBuffer native->dispatch.vkCmdCopyBuffer
#define vkCmdCopyBufferToImage native->dispatch.vkCmdCopyBufferToImage
#define vkCmdCopyImage native->dispatch.vkCmdCopyImage
#define vkCmdDraw native->dispatch.vkCmdDraw
#define vkCmdDrawIndexed native->dispatch.vkCmdDrawIndexed
#define vkCmdEndRenderPass native->dispatch.vkCmdEndRenderPass
#define vkCmdPipelineBarrier native->dispatch.vkCmdPipelineBarrier
#define vkCmdPushConstants native->dispatch.vkCmdPushConstants
#define vkCmdSetScissor native->dispatch.vkCmdSetScissor
#define vkCmdSetViewport native->dispatch.vkCmdSetViewport
#define vkCreateBuffer native->dispatch.vkCreateBuffer
#define vkCreateCommandPool native->dispatch.vkCreateCommandPool
#define vkCreateDescriptorPool native->dispatch.vkCreateDescriptorPool
#define vkCreateDescriptorSetLayout native->dispatch.vkCreateDescriptorSetLayout
#define vkCreateDevice native->dispatch.vkCreateDevice
#define vkCreateFence native->dispatch.vkCreateFence
#define vkCreateFramebuffer native->dispatch.vkCreateFramebuffer
#define vkCreateGraphicsPipelines native->dispatch.vkCreateGraphicsPipelines
#define vkCreateImage native->dispatch.vkCreateImage
#define vkCreateImageView native->dispatch.vkCreateImageView
#define vkCreateInstance native->dispatch.vkCreateInstance
#define vkCreatePipelineCache native->dispatch.vkCreatePipelineCache
#define vkCreatePipelineLayout native->dispatch.vkCreatePipelineLayout
#define vkCreateRenderPass native->dispatch.vkCreateRenderPass
#define vkCreateSampler native->dispatch.vkCreateSampler
#define vkCreateSemaphore native->dispatch.vkCreateSemaphore
#define vkCreateShaderModule native->dispatch.vkCreateShaderModule
#define vkCreateSwapchainKHR native->dispatch.vkCreateSwapchainKHR
#define vkCreateWin32SurfaceKHR native->dispatch.vkCreateWin32SurfaceKHR
#define vkDestroyBuffer native->dispatch.vkDestroyBuffer
#define vkDestroyCommandPool native->dispatch.vkDestroyCommandPool
#define vkDestroyDescriptorPool native->dispatch.vkDestroyDescriptorPool
#define vkDestroyDescriptorSetLayout native->dispatch.vkDestroyDescriptorSetLayout
#define vkDestroyDevice native->dispatch.vkDestroyDevice
#define vkDestroyFence native->dispatch.vkDestroyFence
#define vkDestroyFramebuffer native->dispatch.vkDestroyFramebuffer
#define vkDestroyImage native->dispatch.vkDestroyImage
#define vkDestroyImageView native->dispatch.vkDestroyImageView
#define vkDestroyInstance native->dispatch.vkDestroyInstance
#define vkDestroyPipeline native->dispatch.vkDestroyPipeline
#define vkDestroyPipelineCache native->dispatch.vkDestroyPipelineCache
#define vkDestroyPipelineLayout native->dispatch.vkDestroyPipelineLayout
#define vkDestroyRenderPass native->dispatch.vkDestroyRenderPass
#define vkDestroySampler native->dispatch.vkDestroySampler
#define vkDestroySemaphore native->dispatch.vkDestroySemaphore
#define vkDestroyShaderModule native->dispatch.vkDestroyShaderModule
#define vkDestroySurfaceKHR native->dispatch.vkDestroySurfaceKHR
#define vkDestroySwapchainKHR native->dispatch.vkDestroySwapchainKHR
#define vkDeviceWaitIdle native->dispatch.vkDeviceWaitIdle
#define vkEndCommandBuffer native->dispatch.vkEndCommandBuffer
#define vkEnumerateDeviceExtensionProperties native->dispatch.vkEnumerateDeviceExtensionProperties
#define vkEnumerateInstanceExtensionProperties native->dispatch.vkEnumerateInstanceExtensionProperties
#define vkEnumeratePhysicalDevices native->dispatch.vkEnumeratePhysicalDevices
#define vkFlushMappedMemoryRanges native->dispatch.vkFlushMappedMemoryRanges
#define vkFreeCommandBuffers native->dispatch.vkFreeCommandBuffers
#define vkFreeDescriptorSets native->dispatch.vkFreeDescriptorSets
#define vkFreeMemory native->dispatch.vkFreeMemory
#define vkGetBufferMemoryRequirements native->dispatch.vkGetBufferMemoryRequirements
#define vkGetDeviceProcAddr native->dispatch.vkGetDeviceProcAddr
#define vkGetDeviceQueue native->dispatch.vkGetDeviceQueue
#define vkGetFenceStatus native->dispatch.vkGetFenceStatus
#define vkGetImageMemoryRequirements native->dispatch.vkGetImageMemoryRequirements
#define vkGetInstanceProcAddr native->dispatch.vkGetInstanceProcAddr
#define vkGetPhysicalDeviceFeatures native->dispatch.vkGetPhysicalDeviceFeatures
#define vkGetPhysicalDeviceFormatProperties native->dispatch.vkGetPhysicalDeviceFormatProperties
#define vkGetPhysicalDeviceMemoryProperties native->dispatch.vkGetPhysicalDeviceMemoryProperties
#define vkGetPhysicalDeviceProperties native->dispatch.vkGetPhysicalDeviceProperties
#define vkGetPhysicalDeviceQueueFamilyProperties native->dispatch.vkGetPhysicalDeviceQueueFamilyProperties
#define vkGetPhysicalDeviceSurfaceCapabilitiesKHR native->dispatch.vkGetPhysicalDeviceSurfaceCapabilitiesKHR
#define vkGetPhysicalDeviceSurfaceFormatsKHR native->dispatch.vkGetPhysicalDeviceSurfaceFormatsKHR
#define vkGetPhysicalDeviceSurfacePresentModesKHR native->dispatch.vkGetPhysicalDeviceSurfacePresentModesKHR
#define vkGetPhysicalDeviceSurfaceSupportKHR native->dispatch.vkGetPhysicalDeviceSurfaceSupportKHR
#define vkGetSwapchainImagesKHR native->dispatch.vkGetSwapchainImagesKHR
#define vkMapMemory native->dispatch.vkMapMemory
#define vkQueuePresentKHR native->dispatch.vkQueuePresentKHR
#define vkQueueSubmit native->dispatch.vkQueueSubmit
#define vkQueueWaitIdle native->dispatch.vkQueueWaitIdle
#define vkResetCommandBuffer native->dispatch.vkResetCommandBuffer
#define vkResetFences native->dispatch.vkResetFences
#define vkUnmapMemory native->dispatch.vkUnmapMemory
#define vkUpdateDescriptorSets native->dispatch.vkUpdateDescriptorSets
#define vkWaitForFences native->dispatch.vkWaitForFences
#endif
