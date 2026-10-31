/* Native resources and encoding; shared preparation and native WSI are separate. */
#include "vulkan_internal.h"
#include "maage_vulkan_shaders.h"
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>

#define MAAGE_VULKAN_OK MAAGE_STATUS_VALUE(MAAGE_STATUS_SUCCESS, 0, MAAGE_OPERATION_PRESENT, 0)
#define MAAGE_VULKAN_ERROR(code) MAAGE_STATUS_VALUE(code, MAAGE_CONTEXT_RENDER, MAAGE_OPERATION_UPLOAD, 0)
#define MAAGE_VULKAN_TRY(call)                                              \
    do {                                                                    \
        maage_status maage_try_result = (call);                             \
        if (maage_status_failed(maage_try_result)) return maage_try_result; \
    } while (0)

typedef struct maage_vulkan_buffer {
    VkBuffer buffer;
    VkDeviceMemory memory;
    VkDeviceSize capacity;
    void *mapped;
    int coherent;
} maage_vulkan_buffer;
typedef struct maage_vulkan_image {
    VkImage image;
    VkDeviceMemory memory;
    VkImageView view;
    VkDescriptorSet descriptor;
    VkDescriptorPool descriptor_pool;
} maage_vulkan_image;
typedef struct maage_vulkan_descriptors {
    VkDescriptorPool pool;
    size_t live;
    struct maage_vulkan_descriptors *next;
} maage_vulkan_descriptors;
typedef struct maage_vulkan_mesh {
    maage_vulkan_buffer vertices, indices, sorted[MAAGE_VULKAN_FRAME_SLOTS];
    unsigned long sorted_revision[MAAGE_VULKAN_FRAME_SLOTS];
} maage_vulkan_mesh;
typedef struct maage_vulkan_garbage {
    maage_vulkan_buffer buffer;
    maage_vulkan_image image;
    uint64_t serial;
    struct maage_vulkan_garbage *next;
} maage_vulkan_garbage;
typedef struct maage_vulkan_frame_data {
    maage_vulkan_buffer vertices[3], indices[3];
    maage_vulkan_image scene, depth, panorama[2];
    VkFramebuffer scene_framebuffer, panorama_framebuffer[2];
    VkCommandBuffer upload;
    maage_vulkan_garbage *staging;
    uint64_t serial;
} maage_vulkan_frame_data;
typedef struct maage_vulkan_rendering {
    VkDescriptorSetLayout descriptors;
    VkPipelineLayout layout;
    VkSampler nearest, linear;
    VkRenderPass scene_pass, color_pass, present_pass;
    VkPipeline mesh[3], ui[3], panorama_ui[3], blur, scene_composite, composite;
    VkFramebuffer *present_framebuffers;
    VkCommandPool upload_pool;
    VkCommandBuffer upload;
    maage_vulkan_garbage *staging, *retired;
    maage_vulkan_descriptors *pools;
    maage_vulkan_frame_data frames[MAAGE_VULKAN_FRAME_SLOTS];
    uint64_t submitted, completed;
    unsigned long generation;
    VkFormat pipeline_format;
} maage_vulkan_rendering;

