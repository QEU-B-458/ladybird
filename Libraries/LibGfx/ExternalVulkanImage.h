/*
 * Copyright (c) 2024, MyceliumVR Contributors
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#ifdef USE_VULKAN

#    include <AK/Error.h>
#    include <AK/NonnullRefPtr.h>
#    include <AK/RefCounted.h>
#    include <LibGfx/VulkanContext.h>
#    include <vulkan/vulkan.h>

#    ifdef _WIN32
#        include <vulkan/vulkan_win32.h>
#    endif

namespace Gfx {

// Cross-platform RAII wrapper around an exported GPU memory handle plus the
// metadata needed to recreate a matching VkImage on the importer side.
//
// Linux: wraps an opaque fd (VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD_BIT).
// Windows: wraps a HANDLE (VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_WIN32_BIT).
//
// The handle owns its native resource and closes it on destruction.
struct ExternalMemoryHandle {
#    ifdef _WIN32
    using NativeHandle = HANDLE;
    static constexpr NativeHandle invalid_native = nullptr;
#    else
    using NativeHandle = int;
    static constexpr NativeHandle invalid_native = -1;
#    endif

    NativeHandle native { invalid_native };

    // Metadata the importer needs to reconstruct the VkImage.
    VkDeviceSize allocation_size { 0 };
    uint32_t memory_type_index { 0 };
    VkFormat format { VK_FORMAT_UNDEFINED };
    uint32_t width { 0 };
    uint32_t height { 0 };

    ExternalMemoryHandle() = default;
    ~ExternalMemoryHandle();
    ExternalMemoryHandle(ExternalMemoryHandle const&) = delete;
    ExternalMemoryHandle& operator=(ExternalMemoryHandle const&) = delete;
    ExternalMemoryHandle(ExternalMemoryHandle&&) noexcept;
    ExternalMemoryHandle& operator=(ExternalMemoryHandle&&) noexcept;

    bool is_valid() const { return native != invalid_native; }

    // Releases ownership and returns the raw native handle (caller takes ownership).
    NativeHandle release() noexcept
    {
        auto h = native;
        native = invalid_native;
        return h;
    }

    // Returns the Vulkan handle type constant for the current platform.
    static VkExternalMemoryHandleTypeFlagBits vulkan_handle_type();
};

// A VkImage allocated with external-memory export enabled.
//
// This is the export side of the zero-copy WebContent → MyceliumVR pipeline.
// BackingStoreManager creates one of these and gives Skia a surface backed by
// the VkImage. After each paint, export_handle() sends the fd to MyceliumVR
// via IPC; MyceliumVR imports it with import_external_vulkan_image() and
// samples it directly — no CPU copies involved.
//
// Uses OPAQUE_FD / OPAQUE_WIN32 (same physical device, same driver) rather
// than DMA_BUF (which requires DRM format modifier negotiation with the
// display server and is already handled by the separate VulkanImage).
class ExportableVulkanImage : public RefCounted<ExportableVulkanImage> {
public:
    static ErrorOr<NonnullRefPtr<ExportableVulkanImage>> create(
        VulkanContext const&,
        uint32_t width,
        uint32_t height,
        VkFormat format = VK_FORMAT_B8G8R8A8_UNORM);

    ~ExportableVulkanImage();

    VkImage image() const { return m_image; }
    VkFormat format() const { return m_format; }
    uint32_t width() const { return m_width; }
    uint32_t height() const { return m_height; }
    VkImageLayout layout() const { return m_layout; }

    // Export the underlying device memory as a platform handle.
    // Each call produces a new independent handle pointing to the same allocation.
    ErrorOr<ExternalMemoryHandle> export_handle() const;

    // Submit a one-time layout transition on the graphics queue and wait idle.
    void transition_layout(VkImageLayout old_layout, VkImageLayout new_layout);

private:
    explicit ExportableVulkanImage(VulkanContext const&);

    VulkanContext const& m_context;
    VkCommandPool m_command_pool { VK_NULL_HANDLE };
    VkCommandBuffer m_command_buffer { VK_NULL_HANDLE };

    VkImage m_image { VK_NULL_HANDLE };
    VkDeviceMemory m_memory { VK_NULL_HANDLE };
    VkDeviceSize m_allocation_size { 0 };
    uint32_t m_memory_type_index { 0 };
    VkFormat m_format { VK_FORMAT_UNDEFINED };
    uint32_t m_width { 0 };
    uint32_t m_height { 0 };
    VkImageLayout m_layout { VK_IMAGE_LAYOUT_UNDEFINED };

#    ifndef _WIN32
    PFN_vkGetMemoryFdKHR m_pfn_get_memory_fd { nullptr };
#    else
    PFN_vkGetMemoryWin32HandleKHR m_pfn_get_memory_handle { nullptr };
#    endif
};

// Import side: reconstruct a VkImage + VkDeviceMemory in the calling process
// from an ExternalMemoryHandle obtained via IPC.
//
// Takes the handle by move — on Linux the fd is consumed by vkAllocateMemory
// on success, so the handle is always invalidated after a successful import.
// On failure the handle is left intact so the caller can inspect or retry.
//
// The caller is responsible for vkDestroyImage / vkFreeMemory when done.
// The imported image starts in VK_IMAGE_LAYOUT_UNDEFINED — insert a pipeline
// barrier before sampling (or transition to VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL).
struct ImportedVulkanImage {
    VkImage image { VK_NULL_HANDLE };
    VkDeviceMemory memory { VK_NULL_HANDLE };
};
ErrorOr<ImportedVulkanImage> import_external_vulkan_image(VkDevice, ExternalMemoryHandle&&);

} // namespace Gfx

#endif // USE_VULKAN
