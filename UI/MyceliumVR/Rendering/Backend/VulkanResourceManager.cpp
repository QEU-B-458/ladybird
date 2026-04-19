/*
 * MyceliumVR - a social VR platform built on the Ladybird browser engine
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "VulkanResourceManager.h"
#include <AK/ScopeGuard.h>
#include <LibGfx/Bitmap.h>

namespace MyceliumVR {

#if defined(USE_VULKAN)

ErrorOr<void> VulkanResourceManager::create_buffer(VmaAllocator allocator, VkDeviceSize size, VkBufferUsageFlags usage, VmaMemoryUsage mem_usage, VulkanBuffer& out_buffer)
{
    VkBufferCreateInfo buffer_create_info {};
    buffer_create_info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    buffer_create_info.size = size;
    buffer_create_info.usage = usage;
    buffer_create_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VmaAllocationCreateInfo alloc_info {};
    alloc_info.usage = mem_usage;

    VkBuffer buffer { VK_NULL_HANDLE };
    VmaAllocation allocation { VK_NULL_HANDLE };
    TRY(check_vulkan_result(vmaCreateBuffer(allocator, &buffer_create_info, &alloc_info, &buffer, &allocation, nullptr), "vmaCreateBuffer failed"sv));

    out_buffer.buffer = buffer;
    out_buffer.allocation = allocation;
    out_buffer.size = size;
    return {};
}

ErrorOr<void> VulkanResourceManager::create_image(VmaAllocator allocator, u32 width, u32 height, VkFormat format, VkImageTiling tiling, VkImageUsageFlags usage, VmaMemoryUsage mem_usage, VkImage& out_image, VmaAllocation& out_allocation)
{
    VkImageCreateInfo image_create_info {};
    image_create_info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    image_create_info.imageType = VK_IMAGE_TYPE_2D;
    image_create_info.extent = { width, height, 1 };
    image_create_info.mipLevels = 1;
    image_create_info.arrayLayers = 1;
    image_create_info.format = format;
    image_create_info.tiling = tiling;
    image_create_info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    image_create_info.usage = usage;
    image_create_info.samples = VK_SAMPLE_COUNT_1_BIT;
    image_create_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VmaAllocationCreateInfo alloc_info {};
    alloc_info.usage = mem_usage;

    VkImage image { VK_NULL_HANDLE };
    VmaAllocation allocation { VK_NULL_HANDLE };
    TRY(check_vulkan_result(vmaCreateImage(allocator, &image_create_info, &alloc_info, &image, &allocation, nullptr), "vmaCreateImage failed"sv));

    out_image = image;
    out_allocation = allocation;
    return {};
}

ErrorOr<void> VulkanResourceManager::copy_buffer(VkDevice device, VkCommandPool command_pool, VkQueue queue, VkBuffer src, VkBuffer dst, VkDeviceSize size)
{
    VkCommandBufferAllocateInfo alloc_info {};
    alloc_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    alloc_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    alloc_info.commandPool = command_pool;
    alloc_info.commandBufferCount = 1;

    VkCommandBuffer command_buffer { VK_NULL_HANDLE };
    TRY(check_vulkan_result(vkAllocateCommandBuffers(device, &alloc_info, &command_buffer), "vkAllocateCommandBuffers failed"sv));

    VkCommandBufferBeginInfo begin_info {};
    begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    begin_info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

    TRY(check_vulkan_result(vkBeginCommandBuffer(command_buffer, &begin_info), "vkBeginCommandBuffer failed"sv));

    VkBufferCopy copy_region {};
    copy_region.size = size;
    vkCmdCopyBuffer(command_buffer, src, dst, 1, &copy_region);

    TRY(check_vulkan_result(vkEndCommandBuffer(command_buffer), "vkEndCommandBuffer failed"sv));

    VkSubmitInfo submit_info {};
    submit_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submit_info.commandBufferCount = 1;
    submit_info.pCommandBuffers = &command_buffer;

    TRY(check_vulkan_result(vkQueueSubmit(queue, 1, &submit_info, VK_NULL_HANDLE), "vkQueueSubmit failed"sv));
    TRY(check_vulkan_result(vkQueueWaitIdle(queue), "vkQueueWaitIdle failed"sv));

    vkFreeCommandBuffers(device, command_pool, 1, &command_buffer);
    return {};
}

ErrorOr<void> VulkanResourceManager::copy_buffer_to_image_synchronous(VkDevice device, VkCommandPool command_pool, VkQueue queue, VkBuffer src, VkImage dst, u32 width, u32 height)
{
    VkCommandBufferAllocateInfo alloc_info {};
    alloc_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    alloc_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    alloc_info.commandPool = command_pool;
    alloc_info.commandBufferCount = 1;

    VkCommandBuffer command_buffer { VK_NULL_HANDLE };
    TRY(check_vulkan_result(vkAllocateCommandBuffers(device, &alloc_info, &command_buffer), "vkAllocateCommandBuffers failed"sv));

    VkCommandBufferBeginInfo begin_info {};
    begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    begin_info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

    TRY(check_vulkan_result(vkBeginCommandBuffer(command_buffer, &begin_info), "vkBeginCommandBuffer failed"sv));

    transition_image_layout(command_buffer, dst, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
    copy_buffer_to_image(command_buffer, src, dst, width, height);
    transition_image_layout(command_buffer, dst, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

    TRY(check_vulkan_result(vkEndCommandBuffer(command_buffer), "vkEndCommandBuffer failed"sv));

    VkSubmitInfo submit_info {};
    submit_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submit_info.commandBufferCount = 1;
    submit_info.pCommandBuffers = &command_buffer;

    TRY(check_vulkan_result(vkQueueSubmit(queue, 1, &submit_info, VK_NULL_HANDLE), "vkQueueSubmit failed"sv));
    TRY(check_vulkan_result(vkQueueWaitIdle(queue), "vkQueueWaitIdle failed"sv));

    vkFreeCommandBuffers(device, command_pool, 1, &command_buffer);
    return {};
}

void VulkanResourceManager::transition_image_layout(VkCommandBuffer cmd, VkImage image, VkImageLayout old_layout, VkImageLayout new_layout)
{
    VkPipelineStageFlags2 source_stage;
    VkPipelineStageFlags2 destination_stage;
    VkAccessFlags2 src_access_mask = 0;
    VkAccessFlags2 dst_access_mask = 0;

    if (old_layout == VK_IMAGE_LAYOUT_UNDEFINED && new_layout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL) {
        dst_access_mask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
        source_stage = VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT;
        destination_stage = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
    } else if (old_layout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL && new_layout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) {
        src_access_mask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
        dst_access_mask = VK_ACCESS_2_SHADER_SAMPLED_READ_BIT;
        source_stage = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
        destination_stage = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
    } else if (old_layout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL && new_layout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL) {
        src_access_mask = VK_ACCESS_2_SHADER_SAMPLED_READ_BIT;
        dst_access_mask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
        source_stage = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
        destination_stage = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
    } else {
        source_stage = VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT;
        destination_stage = VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT;
    }

    VkImageMemoryBarrier2 barrier {
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
        .pNext = nullptr,
        .srcStageMask = source_stage,
        .srcAccessMask = src_access_mask,
        .dstStageMask = destination_stage,
        .dstAccessMask = dst_access_mask,
        .oldLayout = old_layout,
        .newLayout = new_layout,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image = image,
        .subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 },
    };
    VkDependencyInfo dependency_info {
        .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
        .pNext = nullptr,
        .dependencyFlags = 0,
        .memoryBarrierCount = 0,
        .pMemoryBarriers = nullptr,
        .bufferMemoryBarrierCount = 0,
        .pBufferMemoryBarriers = nullptr,
        .imageMemoryBarrierCount = 1,
        .pImageMemoryBarriers = &barrier,
    };
    vkCmdPipelineBarrier2(cmd, &dependency_info);
}

void VulkanResourceManager::copy_buffer_to_image(VkCommandBuffer cmd, VkBuffer buffer, VkImage image, u32 width, u32 height)
{
    VkBufferImageCopy region {};
    region.bufferOffset = 0;
    region.bufferRowLength = 0;
    region.bufferImageHeight = 0;
    region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    region.imageSubresource.mipLevel = 0;
    region.imageSubresource.baseArrayLayer = 0;
    region.imageSubresource.layerCount = 1;
    region.imageOffset = { 0, 0, 0 };
    region.imageExtent = { width, height, 1 };

    vkCmdCopyBufferToImage(cmd, buffer, image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
}

ErrorOr<void> VulkanResourceManager::copy_bitmap_to_mapped_staging(Gfx::Bitmap const& bitmap, void* mapped_memory, u32 width, u32 height)
{
    auto expected_width = static_cast<int>(width);
    auto expected_height = static_cast<int>(height);
    if (bitmap.width() != expected_width || bitmap.height() != expected_height)
        return Error::from_string_literal("Bitmap size mismatch for staging upload");

    auto row_bytes = static_cast<size_t>(width) * 4;
    auto* destination = static_cast<u8*>(mapped_memory);
    for (u32 y = 0; y < height; ++y) {
        auto const* source = bitmap.scanline_u8(static_cast<int>(y));
        __builtin_memcpy(destination + (static_cast<size_t>(y) * row_bytes), source, row_bytes);
    }
    return {};
}

ErrorOr<void> VulkanResourceManager::ensure_texture_resource(
    VkDevice device,
    VmaAllocator allocator,
    u32 width,
    u32 height,
    VulkanTexture& resource)
{
    if (width == 0 || height == 0)
        return {};

    if (resource.image != VK_NULL_HANDLE && resource.width == width && resource.height == height)
        return {};

    resource.destroy(device, allocator);

    TRY(create_image(allocator, width, height, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_TILING_OPTIMAL, VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, VMA_MEMORY_USAGE_GPU_ONLY, resource.image, resource.allocation));

    VkImageViewCreateInfo image_view_create_info {};
    image_view_create_info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    image_view_create_info.image = resource.image;
    image_view_create_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
    image_view_create_info.format = VK_FORMAT_R8G8B8A8_UNORM;
    image_view_create_info.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    image_view_create_info.subresourceRange.baseMipLevel = 0;
    image_view_create_info.subresourceRange.levelCount = 1;
    image_view_create_info.subresourceRange.baseArrayLayer = 0;
    image_view_create_info.subresourceRange.layerCount = 1;
    TRY(check_vulkan_result(vkCreateImageView(device, &image_view_create_info, nullptr, &resource.view), "vkCreateImageView failed"sv));

    VkSamplerCreateInfo sampler_create_info {};
    sampler_create_info.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    sampler_create_info.magFilter = VK_FILTER_LINEAR;
    sampler_create_info.minFilter = VK_FILTER_LINEAR;
    sampler_create_info.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sampler_create_info.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sampler_create_info.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sampler_create_info.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    sampler_create_info.maxLod = 1.0f;
    TRY(check_vulkan_result(vkCreateSampler(device, &sampler_create_info, nullptr, &resource.sampler), "vkCreateSampler failed"sv));

    resource.width = width;
    resource.height = height;
    return {};
}

#endif

}
