/*
 * MyceliumVR - a social VR platform built on the Ladybird browser engine
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include "../VulkanCommon.h"
#include <AK/Vector.h>
#include <vk_mem_alloc.h>

namespace MyceliumVR {

struct VulkanSwapchain {
    VkSwapchainKHR swapchain { VK_NULL_HANDLE };
    VkFormat image_format { VK_FORMAT_UNDEFINED };
    VkExtent2D extent {};
    Vector<VkImage> images;
    Vector<VkImageView> image_views;

    VkImage depth_image { VK_NULL_HANDLE };
    VmaAllocation depth_allocation { VK_NULL_HANDLE };
    VkImageView depth_view { VK_NULL_HANDLE };
    VkFormat depth_format { VK_FORMAT_D32_SFLOAT };

    VkSemaphore image_available { VK_NULL_HANDLE };
    // One semaphore per swapchain image — avoids signalling a semaphore that the
    // presentation engine may still be holding from the previous present of the
    // same image. Index with the image index returned by vkAcquireNextImageKHR.
    Vector<VkSemaphore> render_finished_semaphores;
    VkFence in_flight { VK_NULL_HANDLE };

    void destroy(VkDevice device, VmaAllocator allocator) {
        if (in_flight != VK_NULL_HANDLE) vkDestroyFence(device, in_flight, nullptr);
        for (auto sem : render_finished_semaphores) vkDestroySemaphore(device, sem, nullptr);
        render_finished_semaphores.clear();
        if (image_available != VK_NULL_HANDLE) vkDestroySemaphore(device, image_available, nullptr);
        in_flight = VK_NULL_HANDLE;
        image_available = VK_NULL_HANDLE;

        if (depth_view != VK_NULL_HANDLE) vkDestroyImageView(device, depth_view, nullptr);
        if (depth_image != VK_NULL_HANDLE) vmaDestroyImage(allocator, depth_image, depth_allocation);
        depth_view = VK_NULL_HANDLE;
        depth_image = VK_NULL_HANDLE;
        depth_allocation = VK_NULL_HANDLE;

        for (auto view : image_views) vkDestroyImageView(device, view, nullptr);
        image_views.clear();

        if (swapchain != VK_NULL_HANDLE) vkDestroySwapchainKHR(device, swapchain, nullptr);
        swapchain = VK_NULL_HANDLE;
    }
};

class VulkanSwapchainManager {
public:
    static ErrorOr<void> ensure_swapchain_resources(
        VkPhysicalDevice physical_device,
        VkDevice device,
        VkSurfaceKHR surface,
        VmaAllocator allocator,
        u32 width,
        u32 height,
        VulkanSwapchain& out_swapchain);
};

}
