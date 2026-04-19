/*
 * MyceliumVR - a social VR platform built on the Ladybird browser engine
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "VulkanBackend.h"
#include "VulkanDebug.h"

namespace MyceliumVR {

ErrorOr<NonnullOwnPtr<VulkanBackend>> VulkanBackend::create(VulkanContext& ctx)
{
    auto backend = TRY(adopt_nonnull_own_or_enomem(new (nothrow) VulkanBackend(ctx)));
    TRY(backend->initialize());
    return backend;
}

VulkanBackend::VulkanBackend(VulkanContext& ctx)
    : m_context(ctx)
{
}

VulkanBackend::~VulkanBackend()
{
#if defined(TRACY_ENABLE)
    if (m_tracy_vk_ctx) {
        TracyVkDestroy(m_tracy_vk_ctx);
        m_tracy_vk_ctx = nullptr;
    }
#endif

    if (m_frame_pool != VK_NULL_HANDLE)
        vkDestroyCommandPool(m_context.device(), m_frame_pool, nullptr);
}

ErrorOr<void> VulkanBackend::initialize()
{
    VkCommandPoolCreateInfo pool_info {
        .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
        .pNext = nullptr,
        .flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
        .queueFamilyIndex = m_context.graphics_queue_family()
    };
    TRY(check_vulkan_result(vkCreateCommandPool(m_context.device(), &pool_info, nullptr, &m_frame_pool), "vkCreateCommandPool failed"sv));
    VulkanDebug::name(m_context.device(), m_frame_pool, VK_OBJECT_TYPE_COMMAND_POOL, "MyceliumVR/FramePool"sv);

    {
        VkCommandBufferAllocateInfo ai {
            .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
            .pNext = nullptr,
            .commandPool = m_frame_pool,
            .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
            .commandBufferCount = 1
        };
        TRY(check_vulkan_result(vkAllocateCommandBuffers(m_context.device(), &ai, &m_frame_cmd), "vkAllocateCommandBuffers for frame failed"sv));
        VulkanDebug::name(m_context.device(), m_frame_cmd, VK_OBJECT_TYPE_COMMAND_BUFFER, "MyceliumVR/FrameCommandBuffer"sv);
    }

#if defined(TRACY_ENABLE)
    // Allocate a dedicated, persistent command buffer for Tracy queries.
    // This avoids interfering with the recording state of per-frame buffers.
    VkCommandBufferAllocateInfo ai {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .pNext = nullptr,
        .commandPool = m_frame_pool,
        .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
        .commandBufferCount = 1
    };
    TRY(check_vulkan_result(vkAllocateCommandBuffers(m_context.device(), &ai, &m_tracy_cmd), "vkAllocateCommandBuffers for Tracy failed"sv));
    VulkanDebug::name(m_context.device(), m_tracy_cmd, VK_OBJECT_TYPE_COMMAND_BUFFER, "MyceliumVR/TracyCommandBuffer"sv);

    // TracyVkContext internally calls vkBeginCommandBuffer / vkEndCommandBuffer / vkQueueSubmit / vkQueueWaitIdle.
    m_tracy_vk_ctx = TracyVkContext(m_context.physical_device(), m_context.device(), m_context.graphics_queue(), m_tracy_cmd);
#endif

    return {};
}

ErrorOr<VulkanFrameContext> VulkanBackend::begin_frame(VulkanSwapchain& sc, u32& image_index)
{
    if (sc.in_flight != VK_NULL_HANDLE) {
        vkWaitForFences(m_context.device(), 1, &sc.in_flight, VK_TRUE, UINT64_MAX);
        vkResetFences(m_context.device(), 1, &sc.in_flight);
    }

    // Reset the pool to reclaim all per-frame buffers from the previous cycle.
    vkResetCommandPool(m_context.device(), m_frame_pool, 0);

    {
        ZoneScopedN("Vulkan/AcquireImage");
        TRY(check_vulkan_result(vkAcquireNextImageKHR(m_context.device(), sc.swapchain, UINT64_MAX, sc.image_available, VK_NULL_HANDLE, &image_index), "vkAcquireNextImageKHR failed"sv));
    }

    VulkanFrameContext frame;
    frame.command_buffer = m_frame_cmd;

    VkCommandBufferBeginInfo b_i {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .pNext = nullptr,
        .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
        .pInheritanceInfo = nullptr
    };
    TRY(check_vulkan_result(vkBeginCommandBuffer(frame.command_buffer, &b_i), "vkBeginCommandBuffer failed"sv));

#if defined(TRACY_ENABLE)
    if (m_tracy_vk_ctx)
        TracyVkCollect(m_tracy_vk_ctx, frame.command_buffer);
#endif

    return frame;
}

ErrorOr<void> VulkanBackend::end_frame(VulkanSwapchain& sc, u32 image_index, VkCommandBuffer cmd)
{
    TRY(check_vulkan_result(vkEndCommandBuffer(cmd), "vkEndCommandBuffer failed"sv));

    VkPipelineStageFlags wait_stages[] = { VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT };
    VkSemaphore& render_finished = sc.render_finished_semaphores[image_index];
    
    VkSubmitInfo submit_info {
        .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
        .pNext = nullptr,
        .waitSemaphoreCount = 1,
        .pWaitSemaphores = &sc.image_available,
        .pWaitDstStageMask = wait_stages,
        .commandBufferCount = 1,
        .pCommandBuffers = &cmd,
        .signalSemaphoreCount = 1,
        .pSignalSemaphores = &render_finished
    };
    TRY(check_vulkan_result(vkQueueSubmit(m_context.graphics_queue(), 1, &submit_info, sc.in_flight), "vkQueueSubmit failed"sv));

    VkPresentInfoKHR present_info {
        .sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
        .pNext = nullptr,
        .waitSemaphoreCount = 1,
        .pWaitSemaphores = &render_finished,
        .swapchainCount = 1,
        .pSwapchains = &sc.swapchain,
        .pImageIndices = &image_index,
        .pResults = nullptr
    };
    {
        ZoneScopedN("Vulkan/Present");
        TRY(check_vulkan_result(vkQueuePresentKHR(m_context.graphics_queue(), &present_info), "vkQueuePresentKHR failed"sv));
    }

    return {};
}

}
