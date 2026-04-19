/*
 * MyceliumVR - a social VR platform built on the Ladybird browser engine
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include "../VulkanRenderPass.h"
#include "../Pipeline/VulkanPipeline.h"
#include "../Pipeline/VulkanMeshBuilder.h"

namespace MyceliumVR {

class DepthPrepass : public VulkanRenderPass {
public:
    struct GraphBindings {
        TextureHandle depth_target {};
        BufferHandle vertex_buffer {};
        BufferHandle instance_buffer {};
        BufferHandle indirect_buffer {};
    };

    explicit DepthPrepass(VulkanDepthPipeline const& pipeline)
        : m_pipeline(pipeline)
    {
    }

    virtual ErrorOr<void> prepare(VulkanContext const&, VkCommandPool) override { return {}; }
    virtual void record_uploads(VkCommandBuffer) override {}
    virtual void destroy(VkDevice, VmaAllocator) override {}
    virtual String name() const override { return MUST(String::from_utf8("DepthPrepass"sv)); }
    virtual void setup(RenderPassBuilder&) override;
    virtual void execute(VkCommandBuffer, VkExtent2D, PushConstants const&) override {}
    virtual void execute(VkCommandBuffer, RenderGraph&) override;

    void set_graph_bindings(GraphBindings bindings) { m_graph_bindings = bindings; }
    void set_camera_view_projection(PushConstants const& pc) { m_push_constants = pc; }
    void set_draw_groups(Vector<DrawGroup> const& groups);

    bool has_work() const { return m_opaque_back_count + m_opaque_front_count + m_opaque_disabled_count > 0; }

private:
    VulkanDepthPipeline const& m_pipeline;
    GraphBindings m_graph_bindings;
    PushConstants m_push_constants;
    u32 m_opaque_back_count { 0 };
    u32 m_opaque_front_count { 0 };
    u32 m_opaque_disabled_count { 0 };
};

}
