#ifndef MAAGE_VULKAN_INTERNAL_H
#define MAAGE_VULKAN_INTERNAL_H
/* SDK and native WSI declarations never escape the Vulkan target. */
#if defined(MAAGE_RENDER_WIN32)
#define VK_USE_PLATFORM_WIN32_KHR
#define VK_NO_PROTOTYPES
#endif
#if defined(MAAGE_RENDER_X11)
#define VK_USE_PLATFORM_XLIB_KHR
#endif
#if defined(MAAGE_RENDER_WAYLAND)
#define VK_USE_PLATFORM_WAYLAND_KHR
#endif
#if defined(MAAGE_RENDER_COCOA)
#define VK_USE_PLATFORM_METAL_EXT
#define VK_ENABLE_BETA_EXTENSIONS
#endif
#include <vulkan/vulkan.h>
#include "../render_internal.h"
#if defined(MAAGE_RENDER_WIN32)
#include "loader_win32.h"
#endif

#define MAAGE_VULKAN_FRAME_SLOTS 2
typedef struct maage_vulkan_frame {
    VkCommandBuffer command;
    VkFence fence;
    VkSemaphore acquired;
    int in_flight, has_image;
    uint32_t image;
    maage_status_trace trace;
} maage_vulkan_frame;
typedef struct maage_vulkan_renderer {
#if defined(MAAGE_RENDER_WIN32)
    HMODULE library;
    maage_vulkan_dispatch dispatch;
#elif defined(MAAGE_RENDER_COCOA)
    void *cocoa;
#endif
    VkInstance instance;
    VkSurfaceKHR surface;
    VkPhysicalDevice physical;
    VkPhysicalDeviceProperties properties;
    VkPhysicalDeviceMemoryProperties memory;
    VkDevice device;
    uint32_t graphics_family, present_family;
    VkQueue graphics_queue, present_queue;
    VkCommandPool pool;
    maage_vulkan_frame frames[MAAGE_VULKAN_FRAME_SLOTS];
    uint32_t frame_index;
    VkSwapchainKHR swapchain;
    VkImage *images;
    VkImageView *views;
    VkSemaphore *finished;
    VkFence *present_fences;
    unsigned char *present_pending;
    maage_status_trace *present_traces;
    uint32_t image_count;
    VkFormat format, depth_format;
    VkColorSpaceKHR color_space;
    VkExtent2D extent;
    VkPresentModeKHR present_mode;
    unsigned long generation;
    int requested_width, requested_height;
    int vsync, surface_lost, maintenance, maintenance_instance, portability;
    maage_status failure;
    void *rendering;
} maage_vulkan_renderer;

maage_status maage_vulkan_record(maage_renderer *renderer, VkResult value, const char *file, unsigned long line);
#define maage_vulkan_result(value) \
    maage_vulkan_record(renderer, (value), __FILE__, (unsigned long) __LINE__)
#if defined(MAAGE_RENDER_COCOA)
maage_status maage_vulkan_cocoa_surface(maage_renderer *renderer);
maage_status maage_vulkan_cocoa_update(maage_renderer *renderer, const maage_window_state *state);
maage_status maage_vulkan_cocoa_destroy(maage_renderer *renderer);
#endif
maage_status maage_vulkan_platform_create(maage_renderer *renderer);
maage_status maage_vulkan_platform_destroy(maage_renderer *renderer);
/* Polls a submitted frame and forwards its owned submission trace on failure. */
maage_status maage_vulkan_frame_poll(maage_renderer *renderer, maage_vulkan_frame *frame);
/* Mode 0 draws visible windows; 1 only polls/rebuilds; 2 primes hidden windows.
 * A successful acquire must end in submit or abort. */
maage_status maage_vulkan_begin(maage_renderer *renderer, const maage_window_state *state, int preparing, maage_vulkan_frame **frame, uint32_t *image);
maage_status maage_vulkan_submit(maage_renderer *renderer, maage_vulkan_frame *frame, uint32_t image);
maage_status maage_vulkan_abort(maage_renderer *renderer, maage_vulkan_frame *frame, uint32_t image);
/* Implemented by the resource backend; called after GPU idle and before views
 * are destroyed, also on failed/partial creation and native surface recovery. */
void maage_vulkan_targets_destroy(maage_renderer *renderer);
#endif
