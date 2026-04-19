/*
 * MyceliumVR - a social VR platform built on the Ladybird browser engine
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include "../VulkanCommon.h"
#include "VulkanContext.h"
#include "VulkanSwapchain.h"
#include <AK/NonnullOwnPtr.h>

#if defined(TRACY_ENABLE)
#    include <tracy/TracyVulkan.hpp>
#endif

namespace MyceliumVR {

struct VulkanFrameContext {
    VkCommandBuffer command_buffer { VK_NULL_HANDLE };
};

class VulkanBackend {
public:
    static ErrorOr<NonnullOwnPtr<VulkanBackend>> create(VulkanContext& ctx);
    ~VulkanBackend();

    VulkanContext& context() { return m_context; }
    VkCommandPool frame_pool() { return m_frame_pool; }

    ErrorOr<VulkanFrameContext> begin_frame(VulkanSwapchain& sc, u32& image_index);
    ErrorOr<void> end_frame(VulkanSwapchain& sc, u32 image_index, VkCommandBuffer cmd);

#if defined(TRACY_ENABLE)
    TracyVkCtx tracy_vk_ctx() { return m_tracy_vk_ctx; }
#endif

private:
    explicit VulkanBackend(VulkanContext& ctx);
    ErrorOr<void> initialize();

    VulkanContext& m_context;
    VkCommandPool m_frame_pool { VK_NULL_HANDLE };
    VkCommandBuffer m_frame_cmd { VK_NULL_HANDLE };

#if defined(TRACY_ENABLE)
    TracyVkCtx m_tracy_vk_ctx { nullptr };
    VkCommandBuffer m_tracy_cmd { VK_NULL_HANDLE };
#endif
};

}
