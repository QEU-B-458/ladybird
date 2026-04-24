/*
 * MyceliumVR - a social VR platform built on the Ladybird browser engine
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include "../VulkanRenderPass.h"
#include "../Pipeline/VulkanPipeline.h"

namespace MyceliumVR {

enum class PostProcessMode : u32 {
    Threshold = 0,
    BlurHorizontal = 1,
    BlurVertical = 2,
    Composite = 3,
};

struct PostProcessPushConstants {
    float inv_width;
    float inv_height;
    float threshold;
    float bloom_strength;
    u32 mode;
};

class PostProcessPass : public VulkanRenderPass {
public:
    struct GraphBindings {
        TextureHandle input_a;
        TextureHandle input_b;
        TextureHandle output;
    };

    PostProcessPass(StringView name, VulkanPostPipeline const& pipeline, PostProcessMode mode, VkDescriptorSet descriptor_set, VkSampler sampler);
    virtual ~PostProcessPass() override = default;

    virtual void setup(RenderPassBuilder& builder) override;
    virtual void execute(VkCommandBuffer cmd, RenderGraph& graph) override;
    virtual void execute(VkCommandBuffer command_buffer, VkExtent2D extent, PushConstants const& pc) override;
    virtual ErrorOr<void> prepare(VulkanContext const& ctx, VkCommandPool command_pool) override;
    virtual void destroy(VkDevice device, VmaAllocator allocator) override;
    virtual String name() const override { return m_name; }

    void set_graph_bindings(GraphBindings bindings) { m_graph_bindings = bindings; }
    void set_threshold(float threshold) { m_threshold = threshold; }
    void set_bloom_strength(float strength) { m_bloom_strength = strength; }

private:
    String m_name;
    VulkanPostPipeline const& m_pipeline;
    PostProcessMode m_mode;
    VkDescriptorSet m_descriptor_set { VK_NULL_HANDLE };
    VkSampler m_sampler { VK_NULL_HANDLE };
    
    GraphBindings m_graph_bindings;
    float m_threshold { 1.1f };
    float m_bloom_strength { 0.18f };

    VkImageView m_last_view_a { VK_NULL_HANDLE };
    VkImageView m_last_view_b { VK_NULL_HANDLE };

    void update_descriptor_set(VkDevice device, VkImageView view_a, VkImageView view_b);
};

}