static void maage_vulkan_buffer_destroy(maage_vulkan_renderer *native, maage_vulkan_buffer *buffer) {
    if (buffer->mapped) vkUnmapMemory(native->device, buffer->memory);
    if (buffer->buffer) vkDestroyBuffer(native->device, buffer->buffer, NULL);
    if (buffer->memory) vkFreeMemory(native->device, buffer->memory, NULL);
    memset(buffer, 0, sizeof(*buffer));
}
static maage_status maage_vulkan_memory_type(maage_vulkan_renderer *native, uint32_t bits, VkMemoryPropertyFlags required, VkMemoryPropertyFlags preferred, uint32_t *type) {
    uint32_t fallback = UINT32_MAX;
    for (uint32_t index = 0; index < native->memory.memoryTypeCount; ++index) {
        VkMemoryPropertyFlags flags = native->memory.memoryTypes[index].propertyFlags;
        if (!(bits & (1U << index)) || (flags & required) != required) continue;
        if ((flags & preferred) == preferred) {
            *type = index;
            return MAAGE_VULKAN_OK;
        }
        fallback = index;
    }
    if (fallback == UINT32_MAX) return MAAGE_VULKAN_ERROR(MAAGE_STATUS_SYSTEM_NOT_SUPPORTED);
    *type = fallback;
    return MAAGE_VULKAN_OK;
}
static maage_status maage_vulkan_buffer_create(maage_renderer *renderer, VkDeviceSize bytes, VkBufferUsageFlags usage, int host, maage_vulkan_buffer *buffer) {
    maage_vulkan_renderer *native = renderer->platform;
    VkBufferCreateInfo info = {0};
    VkMemoryAllocateInfo allocation = {0};
    VkMemoryRequirements requirements;
    maage_status result;
    info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    info.size = bytes;
    info.usage = usage;
    info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    result = maage_vulkan_result(vkCreateBuffer(native->device, &info, NULL, &buffer->buffer));
    if (maage_status_failed(result)) return result;
    vkGetBufferMemoryRequirements(native->device, buffer->buffer, &requirements);
    allocation.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocation.allocationSize = requirements.size;
    result = maage_vulkan_memory_type(native, requirements.memoryTypeBits,
                                      host ? VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT : VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                                      host ? VK_MEMORY_PROPERTY_HOST_COHERENT_BIT : 0, &allocation.memoryTypeIndex);
    if (maage_status_failed(result)) goto failed;
    result = maage_vulkan_result(vkAllocateMemory(native->device, &allocation, NULL, &buffer->memory));
    if (maage_status_failed(result)) goto failed;
    result = maage_vulkan_result(vkBindBufferMemory(native->device, buffer->buffer, buffer->memory, 0));
    if (maage_status_failed(result)) goto failed;
    buffer->capacity = bytes;
    buffer->coherent = (native->memory.memoryTypes[allocation.memoryTypeIndex].propertyFlags & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT) != 0;
    if (host) {
        result = maage_vulkan_result(vkMapMemory(native->device, buffer->memory, 0, VK_WHOLE_SIZE, 0, &buffer->mapped));
        if (maage_status_failed(result)) goto failed;
    }
    return MAAGE_VULKAN_OK;
failed:
    maage_vulkan_buffer_destroy(native, buffer);
    return result;
}
static maage_status maage_vulkan_flush(maage_renderer *renderer, maage_vulkan_buffer *buffer) {
    maage_vulkan_renderer *native = renderer->platform;
    VkMappedMemoryRange range = {0};
    if (buffer->coherent) return MAAGE_VULKAN_OK;
    range.sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE;
    range.memory = buffer->memory;
    range.size = VK_WHOLE_SIZE;
    return maage_vulkan_result(vkFlushMappedMemoryRanges(native->device, 1, &range));
}
static maage_status maage_vulkan_host_buffer(maage_renderer *renderer, maage_vulkan_buffer *buffer, size_t bytes, VkBufferUsageFlags usage) {
    maage_vulkan_renderer *native = renderer->platform;
    maage_vulkan_buffer replacement = {0};
    size_t capacity;
    if (buffer->capacity >= bytes) return MAAGE_VULKAN_OK;
    capacity = maage_render_transient_capacity((size_t) buffer->capacity, bytes);
    MAAGE_VULKAN_TRY(maage_vulkan_buffer_create(renderer, capacity, usage, 1, &replacement));
    maage_vulkan_buffer_destroy(native, buffer);
    *buffer = replacement;
    return MAAGE_VULKAN_OK;
}
static void maage_vulkan_image_destroy(maage_vulkan_renderer *native, maage_vulkan_image *image) {
    maage_vulkan_rendering *gpu = native->rendering;
    if (image->descriptor) {
        vkFreeDescriptorSets(native->device, image->descriptor_pool, 1, &image->descriptor);
        for (maage_vulkan_descriptors *pool = gpu->pools; pool; pool = pool->next)
            if (pool->pool == image->descriptor_pool) {
                --pool->live;
                break;
            }
    }
    if (image->view) vkDestroyImageView(native->device, image->view, NULL);
    if (image->image) vkDestroyImage(native->device, image->image, NULL);
    if (image->memory) vkFreeMemory(native->device, image->memory, NULL);
    memset(image, 0, sizeof(*image));
}
static maage_status maage_vulkan_image_create(maage_renderer *renderer, uint32_t width, uint32_t height, VkFormat format, VkImageUsageFlags usage, VkImageAspectFlags aspect, maage_vulkan_image *image) {
    maage_vulkan_renderer *native = renderer->platform;
    VkImageCreateInfo info = {0};
    VkImageViewCreateInfo view = {0};
    VkMemoryAllocateInfo allocation = {0};
    VkMemoryRequirements requirements;
    maage_status result;
    info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    info.imageType = VK_IMAGE_TYPE_2D;
    info.format = format;
    info.extent.width = width;
    info.extent.height = height;
    info.extent.depth = 1;
    info.mipLevels = 1;
    info.arrayLayers = 1;
    info.samples = VK_SAMPLE_COUNT_1_BIT;
    info.tiling = VK_IMAGE_TILING_OPTIMAL;
    info.usage = usage;
    info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    result = maage_vulkan_result(vkCreateImage(native->device, &info, NULL, &image->image));
    if (maage_status_failed(result)) return result;
    vkGetImageMemoryRequirements(native->device, image->image, &requirements);
    allocation.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocation.allocationSize = requirements.size;
    result = maage_vulkan_memory_type(native, requirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, 0, &allocation.memoryTypeIndex);
    if (maage_status_failed(result)) goto failed;
    result = maage_vulkan_result(vkAllocateMemory(native->device, &allocation, NULL, &image->memory));
    if (maage_status_failed(result)) goto failed;
    result = maage_vulkan_result(vkBindImageMemory(native->device, image->image, image->memory, 0));
    if (maage_status_failed(result)) goto failed;
    view.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    view.image = image->image;
    view.viewType = VK_IMAGE_VIEW_TYPE_2D;
    view.format = format;
    view.subresourceRange.aspectMask = aspect;
    view.subresourceRange.levelCount = view.subresourceRange.layerCount = 1;
    result = maage_vulkan_result(vkCreateImageView(native->device, &view, NULL, &image->view));
    if (maage_status_failed(result)) goto failed;
    return MAAGE_VULKAN_OK;
failed:
    maage_vulkan_image_destroy(native, image);
    return result;
}
static maage_status maage_vulkan_descriptor(maage_renderer *renderer, maage_vulkan_image *image, VkSampler sampler) {
    maage_vulkan_renderer *native = renderer->platform;
    maage_vulkan_rendering *gpu = native->rendering;
    VkDescriptorSetAllocateInfo allocate = {0};
    VkDescriptorPoolCreateInfo create = {0};
    VkDescriptorPoolSize size = {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 64};
    VkDescriptorImageInfo contents = {0};
    VkWriteDescriptorSet write = {0};
    maage_vulkan_descriptors *pool;
    VkResult value;
    allocate.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocate.descriptorSetCount = 1;
    allocate.pSetLayouts = &gpu->descriptors;
    for (pool = gpu->pools; pool; pool = pool->next) {
        if (pool->live == size.descriptorCount) continue;
        allocate.descriptorPool = pool->pool;
        value = vkAllocateDescriptorSets(native->device, &allocate, &image->descriptor);
        if (value == VK_SUCCESS) goto allocated;
        if (value != VK_ERROR_FRAGMENTED_POOL && value != VK_ERROR_OUT_OF_POOL_MEMORY) return maage_vulkan_result(value);
    }
    pool = calloc(1, sizeof(*pool));
    if (!pool) return MAAGE_VULKAN_ERROR(MAAGE_STATUS_ALLOCATION_FAILED);
    create.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    create.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
    create.maxSets = size.descriptorCount;
    create.poolSizeCount = 1;
    create.pPoolSizes = &size;
    value = vkCreateDescriptorPool(native->device, &create, NULL, &pool->pool);
    if (value != VK_SUCCESS) {
        free(pool);
        return maage_vulkan_result(value);
    }
    pool->next = gpu->pools;
    gpu->pools = pool;
    allocate.descriptorPool = pool->pool;
    value = vkAllocateDescriptorSets(native->device, &allocate, &image->descriptor);
    if (value != VK_SUCCESS) return maage_vulkan_result(value);
allocated:
    ++pool->live;
    image->descriptor_pool = pool->pool;
    contents.sampler = sampler;
    contents.imageView = image->view;
    contents.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    write.dstSet = image->descriptor;
    write.descriptorCount = 1;
    write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    write.pImageInfo = &contents;
    vkUpdateDescriptorSets(native->device, 1, &write, 0, NULL);
    return MAAGE_VULKAN_OK;
}
static maage_status maage_vulkan_upload_begin(maage_renderer *renderer) {
    maage_vulkan_renderer *native = renderer->platform;
    maage_vulkan_rendering *gpu = native->rendering;
    VkCommandBufferAllocateInfo allocate = {0};
    VkCommandBufferBeginInfo begin = {0};
    maage_status result;
    if (gpu->upload) return MAAGE_VULKAN_OK;
    allocate.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocate.commandPool = gpu->upload_pool;
    allocate.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocate.commandBufferCount = 1;
    MAAGE_VULKAN_TRY(maage_vulkan_result(vkAllocateCommandBuffers(native->device, &allocate, &gpu->upload)));
    begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    result = maage_vulkan_result(vkBeginCommandBuffer(gpu->upload, &begin));
    if (maage_status_failed(result)) {
        vkFreeCommandBuffers(native->device, gpu->upload_pool, 1, &gpu->upload);
        gpu->upload = VK_NULL_HANDLE;
    }
    return result;
}
static maage_status maage_vulkan_staging(maage_renderer *renderer, size_t bytes, maage_vulkan_garbage **output) {
    maage_vulkan_renderer *native = renderer->platform;
    maage_vulkan_rendering *gpu = native->rendering;
    maage_vulkan_garbage *staging = calloc(1, sizeof(*staging));
    maage_status result;
    if (!staging) return MAAGE_VULKAN_ERROR(MAAGE_STATUS_ALLOCATION_FAILED);
    result = maage_vulkan_buffer_create(renderer, bytes, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, 1, &staging->buffer);
    if (maage_status_failed(result)) {
        free(staging);
        return result;
    }
    staging->next = gpu->staging;
    gpu->staging = staging;
    *output = staging;
    return MAAGE_VULKAN_OK;
}
static void maage_vulkan_garbage_destroy(maage_vulkan_renderer *native, maage_vulkan_garbage *garbage) {
    while (garbage) {
        maage_vulkan_garbage *next = garbage->next;
        maage_vulkan_buffer_destroy(native, &garbage->buffer);
        maage_vulkan_image_destroy(native, &garbage->image);
        free(garbage);
        garbage = next;
    }
}
static maage_status maage_vulkan_collect(maage_renderer *renderer) {
    maage_vulkan_renderer *native = renderer->platform;
    maage_vulkan_rendering *gpu = native->rendering;
    for (unsigned int slot = 0; slot < MAAGE_VULKAN_FRAME_SLOTS; ++slot) {
        maage_vulkan_frame_data *frame = &gpu->frames[slot];
        maage_status result = maage_vulkan_frame_poll(renderer, &native->frames[slot]);
        if (maage_status_failed(result)) return result;
        if (maage_status_code(result) != MAAGE_STATUS_SUCCESS || !frame->serial) continue;
        if (gpu->completed < frame->serial) gpu->completed = frame->serial;
        maage_vulkan_garbage_destroy(native, frame->staging);
        frame->staging = NULL;
        if (frame->upload) vkFreeCommandBuffers(native->device, gpu->upload_pool, 1, &frame->upload);
        frame->upload = VK_NULL_HANDLE;
    }
    maage_vulkan_garbage **link = &gpu->retired;
    while (*link) {
        maage_vulkan_garbage *entry = *link;
        if (entry->serial <= gpu->completed) {
            *link = entry->next;
            entry->next = NULL;
            maage_vulkan_garbage_destroy(native, entry);
        } else
            link = &entry->next;
    }
    maage_vulkan_descriptors **pool = &gpu->pools;
    while (*pool) {
        if (!(*pool)->live) {
            maage_vulkan_descriptors *empty = *pool;
            *pool = empty->next;
            vkDestroyDescriptorPool(native->device, empty->pool, NULL);
            free(empty);
        } else
            pool = &(*pool)->next;
    }
    return MAAGE_VULKAN_OK;
}
static void maage_vulkan_retire(maage_renderer *renderer, maage_vulkan_buffer *buffer, maage_vulkan_image *image) {
    maage_vulkan_renderer *native = renderer->platform;
    maage_vulkan_rendering *gpu = native->rendering;
    maage_vulkan_garbage *entry = calloc(1, sizeof(*entry));
    if (!entry) {
        /* Only the exceptional allocation failure path drains the device. */
        VkResult result = vkDeviceWaitIdle(native->device);
        if (result != VK_SUCCESS) renderer->failure = maage_vulkan_result(result);
        if (gpu->upload) {
            VkSubmitInfo submit = {0};
            result = vkEndCommandBuffer(gpu->upload);
            if (result == VK_SUCCESS) {
                submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
                submit.commandBufferCount = 1;
                submit.pCommandBuffers = &gpu->upload;
                result = vkQueueSubmit(native->graphics_queue, 1, &submit, VK_NULL_HANDLE);
                if (result == VK_SUCCESS) result = vkDeviceWaitIdle(native->device);
            }
            if (result != VK_SUCCESS) renderer->failure = maage_vulkan_result(result);
            vkFreeCommandBuffers(native->device, gpu->upload_pool, 1, &gpu->upload);
            gpu->upload = VK_NULL_HANDLE;
            maage_vulkan_garbage_destroy(native, gpu->staging);
            gpu->staging = NULL;
        }
        if (buffer) maage_vulkan_buffer_destroy(native, buffer);
        if (image) maage_vulkan_image_destroy(native, image);
        return;
    }
    entry->serial = gpu->submitted + (gpu->upload != VK_NULL_HANDLE);
    if (buffer) {
        entry->buffer = *buffer;
        memset(buffer, 0, sizeof(*buffer));
    }
    if (image) {
        entry->image = *image;
        memset(image, 0, sizeof(*image));
    }
    entry->next = gpu->retired;
    gpu->retired = entry;
}

