/*
 * MyceliumVR - a social VR platform built on the Ladybird browser engine
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include "../VulkanCommon.h"
#include <vk_mem_alloc.h>

namespace MyceliumVR {

#if defined(USE_VULKAN)

struct VulkanBuffer {
    VkBuffer buffer { VK_NULL_HANDLE };
    VmaAllocation allocation { VK_NULL_HANDLE };
    VkDeviceSize size { 0 };

    void destroy(VmaAllocator allocator) {
        if (buffer != VK_NULL_HANDLE) vmaDestroyBuffer(allocator, buffer, allocation);
        buffer = VK_NULL_HANDLE;
        allocation = VK_NULL_HANDLE;
        size = 0;
    }
};

struct VulkanTexture {
    VkImage image { VK_NULL_HANDLE };
    VmaAllocation allocation { VK_NULL_HANDLE };
    VkImageView view { VK_NULL_HANDLE };
    VkSampler sampler { VK_NULL_HANDLE };
    VkDescriptorPool descriptor_pool { VK_NULL_HANDLE };
    VkDescriptorSet descriptor_set { VK_NULL_HANDLE };
    u32 width { 0 };
    u32 height { 0 };

    void destroy(VkDevice device, VmaAllocator allocator) {
        if (descriptor_pool != VK_NULL_HANDLE) vkDestroyDescriptorPool(device, descriptor_pool, nullptr);
        if (sampler != VK_NULL_HANDLE) vkDestroySampler(device, sampler, nullptr);
        if (view != VK_NULL_HANDLE) vkDestroyImageView(device, view, nullptr);
        if (image != VK_NULL_HANDLE) vmaDestroyImage(allocator, image, allocation);
        image = VK_NULL_HANDLE;
        allocation = VK_NULL_HANDLE;
        view = VK_NULL_HANDLE;
        sampler = VK_NULL_HANDLE;
        descriptor_pool = VK_NULL_HANDLE;
        descriptor_set = VK_NULL_HANDLE;
    }
};

class VulkanResourceManager {
public:
    static ErrorOr<void> create_buffer(VmaAllocator, VkDeviceSize, VkBufferUsageFlags, VmaMemoryUsage, VulkanBuffer&);
    static ErrorOr<void> create_image(VmaAllocator, u32 width, u32 height, VkFormat, VkImageTiling, VkImageUsageFlags, VmaMemoryUsage, VkImage&, VmaAllocation&);

    static ErrorOr<void> copy_buffer(VkDevice, VkCommandPool, VkQueue, VkBuffer src, VkBuffer dst, VkDeviceSize size);
    static ErrorOr<void> copy_buffer_to_image_synchronous(VkDevice, VkCommandPool, VkQueue, VkBuffer, VkImage, u32 width, u32 height);

    static void transition_image_layout(VkCommandBuffer, VkImage, VkImageLayout old_layout, VkImageLayout new_layout);
    static void copy_buffer_to_image(VkCommandBuffer, VkBuffer, VkImage, u32 width, u32 height);
    static ErrorOr<void> copy_bitmap_to_mapped_staging(Gfx::Bitmap const&, void* mapped_memory, u32 width, u32 height);

    static ErrorOr<void> ensure_texture_resource(VkDevice, VmaAllocator, u32 width, u32 height, VulkanTexture&);
};

#endif

}
