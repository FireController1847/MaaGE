#ifndef MAAGE_VULKAN_LOADER_WIN32_H
#define MAAGE_VULKAN_LOADER_WIN32_H
/* Per-renderer dispatch avoids a load-time Vulkan DLL requirement, and global
 * loader state. Types and signatures come exclusively from the Vulkan SDK. */
typedef struct maage_vulkan_dispatch {
    PFN_vkAcquireNextImageKHR vkAcquireNextImageKHR;
    PFN_vkAllocateCommandBuffers vkAllocateCommandBuffers;
    PFN_vkBeginCommandBuffer vkBeginCommandBuffer;
    PFN_vkCmdClearColorImage vkCmdClearColorImage;
    PFN_vkCmdPipelineBarrier vkCmdPipelineBarrier;
    PFN_vkCreateCommandPool vkCreateCommandPool;
    PFN_vkCreateDevice vkCreateDevice;
    PFN_vkCreateFence vkCreateFence;
    PFN_vkCreateInstance vkCreateInstance;
    PFN_vkCreateSemaphore vkCreateSemaphore;
    PFN_vkCreateSwapchainKHR vkCreateSwapchainKHR;
    PFN_vkCreateWin32SurfaceKHR vkCreateWin32SurfaceKHR;
    PFN_vkDestroyCommandPool vkDestroyCommandPool;
    PFN_vkDestroyDevice vkDestroyDevice;
    PFN_vkDestroyFence vkDestroyFence;
    PFN_vkDestroyInstance vkDestroyInstance;
    PFN_vkDestroySemaphore vkDestroySemaphore;
    PFN_vkDestroySurfaceKHR vkDestroySurfaceKHR;
    PFN_vkDestroySwapchainKHR vkDestroySwapchainKHR;
    PFN_vkDeviceWaitIdle vkDeviceWaitIdle;
    PFN_vkEndCommandBuffer vkEndCommandBuffer;
    PFN_vkEnumerateDeviceExtensionProperties vkEnumerateDeviceExtensionProperties;
    PFN_vkEnumerateInstanceExtensionProperties vkEnumerateInstanceExtensionProperties;
    PFN_vkEnumeratePhysicalDevices vkEnumeratePhysicalDevices;
    PFN_vkGetDeviceQueue vkGetDeviceQueue;
    PFN_vkGetPhysicalDeviceFormatProperties vkGetPhysicalDeviceFormatProperties;
    PFN_vkGetPhysicalDeviceProperties vkGetPhysicalDeviceProperties;
    PFN_vkGetPhysicalDeviceQueueFamilyProperties vkGetPhysicalDeviceQueueFamilyProperties;
    PFN_vkGetPhysicalDeviceSurfaceCapabilitiesKHR vkGetPhysicalDeviceSurfaceCapabilitiesKHR;
    PFN_vkGetPhysicalDeviceSurfaceFormatsKHR vkGetPhysicalDeviceSurfaceFormatsKHR;
    PFN_vkGetPhysicalDeviceSurfacePresentModesKHR vkGetPhysicalDeviceSurfacePresentModesKHR;
    PFN_vkGetPhysicalDeviceSurfaceSupportKHR vkGetPhysicalDeviceSurfaceSupportKHR;
    PFN_vkGetSwapchainImagesKHR vkGetSwapchainImagesKHR;
    PFN_vkQueuePresentKHR vkQueuePresentKHR;
    PFN_vkQueueSubmit vkQueueSubmit;
    PFN_vkResetCommandBuffer vkResetCommandBuffer;
    PFN_vkResetFences vkResetFences;
    PFN_vkWaitForFences vkWaitForFences;
} maage_vulkan_dispatch;

