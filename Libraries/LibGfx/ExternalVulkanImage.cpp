/*
 * Copyright (c) 2024, MyceliumVR Contributors
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <AK/Debug.h>
#include <AK/Format.h>
#include <AK/Vector.h>
#include <LibGfx/ExternalVulkanImage.h>

#ifdef USE_VULKAN

#    ifndef _WIN32
#        include <unistd.h>
#    endif

namespace Gfx {

// ── ExternalMemoryHandle ─────────────────────────────────────────────────────

VkExternalMemoryHandleTypeFlagBits ExternalMemoryHandle::vulkan_handle_type()
{
#    ifdef _WIN32
    return VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_WIN32_BIT;
#    else
    return VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD_BIT;
#    endif
}

ExternalMemoryHandle::~ExternalMemoryHandle()
{
    if (native == invalid_native)
        return;
#    ifdef _WIN32
    CloseHandle(native);
#    else
    ::close(native);
#    endif
    native = invalid_native;
}

ExternalMemoryHandle::ExternalMemoryHandle(ExternalMemoryHandle&& other) noexcept
    : native(other.native)
    , allocation_size(other.allocation_size)
    , memory_type_index(other.memory_type_index)
    , format(other.format)
    , width(other.width)
    , height(other.height)
{
    other.native = invalid_native;
}

ExternalMemoryHandle& ExternalMemoryHandle::operator=(ExternalMemoryHandle&& other) noexcept
{
    if (this != &other) {
        this->~ExternalMemoryHandle();
        native = other.native;
        allocation_size = other.allocation_size;
        memory_type_index = other.memory_type_index;
        format = other.format;
        width = other.width;
        height = other.height;
        other.native = invalid_native;
    }
    return *this;
}

// ── ExportableVulkanImage ─────────────────────────────────────────────────────

ExportableVulkanImage::ExportableVulkanImage(VulkanContext const& context)
    : m_context(context)
{
}

ExportableVulkanImage::~ExportableVulkanImage()
{
    if (m_command_buffer != VK_NULL_HANDLE)
        vkFreeCommandBuffers(m_context.logical_device, m_command_pool, 1, &m_command_buffer);
    if (m_command_pool != VK_NULL_HANDLE)
        vkDestroyCommandPool(m_context.logical_device, m_command_pool, nullptr);
    if (m_image != VK_NULL_HANDLE)
        vkDestroyImage(m_context.logical_device, m_image, nullptr);
    if (m_memory != VK_NULL_HANDLE)
        vkFreeMemory(m_context.logical_device, m_memory, nullptr);
}

ErrorOr<NonnullRefPtr<ExportableVulkanImage>> ExportableVulkanImage::create(
    VulkanContext const& context,
    uint32_t width,
    uint32_t height,
    VkFormat format)
{
    auto image = adopt_ref(*new ExportableVulkanImage(context));

    // Load platform-specific export function pointer.
#    ifndef _WIN32
    image->m_pfn_get_memory_fd = reinterpret_cast<PFN_vkGetMemoryFdKHR>(
        vkGetDeviceProcAddr(context.logical_device, "vkGetMemoryFdKHR"));
    if (!image->m_pfn_get_memory_fd)
        return Error::from_string_literal("vkGetMemoryFdKHR unavailable — VK_KHR_external_memory_fd not loaded");
#    else
    image->m_pfn_get_memory_handle = reinterpret_cast<PFN_vkGetMemoryWin32HandleKHR>(
        vkGetDeviceProcAddr(context.logical_device, "vkGetMemoryWin32HandleKHR"));
    if (!image->m_pfn_get_memory_handle)
        return Error::from_string_literal("vkGetMemoryWin32HandleKHR unavailable — VK_KHR_external_memory_win32 not loaded");
#    endif

    // Create a private command pool for layout transitions.
    VkCommandPoolCreateInfo pool_info = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
        .pNext = nullptr,
        .flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
        .queueFamilyIndex = context.graphics_queue_family,
    };
    if (vkCreateCommandPool(context.logical_device, &pool_info, nullptr, &image->m_command_pool) != VK_SUCCESS)
        return Error::from_string_literal("ExportableVulkanImage: command pool creation failed");

    VkCommandBufferAllocateInfo buf_info = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .pNext = nullptr,
        .commandPool = image->m_command_pool,
        .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
        .commandBufferCount = 1,
    };
    if (vkAllocateCommandBuffers(context.logical_device, &buf_info, &image->m_command_buffer) != VK_SUCCESS)
        return Error::from_string_literal("ExportableVulkanImage: command buffer allocation failed");

    // Create the exportable VkImage.
    VkExternalMemoryImageCreateInfo ext_image_info = {
        .sType = VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_IMAGE_CREATE_INFO,
        .pNext = nullptr,
        .handleTypes = static_cast<VkExternalMemoryHandleTypeFlags>(ExternalMemoryHandle::vulkan_handle_type()),
    };
    VkImageCreateInfo image_info = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .pNext = &ext_image_info,
        .flags = 0,
        .imageType = VK_IMAGE_TYPE_2D,
        .format = format,
        .extent = { .width = width, .height = height, .depth = 1 },
        .mipLevels = 1,
        .arrayLayers = 1,
        .samples = VK_SAMPLE_COUNT_1_BIT,
        .tiling = VK_IMAGE_TILING_OPTIMAL,
        .usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT
            | VK_IMAGE_USAGE_SAMPLED_BIT
            | VK_IMAGE_USAGE_TRANSFER_DST_BIT
            | VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
        .queueFamilyIndexCount = 0,
        .pQueueFamilyIndices = nullptr,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
    };
    if (vkCreateImage(context.logical_device, &image_info, nullptr, &image->m_image) != VK_SUCCESS)
        return Error::from_string_literal("ExportableVulkanImage: vkCreateImage failed");

    // Allocate device-local memory with export flag.
    VkMemoryRequirements mem_reqs;
    vkGetImageMemoryRequirements(context.logical_device, image->m_image, &mem_reqs);

    VkPhysicalDeviceMemoryProperties mem_props;
    vkGetPhysicalDeviceMemoryProperties(context.physical_device, &mem_props);

    uint32_t mem_type_idx = mem_props.memoryTypeCount;
    for (uint32_t i = 0; i < mem_props.memoryTypeCount; ++i) {
        if ((mem_reqs.memoryTypeBits & (1u << i))
            && (mem_props.memoryTypes[i].propertyFlags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)) {
            mem_type_idx = i;
            break;
        }
    }
    if (mem_type_idx == mem_props.memoryTypeCount)
        return Error::from_string_literal("ExportableVulkanImage: no suitable device-local memory type");

    // Dedicated allocation required on NVIDIA 10-series and for OPAQUE_FD correctness.
    VkMemoryDedicatedAllocateInfo dedicated_info = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_DEDICATED_ALLOCATE_INFO,
        .pNext = nullptr,
        .image = image->m_image,
        .buffer = VK_NULL_HANDLE,
    };
    VkExportMemoryAllocateInfo export_alloc = {
        .sType = VK_STRUCTURE_TYPE_EXPORT_MEMORY_ALLOCATE_INFO,
        .pNext = &dedicated_info,
        .handleTypes = static_cast<VkExternalMemoryHandleTypeFlags>(ExternalMemoryHandle::vulkan_handle_type()),
    };
    VkMemoryAllocateInfo alloc_info = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .pNext = &export_alloc,
        .allocationSize = mem_reqs.size,
        .memoryTypeIndex = mem_type_idx,
    };
    if (vkAllocateMemory(context.logical_device, &alloc_info, nullptr, &image->m_memory) != VK_SUCCESS)
        return Error::from_string_literal("ExportableVulkanImage: vkAllocateMemory failed");

    if (vkBindImageMemory(context.logical_device, image->m_image, image->m_memory, 0) != VK_SUCCESS)
        return Error::from_string_literal("ExportableVulkanImage: vkBindImageMemory failed");

    image->m_format = format;
    image->m_width = width;
    image->m_height = height;
    image->m_allocation_size = mem_reqs.size;
    image->m_memory_type_index = mem_type_idx;

    // Transition to a layout Skia can render into immediately.
    image->transition_layout(VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL);

    return image;
}

ErrorOr<ExternalMemoryHandle> ExportableVulkanImage::export_handle() const
{
    ExternalMemoryHandle handle;
    handle.allocation_size = m_allocation_size;
    handle.memory_type_index = m_memory_type_index;
    handle.format = m_format;
    handle.width = m_width;
    handle.height = m_height;

#    ifndef _WIN32
    VkMemoryGetFdInfoKHR get_fd_info = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_GET_FD_INFO_KHR,
        .pNext = nullptr,
        .memory = m_memory,
        .handleType = ExternalMemoryHandle::vulkan_handle_type(),
    };
    VkResult result = m_pfn_get_memory_fd(m_context.logical_device, &get_fd_info, &handle.native);
    if (result != VK_SUCCESS) {
        dbgln("ExportableVulkanImage::export_handle: vkGetMemoryFdKHR returned {}", to_underlying(result));
        return Error::from_string_literal("ExportableVulkanImage: failed to export memory fd");
    }
#    else
    VkMemoryGetWin32HandleInfoKHR get_handle_info = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_GET_WIN32_HANDLE_INFO_KHR,
        .pNext = nullptr,
        .memory = m_memory,
        .handleType = ExternalMemoryHandle::vulkan_handle_type(),
    };
    VkResult result = m_pfn_get_memory_handle(m_context.logical_device, &get_handle_info, &handle.native);
    if (result != VK_SUCCESS) {
        dbgln("ExportableVulkanImage::export_handle: vkGetMemoryWin32HandleKHR returned {}", to_underlying(result));
        return Error::from_string_literal("ExportableVulkanImage: failed to export memory handle");
    }
#    endif

    return handle;
}

void ExportableVulkanImage::transition_layout(VkImageLayout old_layout, VkImageLayout new_layout)
{
    vkResetCommandBuffer(m_command_buffer, 0);
    VkCommandBufferBeginInfo begin_info = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .pNext = nullptr,
        .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
        .pInheritanceInfo = nullptr,
    };
    vkBeginCommandBuffer(m_command_buffer, &begin_info);

    VkImageMemoryBarrier barrier = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
        .pNext = nullptr,
        .srcAccessMask = 0,
        .dstAccessMask = 0,
        .oldLayout = old_layout,
        .newLayout = new_layout,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image = m_image,
        .subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 },
    };
    vkCmdPipelineBarrier(m_command_buffer,
        VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
        VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
        0, 0, nullptr, 0, nullptr, 1, &barrier);

    vkEndCommandBuffer(m_command_buffer);

    VkSubmitInfo submit = {
        .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
        .pNext = nullptr,
        .waitSemaphoreCount = 0,
        .pWaitSemaphores = nullptr,
        .pWaitDstStageMask = nullptr,
        .commandBufferCount = 1,
        .pCommandBuffers = &m_command_buffer,
        .signalSemaphoreCount = 0,
        .pSignalSemaphores = nullptr,
    };
    vkQueueSubmit(m_context.graphics_queue, 1, &submit, VK_NULL_HANDLE);
    vkQueueWaitIdle(m_context.graphics_queue);

    m_layout = new_layout;
}

// ── Import side ──────────────────────────────────────────────────────────────

ErrorOr<ImportedVulkanImage> import_external_vulkan_image(VkDevice device, ExternalMemoryHandle&& handle)
{
    if (!handle.is_valid())
        return Error::from_string_literal("import_external_vulkan_image: invalid handle");

    ImportedVulkanImage result;

    // Recreate a VkImage with parameters matching the exported image.
    VkExternalMemoryImageCreateInfo ext_image_info = {
        .sType = VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_IMAGE_CREATE_INFO,
        .pNext = nullptr,
        .handleTypes = static_cast<VkExternalMemoryHandleTypeFlags>(ExternalMemoryHandle::vulkan_handle_type()),
    };
    VkImageCreateInfo image_info = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .pNext = &ext_image_info,
        .flags = 0,
        .imageType = VK_IMAGE_TYPE_2D,
        .format = handle.format,
        .extent = { .width = handle.width, .height = handle.height, .depth = 1 },
        .mipLevels = 1,
        .arrayLayers = 1,
        .samples = VK_SAMPLE_COUNT_1_BIT,
        .tiling = VK_IMAGE_TILING_OPTIMAL,
        .usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
        .queueFamilyIndexCount = 0,
        .pQueueFamilyIndices = nullptr,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
    };
    if (vkCreateImage(device, &image_info, nullptr, &result.image) != VK_SUCCESS)
        return Error::from_string_literal("import_external_vulkan_image: vkCreateImage failed");

#    ifndef _WIN32
    VkImportMemoryFdInfoKHR import_info = {
        .sType = VK_STRUCTURE_TYPE_IMPORT_MEMORY_FD_INFO_KHR,
        .pNext = nullptr,
        .handleType = ExternalMemoryHandle::vulkan_handle_type(),
        .fd = handle.native,
    };
#    else
    VkImportMemoryWin32HandleInfoKHR import_info = {
        .sType = VK_STRUCTURE_TYPE_IMPORT_MEMORY_WIN32_HANDLE_INFO_KHR,
        .pNext = nullptr,
        .handleType = ExternalMemoryHandle::vulkan_handle_type(),
        .handle = handle.native,
        .name = nullptr,
    };
#    endif

    VkMemoryDedicatedAllocateInfo dedicated_info = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_DEDICATED_ALLOCATE_INFO,
        .pNext = &import_info,
        .image = result.image,
        .buffer = VK_NULL_HANDLE,
    };
    VkMemoryAllocateInfo alloc_info = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .pNext = &dedicated_info,
        .allocationSize = handle.allocation_size,
        .memoryTypeIndex = handle.memory_type_index,
    };

    VkResult vk_result = vkAllocateMemory(device, &alloc_info, nullptr, &result.memory);
    if (vk_result != VK_SUCCESS) {
        vkDestroyImage(device, result.image, nullptr);
        dbgln("import_external_vulkan_image: vkAllocateMemory returned {}", to_underlying(vk_result));
        return Error::from_string_literal("import_external_vulkan_image: vkAllocateMemory failed");
    }

    // On Linux, vkAllocateMemory consumes the fd on success — prevent double-close.
    // On Windows, the handle is duplicated internally so we leave it intact.
#    ifndef _WIN32
    handle.native = ExternalMemoryHandle::invalid_native;
#    endif

    vk_result = vkBindImageMemory(device, result.image, result.memory, 0);
    if (vk_result != VK_SUCCESS) {
        vkFreeMemory(device, result.memory, nullptr);
        vkDestroyImage(device, result.image, nullptr);
        dbgln("import_external_vulkan_image: vkBindImageMemory returned {}", to_underlying(vk_result));
        return Error::from_string_literal("import_external_vulkan_image: vkBindImageMemory failed");
    }

    return result;
}

} // namespace Gfx

#endif // USE_VULKAN
