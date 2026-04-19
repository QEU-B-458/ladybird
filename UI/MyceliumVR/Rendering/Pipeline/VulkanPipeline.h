/*
 * MyceliumVR - a social VR platform built on the Ladybird browser engine
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include "../VulkanCommon.h"
#include "../../Support/ShaderCompiler.h"
#include <AK/Optional.h>
#include <AK/Vector.h>

namespace MyceliumVR {

struct VulkanWorldPipeline {
    enum class CullMode : u8 {
        Back,
        Front,
        Disabled,
    };

    enum class AlphaMode : u8 {
        Opaque,
        Clip,
        Blend,
        Hash,
    };

    VkDescriptorSetLayout pbr_layout { VK_NULL_HANDLE };
    VkDescriptorSetLayout light_layout { VK_NULL_HANDLE };
    VkDescriptorSetLayout material_layout { VK_NULL_HANDLE };
    VkDescriptorSetLayout shadow_layout { VK_NULL_HANDLE };
    VkDescriptorSetLayout cluster_layout { VK_NULL_HANDLE };
    VkPipelineLayout layout { VK_NULL_HANDLE };
    // VARIANT_HAS_NORMAL_MAP = true
    VkPipeline opaque_backface_pipeline { VK_NULL_HANDLE };
    VkPipeline opaque_frontface_pipeline { VK_NULL_HANDLE };
    VkPipeline opaque_no_cull_pipeline { VK_NULL_HANDLE };
    VkPipeline clip_backface_pipeline { VK_NULL_HANDLE };
    VkPipeline clip_frontface_pipeline { VK_NULL_HANDLE };
    VkPipeline clip_no_cull_pipeline { VK_NULL_HANDLE };
    VkPipeline hash_backface_pipeline { VK_NULL_HANDLE };
    VkPipeline hash_frontface_pipeline { VK_NULL_HANDLE };
    VkPipeline hash_no_cull_pipeline { VK_NULL_HANDLE };
    VkPipeline blend_backface_pipeline { VK_NULL_HANDLE };
    VkPipeline blend_frontface_pipeline { VK_NULL_HANDLE };
    VkPipeline blend_no_cull_pipeline { VK_NULL_HANDLE };
    // VARIANT_HAS_NORMAL_MAP = false
    VkPipeline opaque_backface_no_nmap_pipeline { VK_NULL_HANDLE };
    VkPipeline opaque_frontface_no_nmap_pipeline { VK_NULL_HANDLE };
    VkPipeline opaque_no_cull_no_nmap_pipeline { VK_NULL_HANDLE };
    VkPipeline clip_backface_no_nmap_pipeline { VK_NULL_HANDLE };
    VkPipeline clip_frontface_no_nmap_pipeline { VK_NULL_HANDLE };
    VkPipeline clip_no_cull_no_nmap_pipeline { VK_NULL_HANDLE };
    VkPipeline hash_backface_no_nmap_pipeline { VK_NULL_HANDLE };
    VkPipeline hash_frontface_no_nmap_pipeline { VK_NULL_HANDLE };
    VkPipeline hash_no_cull_no_nmap_pipeline { VK_NULL_HANDLE };
    VkPipeline blend_backface_no_nmap_pipeline { VK_NULL_HANDLE };
    VkPipeline blend_frontface_no_nmap_pipeline { VK_NULL_HANDLE };
    VkPipeline blend_no_cull_no_nmap_pipeline { VK_NULL_HANDLE };
    // Depth-read-only variants (depthWriteEnable=VK_FALSE) — used when a depth
    // prepass has already written the correct depth values. Opaque only; blend
    // pipelines already have depth write disabled.
    // VARIANT_HAS_NORMAL_MAP = true
    VkPipeline dro_backface_pipeline { VK_NULL_HANDLE };
    VkPipeline dro_frontface_pipeline { VK_NULL_HANDLE };
    VkPipeline dro_no_cull_pipeline { VK_NULL_HANDLE };
    // VARIANT_HAS_NORMAL_MAP = false
    VkPipeline dro_backface_no_nmap_pipeline { VK_NULL_HANDLE };
    VkPipeline dro_frontface_no_nmap_pipeline { VK_NULL_HANDLE };
    VkPipeline dro_no_cull_no_nmap_pipeline { VK_NULL_HANDLE };

    // depth_read_only: pass true when a depth prepass already filled the depth
    // buffer — switches opaque groups to the DRO variants so they test depth but
    // don't re-write it (avoids Z-fighting from precision divergence).
    VkPipeline pipeline_for(AlphaMode alpha_mode, CullMode cull_mode, bool has_normal_map = true, bool depth_read_only = false) const
    {
        auto is_blend = alpha_mode == AlphaMode::Blend;
        if (!has_normal_map) {
            if (is_blend) {
                if (cull_mode == CullMode::Disabled) return blend_no_cull_no_nmap_pipeline;
                if (cull_mode == CullMode::Front) return blend_frontface_no_nmap_pipeline;
                return blend_backface_no_nmap_pipeline;
            }
            if (depth_read_only) {
                if (cull_mode == CullMode::Disabled) return dro_no_cull_no_nmap_pipeline;
                if (cull_mode == CullMode::Front) return dro_frontface_no_nmap_pipeline;
                return dro_backface_no_nmap_pipeline;
            }
            if (alpha_mode == AlphaMode::Hash) {
                if (cull_mode == CullMode::Disabled) return hash_no_cull_no_nmap_pipeline;
                if (cull_mode == CullMode::Front) return hash_frontface_no_nmap_pipeline;
                return hash_backface_no_nmap_pipeline;
            }
            if (alpha_mode == AlphaMode::Clip) {
                if (cull_mode == CullMode::Disabled) return clip_no_cull_no_nmap_pipeline;
                if (cull_mode == CullMode::Front) return clip_frontface_no_nmap_pipeline;
                return clip_backface_no_nmap_pipeline;
            }
            if (cull_mode == CullMode::Disabled) return opaque_no_cull_no_nmap_pipeline;
            if (cull_mode == CullMode::Front) return opaque_frontface_no_nmap_pipeline;
            return opaque_backface_no_nmap_pipeline;
        }
        if (is_blend) {
            if (cull_mode == CullMode::Disabled) return blend_no_cull_pipeline;
            if (cull_mode == CullMode::Front) return blend_frontface_pipeline;
            return blend_backface_pipeline;
        }
        if (depth_read_only) {
            if (cull_mode == CullMode::Disabled) return dro_no_cull_pipeline;
            if (cull_mode == CullMode::Front) return dro_frontface_pipeline;
            return dro_backface_pipeline;
        }
        if (alpha_mode == AlphaMode::Hash) {
            if (cull_mode == CullMode::Disabled) return hash_no_cull_pipeline;
            if (cull_mode == CullMode::Front) return hash_frontface_pipeline;
            return hash_backface_pipeline;
        }
        if (alpha_mode == AlphaMode::Clip) {
            if (cull_mode == CullMode::Disabled) return clip_no_cull_pipeline;
            if (cull_mode == CullMode::Front) return clip_frontface_pipeline;
            return clip_backface_pipeline;
        }
        if (cull_mode == CullMode::Disabled) return opaque_no_cull_pipeline;
        if (cull_mode == CullMode::Front) return opaque_frontface_pipeline;
        return opaque_backface_pipeline;
    }

    void destroy(VkDevice device) {
        auto destroy_pipeline = [&](VkPipeline& p) { if (p != VK_NULL_HANDLE) { vkDestroyPipeline(device, p, nullptr); p = VK_NULL_HANDLE; } };
        destroy_pipeline(opaque_backface_pipeline);
        destroy_pipeline(opaque_frontface_pipeline);
        destroy_pipeline(opaque_no_cull_pipeline);
        destroy_pipeline(clip_backface_pipeline);
        destroy_pipeline(clip_frontface_pipeline);
        destroy_pipeline(clip_no_cull_pipeline);
        destroy_pipeline(hash_backface_pipeline);
        destroy_pipeline(hash_frontface_pipeline);
        destroy_pipeline(hash_no_cull_pipeline);
        destroy_pipeline(blend_backface_pipeline);
        destroy_pipeline(blend_frontface_pipeline);
        destroy_pipeline(blend_no_cull_pipeline);
        destroy_pipeline(opaque_backface_no_nmap_pipeline);
        destroy_pipeline(opaque_frontface_no_nmap_pipeline);
        destroy_pipeline(opaque_no_cull_no_nmap_pipeline);
        destroy_pipeline(clip_backface_no_nmap_pipeline);
        destroy_pipeline(clip_frontface_no_nmap_pipeline);
        destroy_pipeline(clip_no_cull_no_nmap_pipeline);
        destroy_pipeline(hash_backface_no_nmap_pipeline);
        destroy_pipeline(hash_frontface_no_nmap_pipeline);
        destroy_pipeline(hash_no_cull_no_nmap_pipeline);
        destroy_pipeline(blend_backface_no_nmap_pipeline);
        destroy_pipeline(blend_frontface_no_nmap_pipeline);
        destroy_pipeline(blend_no_cull_no_nmap_pipeline);
        destroy_pipeline(dro_backface_pipeline);
        destroy_pipeline(dro_frontface_pipeline);
        destroy_pipeline(dro_no_cull_pipeline);
        destroy_pipeline(dro_backface_no_nmap_pipeline);
        destroy_pipeline(dro_frontface_no_nmap_pipeline);
        destroy_pipeline(dro_no_cull_no_nmap_pipeline);
        if (layout != VK_NULL_HANDLE) vkDestroyPipelineLayout(device, layout, nullptr);
        // Note: pbr_layout is now owned by BindlessSet and destroyed there.
        if (light_layout != VK_NULL_HANDLE) vkDestroyDescriptorSetLayout(device, light_layout, nullptr);
        if (material_layout != VK_NULL_HANDLE) vkDestroyDescriptorSetLayout(device, material_layout, nullptr);
        if (shadow_layout != VK_NULL_HANDLE) vkDestroyDescriptorSetLayout(device, shadow_layout, nullptr);
        if (cluster_layout != VK_NULL_HANDLE) vkDestroyDescriptorSetLayout(device, cluster_layout, nullptr);
        layout = VK_NULL_HANDLE;
        pbr_layout = light_layout = material_layout = shadow_layout = cluster_layout = VK_NULL_HANDLE;
    }
};

struct VulkanShadowPipeline {
    VkPipelineLayout layout { VK_NULL_HANDLE };
    VkPipeline backface_pipeline { VK_NULL_HANDLE };
    VkPipeline frontface_pipeline { VK_NULL_HANDLE };
    VkPipeline no_cull_pipeline { VK_NULL_HANDLE };

    VkPipeline pipeline_for(VulkanWorldPipeline::CullMode cull_mode) const
    {
        if (cull_mode == VulkanWorldPipeline::CullMode::Disabled) return no_cull_pipeline;
        if (cull_mode == VulkanWorldPipeline::CullMode::Front) return frontface_pipeline;
        return backface_pipeline;
    }

    void destroy(VkDevice device) {
        if (backface_pipeline != VK_NULL_HANDLE) vkDestroyPipeline(device, backface_pipeline, nullptr);
        if (frontface_pipeline != VK_NULL_HANDLE) vkDestroyPipeline(device, frontface_pipeline, nullptr);
        if (no_cull_pipeline != VK_NULL_HANDLE) vkDestroyPipeline(device, no_cull_pipeline, nullptr);
        if (layout != VK_NULL_HANDLE) vkDestroyPipelineLayout(device, layout, nullptr);
        backface_pipeline = frontface_pipeline = no_cull_pipeline = VK_NULL_HANDLE;
        layout = VK_NULL_HANDLE;
    }
};

struct VulkanDepthPipeline {
    VkPipelineLayout layout { VK_NULL_HANDLE };
    VkPipeline backface_pipeline { VK_NULL_HANDLE };
    VkPipeline frontface_pipeline { VK_NULL_HANDLE };
    VkPipeline no_cull_pipeline { VK_NULL_HANDLE };

    VkPipeline pipeline_for(VulkanWorldPipeline::CullMode cull_mode) const
    {
        if (cull_mode == VulkanWorldPipeline::CullMode::Disabled) return no_cull_pipeline;
        if (cull_mode == VulkanWorldPipeline::CullMode::Front) return frontface_pipeline;
        return backface_pipeline;
    }

    void destroy(VkDevice device) {
        if (backface_pipeline != VK_NULL_HANDLE) vkDestroyPipeline(device, backface_pipeline, nullptr);
        if (frontface_pipeline != VK_NULL_HANDLE) vkDestroyPipeline(device, frontface_pipeline, nullptr);
        if (no_cull_pipeline != VK_NULL_HANDLE) vkDestroyPipeline(device, no_cull_pipeline, nullptr);
        if (layout != VK_NULL_HANDLE) vkDestroyPipelineLayout(device, layout, nullptr);
        backface_pipeline = frontface_pipeline = no_cull_pipeline = VK_NULL_HANDLE;
        layout = VK_NULL_HANDLE;
    }
};

struct VulkanPanelPipeline {
    VkDescriptorSetLayout layout { VK_NULL_HANDLE };
    VkPipelineLayout pipeline_layout { VK_NULL_HANDLE };
    VkPipeline pipeline { VK_NULL_HANDLE };

    void destroy(VkDevice device) {
        if (pipeline != VK_NULL_HANDLE) vkDestroyPipeline(device, pipeline, nullptr);
        if (pipeline_layout != VK_NULL_HANDLE) vkDestroyPipelineLayout(device, pipeline_layout, nullptr);
        if (layout != VK_NULL_HANDLE) vkDestroyDescriptorSetLayout(device, layout, nullptr);
        pipeline = VK_NULL_HANDLE;
        pipeline_layout = VK_NULL_HANDLE;
        layout = VK_NULL_HANDLE;
    }
};

struct VulkanPostPipeline {
    VkDescriptorSetLayout layout { VK_NULL_HANDLE };
    VkPipelineLayout pipeline_layout { VK_NULL_HANDLE };
    VkPipeline pipeline { VK_NULL_HANDLE };

    void destroy(VkDevice device) {
        if (pipeline != VK_NULL_HANDLE) vkDestroyPipeline(device, pipeline, nullptr);
        if (pipeline_layout != VK_NULL_HANDLE) vkDestroyPipelineLayout(device, pipeline_layout, nullptr);
        if (layout != VK_NULL_HANDLE) vkDestroyDescriptorSetLayout(device, layout, nullptr);
        pipeline = VK_NULL_HANDLE;
        pipeline_layout = VK_NULL_HANDLE;
        layout = VK_NULL_HANDLE;
    }
};

class VulkanPipelineManager {
public:
    static ErrorOr<VkShaderModule> create_shader_module(VkDevice, ShaderBytecode const&);

    static ErrorOr<void> ensure_shader_bytecode_loaded(
        VkDevice device,
        ShaderBytecode& vertex_bytecode,
        ShaderBytecode& fragment_bytecode,
        Optional<ShaderBytecode>& panel_vertex_bytecode,
        Optional<ShaderBytecode>& panel_fragment_bytecode,
        Optional<ShaderBytecode>& post_vertex_bytecode,
        Optional<ShaderBytecode>& post_fragment_bytecode,
        Optional<ShaderBytecode>& shadow_vertex_bytecode,
        ShaderBytecode& cull_bytecode,
        Optional<ShaderBytecode>& cluster_compute_bytecode,
        bool& out_loaded);

    static ErrorOr<VulkanWorldPipeline> create_world_pipeline(
        VkDevice,
        VkFormat color_format,
        VkFormat depth_format,
        VkDescriptorSetLayout pbr_layout,
        ShaderBytecode const& vertex_shader,
        ShaderBytecode const& fragment_shader);

    static ErrorOr<VulkanPanelPipeline> create_panel_pipeline(
        VkDevice,
        VkFormat color_format,
        VkFormat depth_format,
        ShaderBytecode const& vertex_shader,
        ShaderBytecode const& fragment_shader);

    static ErrorOr<VulkanPostPipeline> create_post_pipeline(
        VkDevice,
        VkFormat color_format,
        ShaderBytecode const& vertex_shader,
        ShaderBytecode const& fragment_shader);

    static ErrorOr<VulkanShadowPipeline> create_shadow_pipeline(
        VkDevice,
        VkFormat depth_format,
        ShaderBytecode const& vertex_shader);

    static ErrorOr<VulkanDepthPipeline> create_depth_prepass_pipeline(
        VkDevice,
        VkFormat depth_format,
        ShaderBytecode const& vertex_shader);
};

}
