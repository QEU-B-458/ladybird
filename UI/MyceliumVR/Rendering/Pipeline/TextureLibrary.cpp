/*
 * MyceliumVR - a social VR platform built on the Ladybird browser engine
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "TextureLibrary.h"
#include "../Backend/VulkanResourceManager.h"

#include <UI/MyceliumVR/Support/Profiling.h>

#include "../../Support/VirtualFileSystem.h"
#include <AK/ByteBuffer.h>
#include <LibGfx/Bitmap.h>
#include <LibGfx/ImageFormats/ImageDecoder.h>

#if defined(USE_VULKAN)
#    include <vulkan/vulkan.h>
#endif

namespace MyceliumVR {

#if defined(USE_VULKAN)

static bool is_image_path(StringView path)
{
    return path.ends_with(".png"sv) || path.ends_with(".jpg"sv)
        || path.ends_with(".jpeg"sv) || path.ends_with(".bmp"sv)
        || path.ends_with(".gif"sv) || path.ends_with(".webp"sv);
}

TextureLibrary::TextureLibrary(VkPhysicalDevice physical_device, VkDevice device, u32 graphics_queue_family, VkQueue queue, VmaAllocator allocator)
    : m_physical_device(physical_device)
    , m_device(device)
    , m_queue(queue)
    , m_allocator(allocator)
{
    VkCommandPoolCreateInfo pool_info { VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO, nullptr, VK_COMMAND_POOL_CREATE_TRANSIENT_BIT, graphics_queue_family };
    vkCreateCommandPool(m_device, &pool_info, nullptr, &m_transient_pool);
}

TextureLibrary::~TextureLibrary()
{
    destroy();
}

TextureLibrary::Entry const* TextureLibrary::resolve(VirtualFileSystem const& vfs, StringView virtual_path)
{
#if defined(TRACY_ENABLE)
    ZoneScopedN("Asset/TextureResolve");
    TracyPlot("TextureLibrary/CacheEntries", static_cast<int64_t>(m_cache.size()));
#endif
    if (!is_image_path(virtual_path))
        return nullptr;

    auto key = MUST(String::from_utf8(virtual_path));
    if (auto it = m_cache.find(key); it != m_cache.end())
        return it->value.ptr();

    auto result = load_and_upload(vfs, virtual_path);
    if (result.is_error()) {
        warnln("TextureLibrary: failed to load '{}': {}", virtual_path, result.error());
        return nullptr;
    }

    auto entry_ptr_res = adopt_nonnull_own_or_enomem(new (nothrow) Entry(result.release_value()));
    if (entry_ptr_res.is_error()) {
        warnln("TextureLibrary: out of memory for '{}'", virtual_path);
        return nullptr;
    }
    m_cache.set(key, entry_ptr_res.release_value());
#if defined(TRACY_ENABLE)
    TracyPlot("TextureLibrary/CacheEntries", static_cast<int64_t>(m_cache.size()));
#endif
    return m_cache.find(key)->value.ptr();
}

TextureLibrary::Entry const* TextureLibrary::resolve_from_bytes(StringView cache_key, ReadonlyBytes bytes)
{
#if defined(TRACY_ENABLE)
    ZoneScopedN("Asset/TextureResolveEmbedded");
    TracyPlot("TextureLibrary/CacheEntries", static_cast<int64_t>(m_cache.size()));
    TracyPlot("TextureLibrary/EmbeddedSourceBytes", static_cast<int64_t>(bytes.size()));
#endif
    auto key = MUST(String::from_utf8(cache_key));
    if (auto it = m_cache.find(key); it != m_cache.end())
        return it->value.ptr();

    auto result = decode_and_upload(bytes);
    if (result.is_error()) {
        warnln("TextureLibrary: failed to upload embedded texture '{}': {}", cache_key, result.error());
        return nullptr;
    }

    auto entry_ptr_res = adopt_nonnull_own_or_enomem(new (nothrow) Entry(result.release_value()));
    if (entry_ptr_res.is_error()) {
        warnln("TextureLibrary: out of memory for '{}'", cache_key);
        return nullptr;
    }
    m_cache.set(key, entry_ptr_res.release_value());
#if defined(TRACY_ENABLE)
    TracyPlot("TextureLibrary/CacheEntries", static_cast<int64_t>(m_cache.size()));
#endif
    return m_cache.find(key)->value.ptr();
}

ErrorOr<TextureLibrary::Entry> TextureLibrary::load_and_upload(VirtualFileSystem const& vfs, StringView virtual_path)
{
#if defined(TRACY_ENABLE)
    ZoneScopedN("Asset/TextureLoad");
    ZoneText(virtual_path.characters_without_null_termination(), virtual_path.length());
#endif
    auto bytes = TRY(vfs.read_file(virtual_path));
    return decode_and_upload(bytes);
}

ErrorOr<TextureLibrary::Entry> TextureLibrary::decode_and_upload(ReadonlyBytes bytes)
{
#if defined(TRACY_ENABLE)
    ZoneScopedN("Asset/TextureDecode");
    TracyPlot("TextureLibrary/DecodeBytes", static_cast<int64_t>(bytes.size()));
#endif
    auto decoder = TRY(Gfx::ImageDecoder::try_create_for_raw_bytes(bytes));
    if (!decoder)
        return Error::from_string_literal("No image decoder available for this format");

    auto frame = TRY(decoder->frame(0));
    auto& bitmap = *frame.image;

    auto width = static_cast<u32>(bitmap.width());
    auto height = static_cast<u32>(bitmap.height());
    auto pixel_count = static_cast<VkDeviceSize>(width) * height * 4;
#if defined(TRACY_ENABLE)
    TracyPlot("TextureLibrary/DecodedWidth", static_cast<int64_t>(width));
    TracyPlot("TextureLibrary/DecodedHeight", static_cast<int64_t>(height));
    TracyPlot("TextureLibrary/DecodedRGBABytes", static_cast<int64_t>(pixel_count));
#endif

    Vector<u8> rgba_pixels;
    TRY(rgba_pixels.try_resize(pixel_count));
    for (u32 y = 0; y < height; ++y) {
        for (u32 x = 0; x < width; ++x) {
            auto color = bitmap.get_pixel(static_cast<int>(x), static_cast<int>(y));
            auto i = (y * width + x) * 4;
            rgba_pixels[i + 0] = color.red();
            rgba_pixels[i + 1] = color.green();
            rgba_pixels[i + 2] = color.blue();
            rgba_pixels[i + 3] = color.alpha();
        }
    }

    VulkanBuffer staging;
    TRY(VulkanResourceManager::create_buffer(m_allocator, pixel_count, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VMA_MEMORY_USAGE_CPU_ONLY, staging));
    {
        void* mapped = nullptr;
        vmaMapMemory(m_allocator, staging.allocation, &mapped);
        __builtin_memcpy(mapped, rgba_pixels.data(), static_cast<size_t>(pixel_count));
        vmaUnmapMemory(m_allocator, staging.allocation);
    }

    VkImage image { VK_NULL_HANDLE };
    VmaAllocation image_alloc { VK_NULL_HANDLE };
    TRY(VulkanResourceManager::create_image(m_allocator, width, height, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_TILING_OPTIMAL, VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, VMA_MEMORY_USAGE_GPU_ONLY, image, image_alloc));

    {
#if defined(TRACY_ENABLE)
        ZoneScopedN("Asset/TextureUploadGPU");
#endif
        VkCommandBufferAllocateInfo ai { VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO, nullptr, m_transient_pool, VK_COMMAND_BUFFER_LEVEL_PRIMARY, 1 };
        VkCommandBuffer cmd { VK_NULL_HANDLE };
        vkAllocateCommandBuffers(m_device, &ai, &cmd);

        VkCommandBufferBeginInfo bi { VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO, nullptr, VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT, nullptr };
        vkBeginCommandBuffer(cmd, &bi);

        VulkanResourceManager::transition_image_layout(cmd, image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
        VulkanResourceManager::copy_buffer_to_image(cmd, staging.buffer, image, width, height);
        VulkanResourceManager::transition_image_layout(cmd, image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

        vkEndCommandBuffer(cmd);
        VkSubmitInfo si { VK_STRUCTURE_TYPE_SUBMIT_INFO, nullptr, 0, nullptr, nullptr, 1, &cmd, 0, nullptr };
        vkQueueSubmit(m_queue, 1, &si, VK_NULL_HANDLE);
        vkQueueWaitIdle(m_queue);
        vkFreeCommandBuffers(m_device, m_transient_pool, 1, &cmd);
    }

    staging.destroy(m_allocator);

    VkImageView image_view { VK_NULL_HANDLE };
    {
        VkImageViewCreateInfo view_info {};
        view_info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        view_info.image = image;
        view_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
        view_info.format = VK_FORMAT_R8G8B8A8_UNORM;
        view_info.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
        if (vkCreateImageView(m_device, &view_info, nullptr, &image_view) != VK_SUCCESS) {
            vmaDestroyImage(m_allocator, image, image_alloc);
            return Error::from_string_literal("vkCreateImageView for texture failed");
        }
    }

    VkSampler sampler { VK_NULL_HANDLE };
    {
        VkSamplerCreateInfo samp_info {};
        samp_info.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
        samp_info.magFilter = VK_FILTER_LINEAR;
        samp_info.minFilter = VK_FILTER_LINEAR;
        samp_info.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
        samp_info.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
        samp_info.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
        samp_info.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
        samp_info.maxLod = 1.0f;
        if (vkCreateSampler(m_device, &samp_info, nullptr, &sampler) != VK_SUCCESS) {
            vkDestroyImageView(m_device, image_view, nullptr);
            vmaDestroyImage(m_allocator, image, image_alloc);
            return Error::from_string_literal("vkCreateSampler for texture failed");
        }
    }

    return Entry { image, image_alloc, image_view, sampler, width, height };
}

void TextureLibrary::destroy()
{
#if defined(TRACY_ENABLE)
    ZoneScopedN("Asset/TextureDestroyAll");
    TracyPlot("TextureLibrary/CacheEntries", static_cast<int64_t>(m_cache.size()));
#endif
    for (auto& [key, entry] : m_cache) {
        if (entry->sampler != VK_NULL_HANDLE)
            vkDestroySampler(m_device, entry->sampler, nullptr);
        if (entry->image_view != VK_NULL_HANDLE)
            vkDestroyImageView(m_device, entry->image_view, nullptr);
        if (entry->image != VK_NULL_HANDLE)
            vmaDestroyImage(m_allocator, entry->image, entry->allocation);
        
        entry->sampler = VK_NULL_HANDLE;
        entry->image_view = VK_NULL_HANDLE;
        entry->image = VK_NULL_HANDLE;
        entry->allocation = VK_NULL_HANDLE;
    }
    m_cache.clear();
#if defined(TRACY_ENABLE)
    TracyPlot("TextureLibrary/CacheEntries", static_cast<int64_t>(0));
#endif

    if (m_transient_pool != VK_NULL_HANDLE) {
        vkDestroyCommandPool(m_device, m_transient_pool, nullptr);
        m_transient_pool = VK_NULL_HANDLE;
    }
}

#endif // USE_VULKAN

}
