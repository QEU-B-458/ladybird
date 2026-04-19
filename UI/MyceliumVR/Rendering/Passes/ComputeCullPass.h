/*
 * MyceliumVR - a social VR platform built on the Ladybird browser engine
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include "../VulkanRenderPass.h"
#include "../Pipeline/VulkanPipeline.h"

namespace MyceliumVR {

class ComputeCullPass : public VulkanRenderPass {
public:
    struct GraphBindings {
        BufferHandle ref_indirect {};
        BufferHandle live_indirect {};
        BufferHandle aabb_buffer {};
    };

    ComputeCullPass(VkPipeline pipeline, VkPipelineLayout layout, VkDescriptorSet descriptor_set);
    virtual ~ComputeCullPass() override = default;

    virtual ErrorOr<void> prepare(VulkanContext const&, VkCommandPool) override { return {}; }
    virtual void execute(VkCommandBuffer, VkExtent2D, PushConstants const&) override {}
    virtual void destroy(VkDevice, VmaAllocator) override {}

    virtual String name() const override { return MUST(String::from_utf8("ComputeCull"sv)); }
    virtual void setup(RenderPassBuilder&) override;
    virtual void execute(VkCommandBuffer cmd, RenderGraph& graph) override;

    void set_params(PushConstants const& pc, u32 group_count)
    {
        m_pc = pc;
        m_group_count = group_count;
    }
    void set_graph_bindings(GraphBindings bindings) { m_graph_bindings = bindings; }
    ErrorOr<void> update_descriptor_set(VkDevice device, VulkanBuffer const& ref_indirect, VulkanBuffer const& live_indirect, VulkanBuffer const& aabb_buffer);

private:
    VkPipeline m_pipeline;
    VkPipelineLayout m_layout;
    VkDescriptorSet m_descriptor_set;
    PushConstants m_pc;
    u32 m_group_count { 0 };
    GraphBindings m_graph_bindings;
};

}
