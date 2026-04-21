/*
 * MyceliumVR - a social VR platform built on the Ladybird browser engine
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include "../VulkanCommon.h"
#include "VulkanDebug.h"
#include <AK/NonnullOwnPtr.h>
#include <SDL3/SDL.h>
#include <vk_mem_alloc.h>

namespace MyceliumVR {

#if defined(USE_VULKAN)

class VulkanContext {
public:
    static ErrorOr<NonnullOwnPtr<VulkanContext>> create(SDL_Window& window);
    ~VulkanContext();

    VkInstance instance() const { return m_instance; }
    VkSurfaceKHR surface() const { return m_surface; }
    VkPhysicalDevice physical_device() const { return m_physical_device; }
    VkDevice device() const { return m_device; }
    VkQueue graphics_queue() const { return m_graphics_queue; }
    u32 graphics_queue_family() const { return m_graphics_queue_family; }
    VmaAllocator allocator() const { return m_allocator; }
    bool supports_multi_draw_indirect() const { return m_supports_multi_draw_indirect; }
    bool supports_external_image_import() const { return m_supports_external_image_import; }
    float timestamp_period() const { return m_timestamp_period; }

private:
    VulkanContext() = default;
    ErrorOr<void> initialize(SDL_Window& window);

    VkInstance m_instance { VK_NULL_HANDLE };
    VkSurfaceKHR m_surface { VK_NULL_HANDLE };
    VkPhysicalDevice m_physical_device { VK_NULL_HANDLE };
    VkDevice m_device { VK_NULL_HANDLE };
    VkQueue m_graphics_queue { VK_NULL_HANDLE };
    u32 m_graphics_queue_family { 0 };
    VmaAllocator m_allocator { VK_NULL_HANDLE };
    bool m_supports_multi_draw_indirect { false };
    bool m_supports_external_image_import { false };
    float m_timestamp_period { 1.0f };

    VkDebugUtilsMessengerEXT m_debug_messenger { VK_NULL_HANDLE };
};

#endif

}
