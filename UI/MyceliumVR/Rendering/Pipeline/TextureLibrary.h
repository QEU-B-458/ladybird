/*
 * MyceliumVR - a social VR platform built on the Ladybird browser engine
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <AK/Error.h>
#include <AK/HashMap.h>
#include <AK/NonnullOwnPtr.h>
#include <AK/Optional.h>
#include <AK/Span.h>
#include <AK/String.h>

#if defined(USE_VULKAN)
#    include <vulkan/vulkan.h>
#    include <vk_mem_alloc.h>
#endif

namespace MyceliumVR {

class VirtualFileSystem;

// Loads image files from the VFS and uploads them to device-local Vulkan textures.
// Textures are cached by virtual path and reused across entities.
// The renderer is responsible for calling destroy() before tearing down the Vulkan device.
class TextureLibrary {
public:
    struct Entry {
#if defined(USE_VULKAN)
        VkImage image { VK_NULL_HANDLE };
        VmaAllocation allocation { VK_NULL_HANDLE };
        VkImageView image_view { VK_NULL_HANDLE };
        VkSampler sampler { VK_NULL_HANDLE };
#endif
        u32 width { 0 };
        u32 height { 0 };
    };

#if defined(USE_VULKAN)
    TextureLibrary(VkPhysicalDevice, VkDevice, u32 graphics_queue_family, VkQueue, VmaAllocator);
    ~TextureLibrary();

    // Resolve a virtual path to a cached texture entry.
    // Returns nullptr if the path is not an image or load fails; caller should fall back to default.
    Entry const* resolve(VirtualFileSystem const&, StringView virtual_path);

    // Resolve raw PNG/JPEG bytes (e.g. embedded GLB textures) to a cached texture entry.
    // cache_key must be unique and stable for the lifetime of the bytes (caller's responsibility).
    // Returns nullptr if decoding or upload fails; caller should fall back to default.
    Entry const* resolve_from_bytes(StringView cache_key, ReadonlyBytes bytes);

    // Destroy all cached GPU resources. Must be called before the Vulkan device is destroyed.
    void destroy();

private:
    ErrorOr<Entry> load_and_upload(VirtualFileSystem const&, StringView virtual_path);
    ErrorOr<Entry> decode_and_upload(ReadonlyBytes bytes);

    VkPhysicalDevice m_physical_device { VK_NULL_HANDLE };
    VkDevice m_device { VK_NULL_HANDLE };
    VkQueue m_queue { VK_NULL_HANDLE };
    VmaAllocator m_allocator { VK_NULL_HANDLE };
    VkCommandPool m_transient_pool { VK_NULL_HANDLE };
    HashMap<String, NonnullOwnPtr<Entry>> m_cache;
#endif
};

}
