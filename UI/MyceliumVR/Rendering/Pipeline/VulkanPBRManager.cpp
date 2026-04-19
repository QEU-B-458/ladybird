/*
 * MyceliumVR - a social VR platform built on the Ladybird browser engine
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "VulkanPBRManager.h"
#include <AK/ScopeGuard.h>

namespace MyceliumVR {

static ErrorOr<void> upload_1x1_texture(VkPhysicalDevice pd, VkDevice device, VmaAllocator allocator, VkCommandPool cp, VkQueue queue, u8 r, u8 g, u8 b, u8 a, VulkanTexture& out_texture)
{
    (void)pd;
    TRY(VulkanResourceManager::ensure_texture_resource(device, allocator, 1, 1, out_texture));
    VulkanBuffer staging;
    TRY(VulkanResourceManager::create_buffer(allocator, 4, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VMA_MEMORY_USAGE_CPU_ONLY, staging));
    ScopeGuard sg = [&] { staging.destroy(allocator); };
    void* data = nullptr;
    vmaMapMemory(allocator, staging.allocation, &data);
    u8 pixels[4] = { r, g, b, a };
    memcpy(data, pixels, 4);
    vmaUnmapMemory(allocator, staging.allocation);
    VkCommandBufferAllocateInfo ai {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .pNext = nullptr,
        .commandPool = cp,
        .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
        .commandBufferCount = 1
    };
    VkCommandBuffer cmd;
    TRY(check_vulkan_result(vkAllocateCommandBuffers(device, &ai, &cmd), "vkAllocateCommandBuffers failed"sv));
    VkCommandBufferBeginInfo bi { .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO, .pNext = nullptr, .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT, .pInheritanceInfo = nullptr };
    vkBeginCommandBuffer(cmd, &bi);
    VulkanResourceManager::transition_image_layout(cmd, out_texture.image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
    VulkanResourceManager::copy_buffer_to_image(cmd, staging.buffer, out_texture.image, 1, 1);
    VulkanResourceManager::transition_image_layout(cmd, out_texture.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    vkEndCommandBuffer(cmd);
    VkSubmitInfo si {
        .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
        .pNext = nullptr,
        .waitSemaphoreCount = 0,
        .pWaitSemaphores = nullptr,
        .pWaitDstStageMask = nullptr,
        .commandBufferCount = 1,
        .pCommandBuffers = &cmd,
        .signalSemaphoreCount = 0,
        .pSignalSemaphores = nullptr
    };
    vkQueueSubmit(queue, 1, &si, VK_NULL_HANDLE);
    vkQueueWaitIdle(queue);
    vkFreeCommandBuffers(device, cp, 1, &cmd);
    return {};
}

ErrorOr<void> VulkanPBRManager::ensure_fallback_textures(VkPhysicalDevice pd, VkDevice device, VmaAllocator allocator, VkCommandPool cp, VkQueue queue, VulkanTexture& flat_normal, VulkanTexture& fallback_black)
{
    if (flat_normal.image == VK_NULL_HANDLE) TRY(upload_1x1_texture(pd, device, allocator, cp, queue, 128, 128, 255, 255, flat_normal));
    if (fallback_black.image == VK_NULL_HANDLE) TRY(upload_1x1_texture(pd, device, allocator, cp, queue, 0, 0, 0, 255, fallback_black));
    return {};
}

ErrorOr<void> VulkanPBRManager::ensure_bindless_set(VkDevice device, BindlessSet& set)
{
    if (set.layout != VK_NULL_HANDLE) return {};

    VkDescriptorSetLayoutBinding binding {
        .binding = 0,
        .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
        .descriptorCount = set.capacity,
        .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
        .pImmutableSamplers = nullptr
    };

    VkDescriptorBindingFlags binding_flags = 
        VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT | 
        VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT |
        VK_DESCRIPTOR_BINDING_UPDATE_UNUSED_WHILE_PENDING_BIT;

    VkDescriptorSetLayoutBindingFlagsCreateInfo layout_flags {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_BINDING_FLAGS_CREATE_INFO,
        .pNext = nullptr,
        .bindingCount = 1,
        .pBindingFlags = &binding_flags
    };

    VkDescriptorSetLayoutCreateInfo layout_info {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
        .pNext = &layout_flags,
        .flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_UPDATE_AFTER_BIND_POOL_BIT,
        .bindingCount = 1,
        .pBindings = &binding
    };

    TRY(check_vulkan_result(vkCreateDescriptorSetLayout(device, &layout_info, nullptr, &set.layout), "vkCreateDescriptorSetLayout bindless failed"sv));

    VkDescriptorPoolSize ps { .type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, .descriptorCount = set.capacity };
    VkDescriptorPoolCreateInfo pi {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
        .pNext = nullptr,
        .flags = VK_DESCRIPTOR_POOL_CREATE_UPDATE_AFTER_BIND_BIT,
        .maxSets = 1,
        .poolSizeCount = 1,
        .pPoolSizes = &ps
    };
    TRY(check_vulkan_result(vkCreateDescriptorPool(device, &pi, nullptr, &set.pool), "vkCreateDescriptorPool bindless failed"sv));

    VkDescriptorSetAllocateInfo ai {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
        .pNext = nullptr,
        .descriptorPool = set.pool,
        .descriptorSetCount = 1,
        .pSetLayouts = &set.layout
    };
    TRY(check_vulkan_result(vkAllocateDescriptorSets(device, &ai, &set.set), "vkAllocateDescriptorSets bindless failed"sv));

    return {};
}

ErrorOr<void> VulkanPBRManager::update_material_descriptor_set(
    VkDevice device,
    TextureLibrary& texture_library,
    MaterialLibrary const& material_library,
    VirtualFileSystem const& vfs,
    Vector<DrawGroup> const& groups,
    VulkanTexture const& flat_normal,
    VulkanTexture const& fallback_black,
    BindlessSet& bindless_set,
    Vector<VkDescriptorSet>& out_sets)
{
    TRY(ensure_bindless_set(device, bindless_set));
    out_sets.clear();
    out_sets.append(bindless_set.set);

    auto register_texture = [&](MaterialTextureSlot const& slot) -> int32_t {
        if (!slot.is_set()) return -1;
        if (slot.cached_bindless_index.has_value())
            return static_cast<int32_t>(slot.cached_bindless_index.value());

        String path;
        if (!slot.path.is_empty()) path = slot.path;
        else path = slot.embedded_cache_key;

        if (auto it = bindless_set.texture_index_by_path.find(path); it != bindless_set.texture_index_by_path.end()) {
            slot.cached_bindless_index = it->value;
            return static_cast<int32_t>(it->value);
        }

        u32 index = static_cast<u32>(bindless_set.texture_paths.size());
        if (index >= bindless_set.capacity) {
            warnln("BindlessSet: capacity exceeded");
            return -1;
        }

        TextureLibrary::Entry const* entry = nullptr;
        if (!slot.path.is_empty()) entry = texture_library.resolve(vfs, slot.path);
        else entry = texture_library.resolve_from_bytes(path, slot.data.span());

        if (!entry) return -1;

        VkDescriptorImageInfo img_info { entry->sampler, entry->image_view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
        VkWriteDescriptorSet write {
            .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .pNext = nullptr,
            .dstSet = bindless_set.set,
            .dstBinding = 0,
            .dstArrayElement = index,
            .descriptorCount = 1,
            .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            .pImageInfo = &img_info,
            .pBufferInfo = nullptr,
            .pTexelBufferView = nullptr
        };
        vkUpdateDescriptorSets(device, 1, &write, 0, nullptr);

        slot.cached_bindless_index = index;
        bindless_set.texture_index_by_path.set(path, index);
        bindless_set.texture_paths.append(path);
        return static_cast<int32_t>(index);
    };

    if (bindless_set.texture_paths.is_empty()) {
        auto add_fallback = [&](VulkanTexture const& tex, StringView name) {
            u32 index = static_cast<u32>(bindless_set.texture_paths.size());
            VkDescriptorImageInfo img_info { tex.sampler, tex.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
            VkWriteDescriptorSet write {
                .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                .pNext = nullptr,
                .dstSet = bindless_set.set,
                .dstBinding = 0,
                .dstArrayElement = index,
                .descriptorCount = 1,
                .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                .pImageInfo = &img_info,
                .pBufferInfo = nullptr,
                .pTexelBufferView = nullptr
            };
            vkUpdateDescriptorSets(device, 1, &write, 0, nullptr);
            auto path = MUST(String::from_utf8(name));
            bindless_set.texture_index_by_path.set(path, index);
            bindless_set.texture_paths.append(path);
        };
        add_fallback(fallback_black, "__fallback_black"sv);
        add_fallback(flat_normal, "__flat_normal"sv);
    }

    HashTable<String> processed_materials;
    for (auto const& group : groups) {
        if (processed_materials.contains(group.material))
            continue;
        processed_materials.set(group.material);

        auto const* asset = material_library.resolve(group.material);
        if (asset) {
            register_texture(asset->albedo);
            register_texture(asset->normal);
            register_texture(asset->metallic_roughness);
            register_texture(asset->emissive);
            register_texture(asset->occlusion);
        }
    }

    return {};
}

}