static maage_status maage_vulkan_render_pass(maage_renderer *renderer, VkFormat format, int depth, int present, VkRenderPass *output) {
    maage_vulkan_renderer *native = renderer->platform;
    VkAttachmentDescription attachments[2] = {{0}};
    VkAttachmentReference color = {0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
    VkAttachmentReference z = {1, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL};
    VkSubpassDescription subpass = {0};
    VkSubpassDependency dependencies[2] = {{0}};
    VkRenderPassCreateInfo info = {0};
    attachments[0].format = format;
    attachments[0].samples = VK_SAMPLE_COUNT_1_BIT;
    attachments[0].loadOp = depth ? VK_ATTACHMENT_LOAD_OP_LOAD : VK_ATTACHMENT_LOAD_OP_CLEAR;
    attachments[0].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    attachments[0].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    attachments[0].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    attachments[0].initialLayout = depth ? VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL : VK_IMAGE_LAYOUT_UNDEFINED;
    attachments[0].finalLayout = present ? VK_IMAGE_LAYOUT_PRESENT_SRC_KHR : (depth ? VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL : VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    attachments[1].format = native->depth_format;
    attachments[1].samples = VK_SAMPLE_COUNT_1_BIT;
    attachments[1].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    attachments[1].storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    attachments[1].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    attachments[1].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    attachments[1].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    attachments[1].finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &color;
    if (depth) subpass.pDepthStencilAttachment = &z;
    dependencies[0].srcSubpass = VK_SUBPASS_EXTERNAL;
    dependencies[0].dstSubpass = 0;
    dependencies[0].srcStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
    dependencies[0].dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
    dependencies[0].srcAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    dependencies[0].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | (depth ? VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT : 0);
    dependencies[0].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;
    dependencies[1].srcSubpass = 0;
    dependencies[1].dstSubpass = VK_SUBPASS_EXTERNAL;
    dependencies[1].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
    dependencies[1].dstStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
    dependencies[1].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | (depth ? VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT : 0);
    dependencies[1].dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | (depth ? VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT : 0);
    dependencies[1].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;
    info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    info.attachmentCount = depth ? 2 : 1;
    info.pAttachments = attachments;
    info.subpassCount = 1;
    info.pSubpasses = &subpass;
    info.dependencyCount = 2;
    info.pDependencies = dependencies;
    return maage_vulkan_result(vkCreateRenderPass(native->device, &info, NULL, output));
}
static maage_status maage_vulkan_pipeline(maage_renderer *renderer, VkRenderPass pass, int mode, int output_srgb, VkPipeline *output) {
    maage_vulkan_renderer *native = renderer->platform;
    maage_vulkan_rendering *gpu = native->rendering;
    VkShaderModule modules[2] = {0};
    VkShaderModuleCreateInfo module = {0};
    VkPipelineShaderStageCreateInfo stages[2] = {{0}};
    VkVertexInputBindingDescription binding = {0};
    VkVertexInputAttributeDescription attributes[7] = {{0}};
    VkPipelineVertexInputStateCreateInfo vertices = {0};
    VkPipelineInputAssemblyStateCreateInfo assembly = {0};
    VkPipelineViewportStateCreateInfo viewport = {0};
    VkPipelineRasterizationStateCreateInfo raster = {0};
    VkPipelineMultisampleStateCreateInfo samples = {0};
    VkPipelineDepthStencilStateCreateInfo depth = {0};
    VkPipelineColorBlendAttachmentState blend = {0};
    VkPipelineColorBlendStateCreateInfo blending = {0};
    VkPipelineDynamicStateCreateInfo dynamic = {0};
    VkDynamicState dynamics[2] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkGraphicsPipelineCreateInfo info = {0};
    VkSpecializationMapEntry entry = {0, 0, sizeof(uint32_t)};
    uint32_t srgb = output_srgb != 0;
    VkSpecializationInfo specialization = {1, &entry, sizeof(srgb), &srgb};
    static const VkBlendFactor factors[] = {
        VK_BLEND_FACTOR_ONE, VK_BLEND_FACTOR_SRC_ALPHA, VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
        VK_BLEND_FACTOR_SRC_COLOR, VK_BLEND_FACTOR_DST_COLOR,
        VK_BLEND_FACTOR_ONE_MINUS_SRC_COLOR, VK_BLEND_FACTOR_ONE_MINUS_DST_COLOR};
    const maage_render_pipeline_state *state = &maage_render_pipeline_states[mode];
    int ui = state->ui, fullscreen = state->fullscreen;
    maage_status result;
    module.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    for (unsigned int index = 0; index < 2; ++index) {
        if (fullscreen) {
            module.pCode = index ? maage_vulkan_blur_frag : maage_vulkan_fullscreen_vert;
            module.codeSize = index ? sizeof(maage_vulkan_blur_frag) : sizeof(maage_vulkan_fullscreen_vert);
        } else if (ui) {
            module.pCode = index ? maage_vulkan_ui_frag : maage_vulkan_ui_vert;
            module.codeSize = index ? sizeof(maage_vulkan_ui_frag) : sizeof(maage_vulkan_ui_vert);
        } else {
            module.pCode = index ? maage_vulkan_mesh_frag : maage_vulkan_mesh_vert;
            module.codeSize = index ? sizeof(maage_vulkan_mesh_frag) : sizeof(maage_vulkan_mesh_vert);
        }
        result = maage_vulkan_result(vkCreateShaderModule(native->device, &module, NULL, &modules[index]));
        if (maage_status_failed(result)) goto done;
        stages[index].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        stages[index].stage = index ? VK_SHADER_STAGE_FRAGMENT_BIT : VK_SHADER_STAGE_VERTEX_BIT;
        stages[index].module = modules[index];
        stages[index].pName = "main";
    }
    if (fullscreen) stages[1].pSpecializationInfo = &specialization;
    binding.stride = ui ? sizeof(maage_render_ui_gpu_vertex) : sizeof(maage_mesh_vertex);
    binding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
    {
#define MAAGE_VERTEX_VEC1 VK_FORMAT_R32_SFLOAT
#define MAAGE_VERTEX_VEC2 VK_FORMAT_R32G32_SFLOAT
#define MAAGE_VERTEX_VEC3 VK_FORMAT_R32G32B32_SFLOAT
#define MAAGE_VERTEX_VEC4 VK_FORMAT_R32G32B32A32_SFLOAT
#define MAAGE_VERTEX_FIELD(slot, format, name) {slot, 0, format, offsetof(maage_mesh_vertex, name)},
        static const VkVertexInputAttributeDescription mesh_fields[] = {
            MAAGE_RENDER_MESH_ATTRIBUTES(MAAGE_VERTEX_FIELD)};
#undef MAAGE_VERTEX_FIELD
#define MAAGE_VERTEX_FIELD(slot, format, name) {slot, 0, format, offsetof(maage_render_ui_gpu_vertex, vertex.name)},
        static const VkVertexInputAttributeDescription ui_fields[] = {
            MAAGE_RENDER_UI_ATTRIBUTES(MAAGE_VERTEX_FIELD){MAAGE_RENDER_UI_DEPTH_LOCATION, 0, VK_FORMAT_R32_SFLOAT, offsetof(maage_render_ui_gpu_vertex, depth)}};
#undef MAAGE_VERTEX_FIELD
#undef MAAGE_VERTEX_VEC1
#undef MAAGE_VERTEX_VEC2
#undef MAAGE_VERTEX_VEC3
#undef MAAGE_VERTEX_VEC4
        memcpy(attributes, ui ? ui_fields : mesh_fields, ui ? sizeof(ui_fields) : sizeof(mesh_fields));
    }
    vertices.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertices.vertexBindingDescriptionCount = fullscreen ? 0 : 1;
    vertices.pVertexBindingDescriptions = &binding;
    vertices.vertexAttributeDescriptionCount = fullscreen ? 0 : (ui ? 4 : 7);
    vertices.pVertexAttributeDescriptions = attributes;
    assembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    viewport.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewport.viewportCount = viewport.scissorCount = 1;
    raster.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    raster.polygonMode = VK_POLYGON_MODE_FILL;
    raster.lineWidth = 1.0f;
    raster.cullMode = state->cull ? VK_CULL_MODE_BACK_BIT : VK_CULL_MODE_NONE;
    /* Shader clip-Y inversion plus Vulkan's framebuffer convention preserves
     * the neutral CCW front-face contract. */
    raster.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    samples.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    samples.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
    depth.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depth.depthTestEnable = state->depth_test;
    depth.depthWriteEnable = state->depth_write;
    depth.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;
    blend.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    blend.blendEnable = state->blend;
    blend.srcColorBlendFactor = factors[state->source_rgb];
    blend.dstColorBlendFactor = factors[state->destination_rgb];
    blend.colorBlendOp = blend.alphaBlendOp = VK_BLEND_OP_ADD;
    blend.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    blend.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    blending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    blending.attachmentCount = 1;
    blending.pAttachments = &blend;
    dynamic.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamic.dynamicStateCount = 2;
    dynamic.pDynamicStates = dynamics;
    info.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    info.stageCount = 2;
    info.pStages = stages;
    info.pVertexInputState = &vertices;
    info.pInputAssemblyState = &assembly;
    info.pViewportState = &viewport;
    info.pRasterizationState = &raster;
    info.pMultisampleState = &samples;
    info.pDepthStencilState = &depth;
    info.pColorBlendState = &blending;
    info.pDynamicState = &dynamic;
    info.layout = gpu->layout;
    info.renderPass = pass;
    result = maage_vulkan_result(vkCreateGraphicsPipelines(native->device, VK_NULL_HANDLE, 1, &info, NULL, output));
done:
    for (unsigned int index = 0; index < 2; ++index)
        if (modules[index]) vkDestroyShaderModule(native->device, modules[index], NULL);
    return result;
}
void maage_vulkan_targets_destroy(maage_renderer *renderer) {
    maage_vulkan_renderer *native = renderer->platform;
    maage_vulkan_rendering *gpu = native ? native->rendering : NULL;
    if (!gpu) return;
    if (gpu->present_framebuffers)
        for (uint32_t index = 0; index < native->image_count; ++index)
            if (gpu->present_framebuffers[index]) vkDestroyFramebuffer(native->device, gpu->present_framebuffers[index], NULL);
    free(gpu->present_framebuffers);
    gpu->present_framebuffers = NULL;
    for (unsigned int slot = 0; slot < MAAGE_VULKAN_FRAME_SLOTS; ++slot) {
        maage_vulkan_frame_data *frame = &gpu->frames[slot];
        if (frame->scene_framebuffer) vkDestroyFramebuffer(native->device, frame->scene_framebuffer, NULL);
        frame->scene_framebuffer = VK_NULL_HANDLE;
        maage_vulkan_image_destroy(native, &frame->scene);
        maage_vulkan_image_destroy(native, &frame->depth);
        for (unsigned int index = 0; index < 2; ++index) {
            if (frame->panorama_framebuffer[index]) vkDestroyFramebuffer(native->device, frame->panorama_framebuffer[index], NULL);
            frame->panorama_framebuffer[index] = VK_NULL_HANDLE;
            maage_vulkan_image_destroy(native, &frame->panorama[index]);
        }
    }
    gpu->generation = 0;
}
static void maage_vulkan_pipelines_destroy(maage_vulkan_renderer *native) {
    maage_vulkan_rendering *gpu = native->rendering;
    for (unsigned int index = 0; index < 3; ++index) {
        if (gpu->mesh[index]) vkDestroyPipeline(native->device, gpu->mesh[index], NULL);
        gpu->mesh[index] = VK_NULL_HANDLE;
    }
    for (unsigned int index = 0; index < 3; ++index) {
        if (gpu->ui[index]) vkDestroyPipeline(native->device, gpu->ui[index], NULL);
        if (gpu->panorama_ui[index]) vkDestroyPipeline(native->device, gpu->panorama_ui[index], NULL);
        gpu->ui[index] = gpu->panorama_ui[index] = VK_NULL_HANDLE;
    }
    if (gpu->blur) vkDestroyPipeline(native->device, gpu->blur, NULL);
    if (gpu->scene_composite) vkDestroyPipeline(native->device, gpu->scene_composite, NULL);
    if (gpu->composite) vkDestroyPipeline(native->device, gpu->composite, NULL);
    gpu->blur = gpu->scene_composite = gpu->composite = VK_NULL_HANDLE;
    if (gpu->scene_pass) vkDestroyRenderPass(native->device, gpu->scene_pass, NULL);
    if (gpu->color_pass) vkDestroyRenderPass(native->device, gpu->color_pass, NULL);
    if (gpu->present_pass) vkDestroyRenderPass(native->device, gpu->present_pass, NULL);
    gpu->scene_pass = gpu->color_pass = gpu->present_pass = VK_NULL_HANDLE;
    gpu->pipeline_format = VK_FORMAT_UNDEFINED;
}
static maage_status maage_vulkan_targets_create(maage_renderer *renderer) {
    maage_vulkan_renderer *native = renderer->platform;
    maage_vulkan_rendering *gpu = native->rendering;
    VkFramebufferCreateInfo framebuffer = {0};
    int srgb = native->format == VK_FORMAT_B8G8R8A8_SRGB || native->format == VK_FORMAT_R8G8B8A8_SRGB;
    if (gpu->generation == native->generation) return MAAGE_VULKAN_OK;
    if (gpu->pipeline_format != native->format) {
        maage_vulkan_pipelines_destroy(native);
        MAAGE_VULKAN_TRY(maage_vulkan_render_pass(renderer, VK_FORMAT_R8G8B8A8_UNORM, 1, 0, &gpu->scene_pass));
        MAAGE_VULKAN_TRY(maage_vulkan_render_pass(renderer, VK_FORMAT_R8G8B8A8_UNORM, 0, 0, &gpu->color_pass));
        MAAGE_VULKAN_TRY(maage_vulkan_render_pass(renderer, native->format, 0, 1, &gpu->present_pass));
        for (unsigned int index = 0; index < 3; ++index)
            MAAGE_VULKAN_TRY(maage_vulkan_pipeline(renderer, gpu->scene_pass, index, 0, &gpu->mesh[index]));
        for (unsigned int index = 0; index < 3; ++index) {
            MAAGE_VULKAN_TRY(maage_vulkan_pipeline(renderer, gpu->scene_pass, index + MAAGE_RENDER_UI, 0, &gpu->ui[index]));
            MAAGE_VULKAN_TRY(maage_vulkan_pipeline(renderer, gpu->color_pass, index + MAAGE_RENDER_UI, 0, &gpu->panorama_ui[index]));
        }
        MAAGE_VULKAN_TRY(maage_vulkan_pipeline(renderer, gpu->color_pass, MAAGE_RENDER_FULLSCREEN, 0, &gpu->blur));
        MAAGE_VULKAN_TRY(maage_vulkan_pipeline(renderer, gpu->scene_pass, MAAGE_RENDER_FULLSCREEN, 0, &gpu->scene_composite));
        MAAGE_VULKAN_TRY(maage_vulkan_pipeline(renderer, gpu->present_pass, MAAGE_RENDER_FULLSCREEN, srgb, &gpu->composite));
        gpu->pipeline_format = native->format;
    }
    gpu->present_framebuffers = calloc(native->image_count, sizeof(*gpu->present_framebuffers));
    if (!gpu->present_framebuffers) return MAAGE_VULKAN_ERROR(MAAGE_STATUS_ALLOCATION_FAILED);
    framebuffer.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
    framebuffer.renderPass = gpu->present_pass;
    framebuffer.attachmentCount = 1;
    framebuffer.width = native->extent.width;
    framebuffer.height = native->extent.height;
    framebuffer.layers = 1;
    for (uint32_t index = 0; index < native->image_count; ++index) {
        framebuffer.pAttachments = &native->views[index];
        MAAGE_VULKAN_TRY(maage_vulkan_result(vkCreateFramebuffer(native->device, &framebuffer, NULL, &gpu->present_framebuffers[index])));
    }
    gpu->generation = native->generation;
    return MAAGE_VULKAN_OK;
}
static maage_status maage_vulkan_frame_targets(maage_renderer *renderer, maage_vulkan_frame_data *frame, int panorama) {
    maage_vulkan_renderer *native = renderer->platform;
    maage_vulkan_rendering *gpu = native->rendering;
    VkFramebufferCreateInfo framebuffer = {0};
    VkImageView attachments[2];
    framebuffer.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
    framebuffer.width = native->extent.width;
    framebuffer.height = native->extent.height;
    framebuffer.layers = 1;
    if (!frame->scene.image) {
        MAAGE_VULKAN_TRY(maage_vulkan_image_create(renderer, native->extent.width, native->extent.height, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, VK_IMAGE_ASPECT_COLOR_BIT, &frame->scene));
        MAAGE_VULKAN_TRY(maage_vulkan_descriptor(renderer, &frame->scene, gpu->nearest));
        MAAGE_VULKAN_TRY(maage_vulkan_image_create(renderer, native->extent.width, native->extent.height, native->depth_format, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT, VK_IMAGE_ASPECT_DEPTH_BIT, &frame->depth));
        attachments[0] = frame->scene.view;
        attachments[1] = frame->depth.view;
        framebuffer.renderPass = gpu->scene_pass;
        framebuffer.attachmentCount = 2;
        framebuffer.pAttachments = attachments;
        MAAGE_VULKAN_TRY(maage_vulkan_result(vkCreateFramebuffer(native->device, &framebuffer, NULL, &frame->scene_framebuffer)));
    }
    if (panorama && !frame->panorama[0].image) {
        for (unsigned int index = 0; index < 2; ++index) {
            MAAGE_VULKAN_TRY(maage_vulkan_image_create(renderer, native->extent.width, native->extent.height, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, VK_IMAGE_ASPECT_COLOR_BIT, &frame->panorama[index]));
            MAAGE_VULKAN_TRY(maage_vulkan_descriptor(renderer, &frame->panorama[index], gpu->linear));
            framebuffer.renderPass = gpu->color_pass;
            framebuffer.attachmentCount = 1;
            framebuffer.pAttachments = &frame->panorama[index].view;
            MAAGE_VULKAN_TRY(maage_vulkan_result(vkCreateFramebuffer(native->device, &framebuffer, NULL, &frame->panorama_framebuffer[index])));
        }
    }
    return MAAGE_VULKAN_OK;
}

static void maage_vulkan_transition(maage_vulkan_renderer *native, VkCommandBuffer command, VkImage image, VkImageLayout old_layout, VkImageLayout new_layout, VkPipelineStageFlags source_stage, VkPipelineStageFlags destination_stage, VkAccessFlags source_access, VkAccessFlags destination_access) {
    VkImageMemoryBarrier barrier = {0};
    (void) native;
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.oldLayout = old_layout;
    barrier.newLayout = new_layout;
    barrier.srcAccessMask = source_access;
    barrier.dstAccessMask = destination_access;
    barrier.srcQueueFamilyIndex = barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = image;
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.levelCount = barrier.subresourceRange.layerCount = 1;
    vkCmdPipelineBarrier(command, source_stage, destination_stage, 0, 0, NULL, 0, NULL, 1, &barrier);
}
static void maage_vulkan_pass_begin(maage_renderer *renderer, VkCommandBuffer command, VkRenderPass pass, VkFramebuffer framebuffer) {
    maage_vulkan_renderer *native = renderer->platform;
    VkClearValue clear[2] = {{{0}}};
    VkRenderPassBeginInfo begin = {0};
    VkViewport viewport = {0, 0, (float) native->extent.width, (float) native->extent.height, 0, 1};
    VkRect2D scissor = {{0, 0}, native->extent};
    memcpy(clear[0].color.float32, renderer->clear, sizeof(renderer->clear));
    clear[1].depthStencil.depth = 1.0f;
    begin.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    begin.renderPass = pass;
    begin.framebuffer = framebuffer;
    begin.renderArea = scissor;
    begin.clearValueCount = pass == ((maage_vulkan_rendering *) native->rendering)->scene_pass ? 2 : 1;
    begin.pClearValues = clear;
    vkCmdBeginRenderPass(command, &begin, VK_SUBPASS_CONTENTS_INLINE);
    vkCmdSetViewport(command, 0, 1, &viewport);
    vkCmdSetScissor(command, 0, 1, &scissor);
}
static void maage_vulkan_bind(maage_vulkan_renderer *native, VkCommandBuffer command, VkPipeline pipeline, VkDescriptorSet descriptor, const maage_render_uniforms *push) {
    maage_vulkan_rendering *gpu = native->rendering;
    vkCmdBindPipeline(command, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
    vkCmdBindDescriptorSets(command, VK_PIPELINE_BIND_POINT_GRAPHICS, gpu->layout, 0, 1, &descriptor, 0, NULL);
    vkCmdPushConstants(command, gpu->layout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(*push), push);
}
static void maage_vulkan_indices(maage_vulkan_renderer *native, VkCommandBuffer command, VkBuffer indices, size_t first, size_t count) {
    (void) native;
    while (count) {
        uint32_t amount = count > UINT32_MAX ? UINT32_MAX : (uint32_t) count;
        amount -= amount % 3;
        vkCmdBindIndexBuffer(command, indices, (VkDeviceSize) first * sizeof(unsigned int), VK_INDEX_TYPE_UINT32);
        vkCmdDrawIndexed(command, amount, 1, 0, 0, 0);
        first += amount;
        count -= amount;
    }
}
static maage_status maage_vulkan_sort(maage_renderer *renderer, const maage_render_mesh *mesh, const float matrix[16], unsigned int slot) {
    maage_vulkan_mesh *geometry = mesh->platform;
    maage_status result = maage_render_geometry_sort_slot(mesh, matrix, geometry->sorted_revision, MAAGE_VULKAN_FRAME_SLOTS, slot);
    if (maage_status_code(result) != MAAGE_STATUS_SUCCESS) return result;
    MAAGE_VULKAN_TRY(maage_vulkan_host_buffer(renderer, &geometry->sorted[slot], mesh->geometry.translucent_count * sizeof(unsigned int), VK_BUFFER_USAGE_INDEX_BUFFER_BIT));
    MAAGE_VULKAN_TRY(maage_render_geometry_write_sorted(&mesh->geometry, geometry->sorted[slot].mapped));
    MAAGE_VULKAN_TRY(maage_vulkan_flush(renderer, &geometry->sorted[slot]));
    geometry->sorted_revision[slot] = mesh->geometry.revision;
    return MAAGE_VULKAN_OK;
}
static void maage_vulkan_mesh_draw(maage_vulkan_renderer *native, VkCommandBuffer command, const maage_render_mesh *mesh, unsigned int layer, unsigned int slot, const maage_render_uniforms *push) {
    maage_vulkan_rendering *gpu = native->rendering;
    maage_vulkan_mesh *geometry = mesh->platform;
    maage_vulkan_image *texture = mesh->texture->platform;
    size_t first, count = maage_render_geometry_range(&mesh->geometry, layer, &first);
    VkDeviceSize offset = 0;
    if (!count) return;
    maage_vulkan_bind(native, command, gpu->mesh[layer], texture->descriptor, push);
    vkCmdBindVertexBuffers(command, 0, 1, &geometry->vertices.buffer, &offset);
    maage_vulkan_indices(native, command, layer == MAAGE_RENDER_TRANSLUCENT ? geometry->sorted[slot].buffer : geometry->indices.buffer, first, count);
}
static void maage_vulkan_mesh_passes(maage_vulkan_renderer *native, VkCommandBuffer command, const maage_render_frame_plan *plan, int foreground, unsigned int slot) {
    maage_render_mesh_cursor cursor = {0, 0};
    const maage_render_mesh *mesh;
    maage_render_uniforms uniforms;
    while ((mesh = maage_render_frame_next_mesh(plan, foreground, &cursor)) != NULL) {
        maage_render_draw_uniforms(plan, foreground, &cursor, &uniforms);
        if (cursor.instance && cursor.layer == MAAGE_RENDER_TRANSLUCENT) {
            maage_vulkan_mesh *geometry = mesh->platform;
            maage_vulkan_image *texture = mesh->texture->platform;
            VkDeviceSize offset = 0;
            maage_vulkan_bind(native, command, ((maage_vulkan_rendering *) native->rendering)->mesh[cursor.layer], texture->descriptor, &uniforms);
            vkCmdBindVertexBuffers(command, 0, 1, &geometry->vertices.buffer, &offset);
            maage_vulkan_indices(native, command, geometry->indices.buffer, cursor.triangle, 3);
        } else
            maage_vulkan_mesh_draw(native, command, mesh, cursor.layer, slot, &uniforms);
    }
}
static maage_status maage_vulkan_ui_prepare(maage_renderer *renderer, maage_vulkan_frame_data *frame, unsigned int layer, const maage_render_ui_data *batch) {
    if (!batch || !batch->index_count) return MAAGE_VULKAN_OK;
    MAAGE_VULKAN_TRY(maage_vulkan_host_buffer(renderer, &frame->vertices[layer], batch->vertex_count * sizeof(maage_render_ui_gpu_vertex), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT));
    MAAGE_VULKAN_TRY(maage_vulkan_host_buffer(renderer, &frame->indices[layer], batch->index_count * sizeof(unsigned int), VK_BUFFER_USAGE_INDEX_BUFFER_BIT));
    MAAGE_VULKAN_TRY(maage_render_ui_pack(batch, frame->vertices[layer].mapped, frame->indices[layer].mapped));
    MAAGE_VULKAN_TRY(maage_vulkan_flush(renderer, &frame->vertices[layer]));
    return maage_vulkan_flush(renderer, &frame->indices[layer]);
}
static void maage_vulkan_ui_draw(maage_vulkan_renderer *native, VkCommandBuffer command, maage_vulkan_frame_data *frame, unsigned int layer, const maage_render_ui_data *batch, size_t first, size_t count, int panorama) {
    maage_vulkan_rendering *gpu = native->rendering;
    maage_render_uniforms push = {0};
    VkDeviceSize offset = 0;
    maage_vulkan_image *texture;
    maage_render_ui_segment segments[3];
    unsigned int n, segment_count;
    if (!batch || !count) return;
    segment_count = maage_render_ui_segments(batch, first, count, segments);
    texture = batch->texture->platform;
    maage_render_ui_uniforms(batch, &push);
    vkCmdBindVertexBuffers(command, 0, 1, &frame->vertices[layer].buffer, &offset);
    for (n = 0; n < segment_count; ++n) {
        maage_vulkan_bind(native, command, panorama ? gpu->panorama_ui[MAAGE_RENDER_BLEND_ALPHA] : gpu->ui[segments[n].blend], texture->descriptor, &push);
        maage_vulkan_indices(native, command, frame->indices[layer].buffer, segments[n].first, segments[n].count);
    }
}
static void maage_vulkan_fullscreen(maage_vulkan_renderer *native, VkCommandBuffer command, VkPipeline pipeline, maage_vulkan_image *source, float x, float y, int blur) {
    maage_render_uniforms push = {0};
    push.parameters[0] = x;
    push.parameters[1] = y;
    push.parameters[2] = (float) MAAGE_RENDER_BLUR_SIGMA;
    push.parameters[3] = blur ? MAAGE_RENDER_BLUR_RADIUS : 0;
    maage_vulkan_bind(native, command, pipeline, source->descriptor, &push);
    vkCmdDraw(command, 3, 1, 0, 0);
}
static maage_status maage_vulkan_upload_submit(maage_renderer *renderer, maage_vulkan_frame_data *frame) {
    maage_vulkan_renderer *native = renderer->platform;
    maage_vulkan_rendering *gpu = native->rendering;
    VkSubmitInfo submit = {0};
    VkMemoryBarrier barrier = {0};
    if (!gpu->upload) return MAAGE_VULKAN_OK;
    barrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
    barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT | VK_ACCESS_INDEX_READ_BIT | VK_ACCESS_SHADER_READ_BIT;
    vkCmdPipelineBarrier(gpu->upload, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_VERTEX_INPUT_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 1, &barrier, 0, NULL, 0, NULL);
    MAAGE_VULKAN_TRY(maage_vulkan_result(vkEndCommandBuffer(gpu->upload)));
    submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submit.commandBufferCount = 1;
    submit.pCommandBuffers = &gpu->upload;
    MAAGE_VULKAN_TRY(maage_vulkan_result(vkQueueSubmit(native->graphics_queue, 1, &submit, VK_NULL_HANDLE)));
    frame->upload = gpu->upload;
    frame->staging = gpu->staging;
    gpu->upload = VK_NULL_HANDLE;
    gpu->staging = NULL;
    return MAAGE_VULKAN_OK;
}
static maage_status maage_vulkan_render_frame(maage_renderer *renderer, const maage_window_state *state, const maage_render_frame_plan *plan) {
    maage_vulkan_renderer *native = renderer->platform;
    maage_vulkan_rendering *gpu = native->rendering;
    maage_vulkan_frame *flight = NULL;
    maage_vulkan_frame_data *frame;
    uint32_t image;
    unsigned int slot;
    maage_status result;
    MAAGE_VULKAN_TRY(maage_vulkan_collect(renderer));
    result = maage_vulkan_begin(renderer, state, plan->preparing ? 2 : 0, &flight, &image);
    if (maage_status_code(result) != MAAGE_STATUS_SUCCESS) return result;
    slot = (unsigned int) (flight - native->frames);
    frame = &gpu->frames[slot];
    result = maage_vulkan_collect(renderer);
    if (maage_status_failed(result)) goto failed;
    result = maage_vulkan_targets_create(renderer);
    if (maage_status_failed(result)) goto failed;
    result = maage_vulkan_frame_targets(renderer, frame, plan->panorama_count != 0);
    if (maage_status_failed(result)) goto failed;
    for (unsigned int layer = 0; layer < 3; ++layer) {
        result = maage_vulkan_ui_prepare(renderer, frame, layer, plan->layers[layer]);
        if (maage_status_failed(result)) goto failed;
    }
    for (size_t index = 0; index < plan->ordered_count; ++index) {
        if (plan->ordered[index].instance) continue;
        result = maage_vulkan_sort(renderer, plan->ordered[index].mesh, plan->world.matrix, slot);
        if (maage_status_failed(result)) goto failed;
    }
    if (plan->foreground) {
        result = maage_vulkan_sort(renderer, plan->foreground, plan->hand.matrix, slot);
        if (maage_status_failed(result)) goto failed;
    }
    maage_vulkan_transition(native, flight->command, frame->scene.image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, 0, VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT);
    maage_vulkan_pass_begin(renderer, flight->command, gpu->scene_pass, frame->scene_framebuffer);
    {
        VkClearAttachment clear = {0};
        VkClearRect rect = {{{0, 0}, native->extent}, 0, 1};
        clear.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        memcpy(clear.clearValue.color.float32, renderer->clear, sizeof(renderer->clear));
        vkCmdClearAttachments(flight->command, 1, &clear, 1, &rect);
    }
    maage_vulkan_ui_draw(native, flight->command, frame, 0, plan->layers[0], 0, plan->layers[0] ? plan->layers[0]->index_count : 0, 0);
    maage_vulkan_mesh_passes(native, flight->command, plan, 0, slot);
    if (plan->panorama_count) {
        vkCmdEndRenderPass(flight->command);
        maage_vulkan_pass_begin(renderer, flight->command, gpu->color_pass, frame->panorama_framebuffer[0]);
        maage_vulkan_ui_draw(native, flight->command, frame, 2, plan->layers[2], 0, plan->panorama_count, 1);
        vkCmdEndRenderPass(flight->command);
        maage_vulkan_pass_begin(renderer, flight->command, gpu->color_pass, frame->panorama_framebuffer[1]);
        maage_vulkan_fullscreen(native, flight->command, gpu->blur, &frame->panorama[0], 1.0f / native->extent.width, 0, 1);
        vkCmdEndRenderPass(flight->command);
        maage_vulkan_pass_begin(renderer, flight->command, gpu->color_pass, frame->panorama_framebuffer[0]);
        maage_vulkan_fullscreen(native, flight->command, gpu->blur, &frame->panorama[1], 0, 1.0f / native->extent.height, 1);
        vkCmdEndRenderPass(flight->command);
        maage_vulkan_pass_begin(renderer, flight->command, gpu->scene_pass, frame->scene_framebuffer);
        maage_vulkan_fullscreen(native, flight->command, gpu->scene_composite, &frame->panorama[0], 0, 0, 0);
    }
    if (!plan->layers[1] || !plan->layers[1]->after_foreground) maage_vulkan_ui_draw(native, flight->command, frame, 1, plan->layers[1], 0, plan->layers[1] ? plan->layers[1]->index_count : 0, 0);
    if (plan->foreground) {
        VkClearAttachment clear = {0};
        VkClearRect rect = {{{0, 0}, native->extent}, 0, 1};
        clear.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
        clear.clearValue.depthStencil.depth = 1.0f;
        vkCmdClearAttachments(flight->command, 1, &clear, 1, &rect);
        maage_vulkan_mesh_passes(native, flight->command, plan, 1, slot);
    }
    if (plan->layers[1] && plan->layers[1]->after_foreground) maage_vulkan_ui_draw(native, flight->command, frame, 1, plan->layers[1], 0, plan->layers[1] ? plan->layers[1]->index_count : 0, 0);
    maage_vulkan_ui_draw(native, flight->command, frame, 2, plan->layers[2], plan->panorama_count, plan->layers[2] ? plan->layers[2]->index_count - plan->panorama_count : 0, 0);
    vkCmdEndRenderPass(flight->command);
    maage_vulkan_transition(native, flight->command, frame->scene.image, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT);
    maage_vulkan_pass_begin(renderer, flight->command, gpu->present_pass, gpu->present_framebuffers[image]);
    maage_vulkan_fullscreen(native, flight->command, gpu->composite, &frame->scene, 0, 0, 0);
    vkCmdEndRenderPass(flight->command);
    result = maage_vulkan_upload_submit(renderer, frame);
    if (maage_status_failed(result)) goto failed;
    frame->serial = ++gpu->submitted;
    return maage_vulkan_submit(renderer, flight, image);
failed: {
    maage_status recovery = maage_vulkan_abort(renderer, flight, image);
    if (maage_status_failed(recovery)) MAAGE_STATUS_TRACE(renderer->operation_trace, recovery, MAAGE_CONTEXT_RENDER, MAAGE_OPERATION_CANCEL);
}
    return result;
}

static maage_status maage_vulkan_create(maage_renderer *renderer) {
    maage_vulkan_renderer *native;
    maage_vulkan_rendering *gpu;
    VkDescriptorSetLayoutBinding binding = {0};
    VkDescriptorSetLayoutCreateInfo descriptors = {0};
    VkPushConstantRange constants = {0};
    VkPipelineLayoutCreateInfo layout = {0};
    VkSamplerCreateInfo sampler = {0};
    VkCommandPoolCreateInfo pool = {0};
    maage_status result = maage_vulkan_platform_create(renderer);
    if (maage_status_code(result) != MAAGE_STATUS_SUCCESS) return result;
    native = renderer->platform;
    if (!maage_render_gpu_layout_supported() ||
        native->properties.limits.maxPushConstantsSize < sizeof(maage_render_uniforms) ||
        native->properties.limits.maxVertexInputAttributes < 7 ||
        native->properties.limits.maxVertexInputBindingStride < sizeof(maage_mesh_vertex))
        return MAAGE_VULKAN_ERROR(MAAGE_STATUS_SYSTEM_NOT_SUPPORTED);
    gpu = calloc(1, sizeof(*gpu));
    if (!gpu) return MAAGE_VULKAN_ERROR(MAAGE_STATUS_ALLOCATION_FAILED);
    native->rendering = gpu;
    binding.binding = 0;
    binding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    binding.descriptorCount = 1;
    binding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    descriptors.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    descriptors.bindingCount = 1;
    descriptors.pBindings = &binding;
    MAAGE_VULKAN_TRY(maage_vulkan_result(vkCreateDescriptorSetLayout(native->device, &descriptors, NULL, &gpu->descriptors)));
    constants.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    constants.size = sizeof(maage_render_uniforms);
    layout.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    layout.setLayoutCount = 1;
    layout.pSetLayouts = &gpu->descriptors;
    layout.pushConstantRangeCount = 1;
    layout.pPushConstantRanges = &constants;
    MAAGE_VULKAN_TRY(maage_vulkan_result(vkCreatePipelineLayout(native->device, &layout, NULL, &gpu->layout)));
    sampler.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    sampler.magFilter = sampler.minFilter = VK_FILTER_NEAREST;
    sampler.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    sampler.addressModeU = sampler.addressModeV = sampler.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    MAAGE_VULKAN_TRY(maage_vulkan_result(vkCreateSampler(native->device, &sampler, NULL, &gpu->nearest)));
    sampler.magFilter = sampler.minFilter = VK_FILTER_LINEAR;
    MAAGE_VULKAN_TRY(maage_vulkan_result(vkCreateSampler(native->device, &sampler, NULL, &gpu->linear)));
    pool.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    pool.queueFamilyIndex = native->graphics_family;
    pool.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT | VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    MAAGE_VULKAN_TRY(maage_vulkan_result(vkCreateCommandPool(native->device, &pool, NULL, &gpu->upload_pool)));
    renderer->capabilities.features |= MAAGE_RENDER_CAP_SHADERS | MAAGE_RENDER_CAP_VERTEX_BUFFERS | MAAGE_RENDER_CAP_INDEX_BUFFERS | MAAGE_RENDER_CAP_RENDER_TARGETS | MAAGE_RENDER_CAP_DEPTH | MAAGE_RENDER_CAP_TEXTURED_MESH | MAAGE_RENDER_CAP_UI | MAAGE_RENDER_CAP_SCENE | MAAGE_RENDER_CAP_FOG;
    renderer->capabilities.texture_formats = MAAGE_RENDER_FORMAT_RGBA8;
    renderer->capabilities.max_frames_in_flight = MAAGE_VULKAN_FRAME_SLOTS;
    return MAAGE_VULKAN_OK;
}
static maage_status maage_vulkan_destroy(maage_renderer *renderer) {
    maage_vulkan_renderer *native = renderer->platform;
    maage_vulkan_rendering *gpu = native ? native->rendering : NULL;
    maage_status result = MAAGE_VULKAN_OK, released;
    if (!native) return maage_status_make(MAAGE_STATUS_NOOP, 0, MAAGE_OPERATION_DESTROY, 0);
    if (gpu) {
        if (native->device) result = maage_vulkan_result(vkDeviceWaitIdle(native->device));
        /* Discard unsubmitted copies before releasing their source/destination
         * resources. Submitted copies are retired by the device-idle boundary. */
        if (gpu->upload) vkFreeCommandBuffers(native->device, gpu->upload_pool, 1, &gpu->upload);
        gpu->upload = VK_NULL_HANDLE;
        maage_vulkan_garbage_destroy(native, gpu->staging);
        gpu->staging = NULL;
        for (unsigned int slot = 0; slot < MAAGE_VULKAN_FRAME_SLOTS; ++slot) {
            maage_vulkan_frame_data *frame = &gpu->frames[slot];
            if (frame->upload) vkFreeCommandBuffers(native->device, gpu->upload_pool, 1, &frame->upload);
            frame->upload = VK_NULL_HANDLE;
            maage_vulkan_garbage_destroy(native, frame->staging);
            frame->staging = NULL;
            for (unsigned int layer = 0; layer < 3; ++layer) {
                maage_vulkan_buffer_destroy(native, &frame->vertices[layer]);
                maage_vulkan_buffer_destroy(native, &frame->indices[layer]);
            }
        }
        maage_vulkan_targets_destroy(renderer);
        maage_vulkan_pipelines_destroy(native);
        maage_vulkan_garbage_destroy(native, gpu->retired);
        gpu->retired = NULL;
        while (gpu->pools) {
            maage_vulkan_descriptors *pool = gpu->pools;
            gpu->pools = pool->next;
            vkDestroyDescriptorPool(native->device, pool->pool, NULL);
            free(pool);
        }
        if (gpu->upload_pool) vkDestroyCommandPool(native->device, gpu->upload_pool, NULL);
        if (gpu->nearest) vkDestroySampler(native->device, gpu->nearest, NULL);
        if (gpu->linear) vkDestroySampler(native->device, gpu->linear, NULL);
        if (gpu->layout) vkDestroyPipelineLayout(native->device, gpu->layout, NULL);
        if (gpu->descriptors) vkDestroyDescriptorSetLayout(native->device, gpu->descriptors, NULL);
        free(gpu);
        native->rendering = NULL;
    }
    released = maage_vulkan_platform_destroy(renderer);
    return maage_status_failed(result) ? result : released;
}
static maage_status maage_vulkan_upload_geometry(maage_renderer *renderer, maage_render_mesh *mesh, const maage_mesh_data *data) {
    maage_vulkan_renderer *native = renderer->platform;
    maage_vulkan_rendering *gpu = native->rendering;
    maage_vulkan_mesh *geometry;
    maage_vulkan_garbage *staging;
    size_t vertex_bytes = data->vertex_count * sizeof(*data->vertices);
    size_t index_bytes = data->index_count * sizeof(*data->indices);
    VkBufferCopy copy = {0};
    if (vertex_bytes > (size_t) - 1 - index_bytes) return MAAGE_VULKAN_ERROR(MAAGE_STATUS_BUFFER_OVERFLOW);
    geometry = calloc(1, sizeof(*geometry));
    if (!geometry) return MAAGE_VULKAN_ERROR(MAAGE_STATUS_ALLOCATION_FAILED);
    mesh->platform = geometry;
    MAAGE_VULKAN_TRY(maage_vulkan_staging(renderer, vertex_bytes + index_bytes, &staging));
    memcpy(staging->buffer.mapped, data->vertices, vertex_bytes);
    MAAGE_VULKAN_TRY(maage_render_geometry_prepare(&mesh->geometry, data, (unsigned int *) ((unsigned char *) staging->buffer.mapped + vertex_bytes)));
    MAAGE_VULKAN_TRY(maage_vulkan_flush(renderer, &staging->buffer));
    MAAGE_VULKAN_TRY(maage_vulkan_buffer_create(renderer, vertex_bytes, VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, 0, &geometry->vertices));
    MAAGE_VULKAN_TRY(maage_vulkan_buffer_create(renderer, index_bytes, VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT, 0, &geometry->indices));
    MAAGE_VULKAN_TRY(maage_vulkan_upload_begin(renderer));
    copy.size = vertex_bytes;
    vkCmdCopyBuffer(gpu->upload, staging->buffer.buffer, geometry->vertices.buffer, 1, &copy);
    copy.srcOffset = vertex_bytes;
    copy.size = index_bytes;
    vkCmdCopyBuffer(gpu->upload, staging->buffer.buffer, geometry->indices.buffer, 1, &copy);
    /* The batched upload submission publishes all transfer writes together. */
    return MAAGE_VULKAN_OK;
}
static void maage_vulkan_destroy_mesh(maage_render_mesh *mesh) {
    maage_vulkan_mesh *geometry = mesh->platform;
    if (!geometry) return;
    if (geometry->vertices.buffer) maage_vulkan_retire(mesh->renderer, &geometry->vertices, NULL);
    if (geometry->indices.buffer) maage_vulkan_retire(mesh->renderer, &geometry->indices, NULL);
    for (unsigned int slot = 0; slot < MAAGE_VULKAN_FRAME_SLOTS; ++slot)
        if (geometry->sorted[slot].buffer) maage_vulkan_retire(mesh->renderer, &geometry->sorted[slot], NULL);
    free(geometry);
    mesh->platform = NULL;
}
static maage_status maage_vulkan_create_texture(maage_renderer *renderer, maage_render_texture *texture, const maage_pixel_buffer *atlas) {
    maage_vulkan_renderer *native = renderer->platform;
    maage_vulkan_rendering *gpu = native->rendering;
    maage_vulkan_image *image;
    maage_vulkan_garbage *staging;
    size_t row_bytes = (size_t) atlas->width * 4;
    VkImageMemoryBarrier barrier = {0};
    VkBufferImageCopy copy = {0};
    if ((unsigned int) atlas->width > native->properties.limits.maxImageDimension2D || (unsigned int) atlas->height > native->properties.limits.maxImageDimension2D) return MAAGE_VULKAN_ERROR(MAAGE_STATUS_SYSTEM_NOT_SUPPORTED);
    image = calloc(1, sizeof(*image));
    if (!image) return MAAGE_VULKAN_ERROR(MAAGE_STATUS_ALLOCATION_FAILED);
    texture->platform = image;
    MAAGE_VULKAN_TRY(maage_vulkan_staging(renderer, row_bytes * (size_t) atlas->height, &staging));
    for (int row = 0; row < atlas->height; ++row)
        memcpy((unsigned char *) staging->buffer.mapped + row_bytes * (size_t) row, atlas->pixels + atlas->stride * (size_t) row, row_bytes);
    MAAGE_VULKAN_TRY(maage_vulkan_flush(renderer, &staging->buffer));
    MAAGE_VULKAN_TRY(maage_vulkan_image_create(renderer, (uint32_t) atlas->width, (uint32_t) atlas->height, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, VK_IMAGE_ASPECT_COLOR_BIT, image));
    MAAGE_VULKAN_TRY(maage_vulkan_descriptor(renderer, image, gpu->nearest));
    MAAGE_VULKAN_TRY(maage_vulkan_upload_begin(renderer));
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier.srcQueueFamilyIndex = barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = image->image;
    barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.levelCount = barrier.subresourceRange.layerCount = 1;
    vkCmdPipelineBarrier(gpu->upload, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, NULL, 0, NULL, 1, &barrier);
    copy.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    copy.imageSubresource.layerCount = 1;
    copy.imageExtent.width = (uint32_t) atlas->width;
    copy.imageExtent.height = (uint32_t) atlas->height;
    copy.imageExtent.depth = 1;
    vkCmdCopyBufferToImage(gpu->upload, staging->buffer.buffer, image->image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy);
    barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    vkCmdPipelineBarrier(gpu->upload, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, NULL, 0, NULL, 1, &barrier);
    return MAAGE_VULKAN_OK;
}
static void maage_vulkan_destroy_texture(maage_render_texture *texture) {
    maage_vulkan_image *image = texture->platform;
    if (!image) return;
    if (image->image || image->descriptor) maage_vulkan_retire(texture->renderer, NULL, image);
    free(image);
    texture->platform = NULL;
}
static maage_status maage_vulkan_set_vsync(maage_renderer *renderer, int enabled) {
    maage_vulkan_renderer *native = renderer->platform;
    if (native->vsync == enabled) return maage_status_make(MAAGE_STATUS_NOOP, 0, MAAGE_OPERATION_SET, 0);
    native->vsync = enabled;
    renderer->refresh = 1;
    return maage_status_make(MAAGE_STATUS_SUCCESS, 0, MAAGE_OPERATION_SET, 0);
}
const maage_render_backend_api maage_render_vulkan_api = {
    MAAGE_RENDER_VULKAN, maage_vulkan_create, maage_vulkan_destroy,
    MAAGE_RENDER_CAPABILITIES_VERSION, NULL, maage_vulkan_render_frame, maage_vulkan_upload_geometry,
    maage_vulkan_destroy_mesh, maage_vulkan_create_texture,
    maage_vulkan_destroy_texture, maage_vulkan_set_vsync};