static int maage_vulkan_load_win32(HMODULE *library, maage_vulkan_dispatch *dispatch)
{
    *library = LoadLibraryExA("vulkan-1.dll", NULL, LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (!*library) return 0;
#define MAAGE_VULKAN_LOAD(name) \
    dispatch->name = (PFN_##name)GetProcAddress(*library, #name); \
    if (!dispatch->name) return 0
    MAAGE_VULKAN_LOAD(vkAcquireNextImageKHR);
    MAAGE_VULKAN_LOAD(vkAllocateCommandBuffers);
    MAAGE_VULKAN_LOAD(vkBeginCommandBuffer);
    MAAGE_VULKAN_LOAD(vkCmdClearColorImage);
    MAAGE_VULKAN_LOAD(vkCmdPipelineBarrier);
    MAAGE_VULKAN_LOAD(vkCreateCommandPool);
    MAAGE_VULKAN_LOAD(vkCreateDevice);
    MAAGE_VULKAN_LOAD(vkCreateFence);
    MAAGE_VULKAN_LOAD(vkCreateInstance);
    MAAGE_VULKAN_LOAD(vkCreateSemaphore);
    MAAGE_VULKAN_LOAD(vkCreateSwapchainKHR);
    MAAGE_VULKAN_LOAD(vkCreateWin32SurfaceKHR);
    MAAGE_VULKAN_LOAD(vkDestroyCommandPool);
    MAAGE_VULKAN_LOAD(vkDestroyDevice);
    MAAGE_VULKAN_LOAD(vkDestroyFence);
    MAAGE_VULKAN_LOAD(vkDestroyInstance);
    MAAGE_VULKAN_LOAD(vkDestroySemaphore);
    MAAGE_VULKAN_LOAD(vkDestroySurfaceKHR);
    MAAGE_VULKAN_LOAD(vkDestroySwapchainKHR);
    MAAGE_VULKAN_LOAD(vkDeviceWaitIdle);
    MAAGE_VULKAN_LOAD(vkEndCommandBuffer);
    MAAGE_VULKAN_LOAD(vkEnumerateDeviceExtensionProperties);
    MAAGE_VULKAN_LOAD(vkEnumerateInstanceExtensionProperties);
    MAAGE_VULKAN_LOAD(vkEnumeratePhysicalDevices);
    MAAGE_VULKAN_LOAD(vkGetDeviceQueue);
    MAAGE_VULKAN_LOAD(vkGetPhysicalDeviceFormatProperties);
    MAAGE_VULKAN_LOAD(vkGetPhysicalDeviceProperties);
    MAAGE_VULKAN_LOAD(vkGetPhysicalDeviceQueueFamilyProperties);
    MAAGE_VULKAN_LOAD(vkGetPhysicalDeviceSurfaceCapabilitiesKHR);
    MAAGE_VULKAN_LOAD(vkGetPhysicalDeviceSurfaceFormatsKHR);
    MAAGE_VULKAN_LOAD(vkGetPhysicalDeviceSurfacePresentModesKHR);
    MAAGE_VULKAN_LOAD(vkGetPhysicalDeviceSurfaceSupportKHR);
    MAAGE_VULKAN_LOAD(vkGetSwapchainImagesKHR);
    MAAGE_VULKAN_LOAD(vkQueuePresentKHR);
    MAAGE_VULKAN_LOAD(vkQueueSubmit);
    MAAGE_VULKAN_LOAD(vkResetCommandBuffer);
    MAAGE_VULKAN_LOAD(vkResetFences);
    MAAGE_VULKAN_LOAD(vkWaitForFences);
#undef MAAGE_VULKAN_LOAD
    return 1;
}

#define vkAcquireNextImageKHR native->dispatch.vkAcquireNextImageKHR
#define vkAllocateCommandBuffers native->dispatch.vkAllocateCommandBuffers
#define vkBeginCommandBuffer native->dispatch.vkBeginCommandBuffer
#define vkCmdClearColorImage native->dispatch.vkCmdClearColorImage
#define vkCmdPipelineBarrier native->dispatch.vkCmdPipelineBarrier
#define vkCreateCommandPool native->dispatch.vkCreateCommandPool
#define vkCreateDevice native->dispatch.vkCreateDevice
#define vkCreateFence native->dispatch.vkCreateFence
#define vkCreateInstance native->dispatch.vkCreateInstance
#define vkCreateSemaphore native->dispatch.vkCreateSemaphore
#define vkCreateSwapchainKHR native->dispatch.vkCreateSwapchainKHR
#define vkCreateWin32SurfaceKHR native->dispatch.vkCreateWin32SurfaceKHR
#define vkDestroyCommandPool native->dispatch.vkDestroyCommandPool
#define vkDestroyDevice native->dispatch.vkDestroyDevice
#define vkDestroyFence native->dispatch.vkDestroyFence
#define vkDestroyInstance native->dispatch.vkDestroyInstance
#define vkDestroySemaphore native->dispatch.vkDestroySemaphore
#define vkDestroySurfaceKHR native->dispatch.vkDestroySurfaceKHR
#define vkDestroySwapchainKHR native->dispatch.vkDestroySwapchainKHR
#define vkDeviceWaitIdle native->dispatch.vkDeviceWaitIdle
#define vkEndCommandBuffer native->dispatch.vkEndCommandBuffer
#define vkEnumerateDeviceExtensionProperties native->dispatch.vkEnumerateDeviceExtensionProperties
#define vkEnumerateInstanceExtensionProperties native->dispatch.vkEnumerateInstanceExtensionProperties
#define vkEnumeratePhysicalDevices native->dispatch.vkEnumeratePhysicalDevices
#define vkGetDeviceQueue native->dispatch.vkGetDeviceQueue
#define vkGetPhysicalDeviceFormatProperties native->dispatch.vkGetPhysicalDeviceFormatProperties
#define vkGetPhysicalDeviceProperties native->dispatch.vkGetPhysicalDeviceProperties
#define vkGetPhysicalDeviceQueueFamilyProperties native->dispatch.vkGetPhysicalDeviceQueueFamilyProperties
#define vkGetPhysicalDeviceSurfaceCapabilitiesKHR native->dispatch.vkGetPhysicalDeviceSurfaceCapabilitiesKHR
#define vkGetPhysicalDeviceSurfaceFormatsKHR native->dispatch.vkGetPhysicalDeviceSurfaceFormatsKHR
#define vkGetPhysicalDeviceSurfacePresentModesKHR native->dispatch.vkGetPhysicalDeviceSurfacePresentModesKHR
#define vkGetPhysicalDeviceSurfaceSupportKHR native->dispatch.vkGetPhysicalDeviceSurfaceSupportKHR
#define vkGetSwapchainImagesKHR native->dispatch.vkGetSwapchainImagesKHR
#define vkQueuePresentKHR native->dispatch.vkQueuePresentKHR
#define vkQueueSubmit native->dispatch.vkQueueSubmit
#define vkResetCommandBuffer native->dispatch.vkResetCommandBuffer
#define vkResetFences native->dispatch.vkResetFences
#define vkWaitForFences native->dispatch.vkWaitForFences
#endif
