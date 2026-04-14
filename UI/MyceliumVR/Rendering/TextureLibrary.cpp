/*
 * MyceliumVR - a social VR platform built on the Ladybird browser engine
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "TextureLibrary.h"

#include "../Support/VirtualFileSystem.h"

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

static u32 find_memory_type(VkPhysicalDevice physical_device, u32 type_filter, VkMemoryPropertyFlags properties)
{
    VkPhysicalDeviceMemoryProperties mem_properties {};
    vkGetPhysicalDeviceMemoryProperties(physical_device, &mem_properties);
    for (u32 i = 0; i < mem_properties.memoryTypeCount; ++i) {
        if ((type_filter & (1u << i)) && (mem_properties.memoryTypes[i].propertyFlags & properties) == properties)
            return i;
    }
    return UINT32_MAX;
}

TextureLibrary::TextureLibrary(VkPhysicalDevice physical_device, VkDevice device, VkCommandPool command_pool, VkQueue queue, VkDescriptorSetLayout descriptor_set_layout)
    : m_physical_device(physical_device)
    , m_device(device)
    , m_command_pool(command_pool)
    , m_queue(queue)
    , m_descriptor_set_layout(descriptor_set_layout)
{
}

TextureLibrary::Entry const* TextureLibrary::resolve(VirtualFileSystem const& vfs, StringView virtual_path)
{
    if (!is_image_path(virtual_path))
        return nullptr;

    auto key = MUST(String::from_utf8(virtual_path));
    if (auto it = m_cache.find(key); it != m_cache.end())
        return &it->value;

    auto result = load_and_upload(vfs, virtual_path);
    if (result.is_error()) {
        warnln("TextureLibrary: failed to load '{}': {}", virtual_path, result.error());
        return nullptr;
    }

    m_cache.set(key, result.release_value());
    return &m_cache.find(key)->value;
}

TextureLibrary::Entry const* TextureLibrary::resolve_from_bytes(StringView cache_key, ReadonlyBytes bytes)
{
    auto key = MUST(String::from_utf8(cache_key));
    if (auto it = m_cache.find(key); it != m_cache.end())
        return &it->value;

    auto result = decode_and_upload(bytes);
    if (result.is_error()) {
        warnln("TextureLibrary: failed to upload embedded texture '{}': {}", cache_key, result.error());
        return nullptr;
    }

    m_cache.set(key, result.release_value());
    return &m_cache.find(key)->value;
}

ErrorOr<TextureLibrary::Entry> TextureLibrary::load_and_upload(VirtualFileSystem const& vfs, StringView virtual_path)
{
    auto bytes = TRY(vfs.read_file(virtual_path));
    return decode_and_upload(bytes);
}

ErrorOr<TextureLibrary::Entry> TextureLibrary::decode_and_upload(ReadonlyBytes bytes)
{
    auto decoder = TRY(Gfx::ImageDecoder::try_create_for_raw_bytes(bytes));
    if (!decoder)
        return Error::from_string_literal("No image decoder available for this format");

    auto frame = TRY(decoder->frame(0));
    auto& bitmap = *frame.image;

    // Ensure BGRA8888 → convert to RGBA8888 for Vulkan.
    auto width = static_cast<u32>(bitmap.width());
    auto height = static_cast<u32>(bitmap.height());
    auto pixel_count = static_cast<VkDeviceSize>(width) * height * 4;

    // Build an RGBA byte buffer from the bitmap.
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

    // Staging buffer.
    VkBuffer staging { VK_NULL_HANDLE };
    VkDeviceMemory staging_mem { VK_NULL_HANDLE };
    {
        VkBufferCreateInfo buf_info {};
        buf_info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        buf_info.size = pixel_count;
        buf_info.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
        buf_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        if (vkCreateBuffer(m_device, &buf_info, nullptr, &staging) != VK_SUCCESS)
            return Error::from_string_literal("vkCreateBuffer for texture staging failed");

        VkMemoryRequirements mem_req {};
        vkGetBufferMemoryRequirements(m_device, staging, &mem_req);
        VkMemoryAllocateInfo alloc_info {};
        alloc_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        alloc_info.allocationSize = mem_req.size;
        alloc_info.memoryTypeIndex = find_memory_type(m_physical_device, mem_req.memoryTypeBits,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        if (vkAllocateMemory(m_device, &alloc_info, nullptr, &staging_mem) != VK_SUCCESS) {
            vkDestroyBuffer(m_device, staging, nullptr);
            return Error::from_string_literal("vkAllocateMemory for texture staging failed");
        }
        vkBindBufferMemory(m_device, staging, staging_mem, 0);

        void* mapped = nullptr;
        vkMapMemory(m_device, staging_mem, 0, pixel_count, 0, &mapped);
        __builtin_memcpy(mapped, rgba_pixels.data(), static_cast<size_t>(pixel_count));
        vkUnmapMemory(m_device, staging_mem);
    }

    // Device-local image.
    VkImage image { VK_NULL_HANDLE };
    VkDeviceMemory image_mem { VK_NULL_HANDLE };
    {
        VkImageCreateInfo img_info {};
        img_info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        img_info.imageType = VK_IMAGE_TYPE_2D;
        img_info.extent = { width, height, 1 };
        img_info.mipLevels = 1;
        img_info.arrayLayers = 1;
        img_info.format = VK_FORMAT_R8G8B8A8_UNORM;
        img_info.tiling = VK_IMAGE_TILING_OPTIMAL;
        img_info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        img_info.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
        img_info.samples = VK_SAMPLE_COUNT_1_BIT;
        img_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        if (vkCreateImage(m_device, &img_info, nullptr, &image) != VK_SUCCESS) {
            vkDestroyBuffer(m_device, staging, nullptr);
            vkFreeMemory(m_device, staging_mem, nullptr);
            return Error::from_string_literal("vkCreateImage for texture failed");
        }

        VkMemoryRequirements mem_req {};
        vkGetImageMemoryRequirements(m_device, image, &mem_req);
        VkMemoryAllocateInfo alloc_info {};
        alloc_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        alloc_info.allocationSize = mem_req.size;
        alloc_info.memoryTypeIndex = find_memory_type(m_physical_device, mem_req.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        if (vkAllocateMemory(m_device, &alloc_info, nullptr, &image_mem) != VK_SUCCESS) {
            vkDestroyImage(m_device, image, nullptr);
            vkDestroyBuffer(m_device, staging, nullptr);
            vkFreeMemory(m_device, staging_mem, nullptr);
            return Error::from_string_literal("vkAllocateMemory for texture image failed");
        }
        vkBindImageMemory(m_device, image, image_mem, 0);
    }

    // One-shot transfer command.
    {
        VkCommandBufferAllocateInfo ai {};
        ai.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        ai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        ai.commandPool = m_command_pool;
        ai.commandBufferCount = 1;
        VkCommandBuffer cmd { VK_NULL_HANDLE };
        vkAllocateCommandBuffers(m_device, &ai, &cmd);

        VkCommandBufferBeginInfo bi {};
        bi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        vkBeginCommandBuffer(cmd, &bi);

        // Undefined → transfer dst.
        VkImageMemoryBarrier barrier {};
        barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.image = image;
        barrier.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
        barrier.srcAccessMask = 0;
        barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);

        VkBufferImageCopy region {};
        region.imageSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
        region.imageExtent = { width, height, 1 };
        vkCmdCopyBufferToImage(cmd, staging, image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

        // Transfer dst → shader read only.
        barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);

        vkEndCommandBuffer(cmd);
        VkSubmitInfo si {};
        si.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        si.commandBufferCount = 1;
        si.pCommandBuffers = &cmd;
        vkQueueSubmit(m_queue, 1, &si, VK_NULL_HANDLE);
        vkQueueWaitIdle(m_queue);
        vkFreeCommandBuffers(m_device, m_command_pool, 1, &cmd);
    }

    vkDestroyBuffer(m_device, staging, nullptr);
    vkFreeMemory(m_device, staging_mem, nullptr);

    // Image view.
    VkImageView image_view { VK_NULL_HANDLE };
    {
        VkImageViewCreateInfo view_info {};
        view_info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        view_info.image = image;
        view_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
        view_info.format = VK_FORMAT_R8G8B8A8_UNORM;
        view_info.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
        if (vkCreateImageView(m_device, &view_info, nullptr, &image_view) != VK_SUCCESS) {
            vkDestroyImage(m_device, image, nullptr);
            vkFreeMemory(m_device, image_mem, nullptr);
            return Error::from_string_literal("vkCreateImageView for texture failed");
        }
    }

    // Sampler.
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
            vkDestroyImage(m_device, image, nullptr);
            vkFreeMemory(m_device, image_mem, nullptr);
            return Error::from_string_literal("vkCreateSampler for texture failed");
        }
    }

    // Descriptor pool + set.
    VkDescriptorPool pool { VK_NULL_HANDLE };
    VkDescriptorSet set { VK_NULL_HANDLE };
    {
        VkDescriptorPoolSize pool_size { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1 };
        VkDescriptorPoolCreateInfo pool_info {};
        pool_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        pool_info.poolSizeCount = 1;
        pool_info.pPoolSizes = &pool_size;
        pool_info.maxSets = 1;
        if (vkCreateDescriptorPool(m_device, &pool_info, nullptr, &pool) != VK_SUCCESS) {
            vkDestroySampler(m_device, sampler, nullptr);
            vkDestroyImageView(m_device, image_view, nullptr);
            vkDestroyImage(m_device, image, nullptr);
            vkFreeMemory(m_device, image_mem, nullptr);
            return Error::from_string_literal("vkCreateDescriptorPool for texture failed");
        }

        VkDescriptorSetAllocateInfo alloc_info {};
        alloc_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        alloc_info.descriptorPool = pool;
        alloc_info.descriptorSetCount = 1;
        alloc_info.pSetLayouts = &m_descriptor_set_layout;
        vkAllocateDescriptorSets(m_device, &alloc_info, &set);

        VkDescriptorImageInfo img_info {};
        img_info.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        img_info.imageView = image_view;
        img_info.sampler = sampler;
        VkWriteDescriptorSet write {};
        write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        write.dstSet = set;
        write.dstBinding = 0;
        write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        write.descriptorCount = 1;
        write.pImageInfo = &img_info;
        vkUpdateDescriptorSets(m_device, 1, &write, 0, nullptr);
    }

    return Entry { image, image_mem, image_view, sampler, pool, set, width, height };
}

void TextureLibrary::destroy()
{
    for (auto& [key, entry] : m_cache) {
        if (entry.descriptor_pool != VK_NULL_HANDLE)
            vkDestroyDescriptorPool(m_device, entry.descriptor_pool, nullptr);
        if (entry.sampler != VK_NULL_HANDLE)
            vkDestroySampler(m_device, entry.sampler, nullptr);
        if (entry.image_view != VK_NULL_HANDLE)
            vkDestroyImageView(m_device, entry.image_view, nullptr);
        if (entry.image != VK_NULL_HANDLE)
            vkDestroyImage(m_device, entry.image, nullptr);
        if (entry.memory != VK_NULL_HANDLE)
            vkFreeMemory(m_device, entry.memory, nullptr);
    }
    m_cache.clear();
}

#endif // USE_VULKAN

}
