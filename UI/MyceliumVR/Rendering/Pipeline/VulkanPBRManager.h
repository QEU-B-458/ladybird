/*
 * MyceliumVR - a social VR platform built on the Ladybird browser engine
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include "../VulkanCommon.h"
#include "../Backend/VulkanResourceManager.h"
#include "VulkanPipeline.h"
#include "VulkanMeshBuilder.h"
#include "../../Support/MaterialLibrary.h"
#include "TextureLibrary.h"

namespace MyceliumVR {

#if defined(USE_VULKAN)

class VulkanPBRManager {
public:
    static ErrorOr<void> ensure_fallback_textures(VkPhysicalDevice, VkDevice, VmaAllocator, VkCommandPool, VkQueue, VulkanTexture& flat_normal, VulkanTexture& fallback_black);

    // Bindless texture set (set 0) — contains one large array of all unique textures.
    // Descriptor pool and set are allocated once and updated as textures are loaded.
    struct BindlessSet {
        VkDescriptorPool pool { VK_NULL_HANDLE };
        VkDescriptorSet set { VK_NULL_HANDLE };
        VkDescriptorSetLayout layout { VK_NULL_HANDLE };
        u32 capacity { 1024 };
        HashMap<String, u32> texture_index_by_path;
        Vector<String> texture_paths;

        void destroy(VkDevice device) {
            if (pool != VK_NULL_HANDLE) vkDestroyDescriptorPool(device, pool, nullptr);
            if (layout != VK_NULL_HANDLE) vkDestroyDescriptorSetLayout(device, layout, nullptr);
            pool = VK_NULL_HANDLE;
            set = VK_NULL_HANDLE;
            layout = VK_NULL_HANDLE;
            texture_index_by_path.clear();
            texture_paths.clear();
        }
    };

    static ErrorOr<void> ensure_bindless_set(VkDevice, BindlessSet&);

    static ErrorOr<void> update_material_descriptor_set(
        VkDevice device,
        TextureLibrary& texture_library,
        MaterialLibrary const& material_library,
        VirtualFileSystem const& vfs,
        Vector<DrawGroup> const& groups,
        VulkanTexture const& flat_normal,
        VulkanTexture const& fallback_black,
        BindlessSet& bindless_set,
        Vector<VkDescriptorSet>& out_sets);
};

#endif

}
