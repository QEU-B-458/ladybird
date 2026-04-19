/*
 * MyceliumVR - a social VR platform built on the Ladybird browser engine
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include "../VulkanRenderPass.h"
#include "../Pipeline/VulkanPipeline.h"
#include "../Pipeline/VulkanMeshBuilder.h"

namespace MyceliumVR {

class ShadowPass : public VulkanRenderPass {
public:
    struct GraphBindings {
        TextureHandle shadow_target {};
        BufferHandle vertex_buffer {};
        BufferHandle instance_buffer {};
        BufferHandle indirect_buffer {};
    };

    explicit ShadowPass(VulkanShadowPipeline const& pipeline)
        : m_pipeline(pipeline)
    {
    }

    virtual ErrorOr<void> prepare(VulkanContext const&, VkCommandPool) override { return {}; }
    virtual void record_uploads(VkCommandBuffer) override {}
    virtual void destroy(VkDevice, VmaAllocator) override {}
    virtual String name() const override { return MUST(String::from_utf8("Shadow"sv)); }
    virtual void setup(RenderPassBuilder&) override;
    virtual void execute(VkCommandBuffer, VkExtent2D, PushConstants const&) override {}
    virtual void execute(VkCommandBuffer, RenderGraph&) override;

    void set_graph_bindings(GraphBindings bindings) { m_graph_bindings = bindings; }
    void set_light_view_projection(Mat4 light_view_projection) { m_light_view_projection = light_view_projection; }
    void set_draw_groups(Vector<DrawGroup> const& groups)
    {
        m_cull_modes.clear();
        m_cull_modes.ensure_capacity(groups.size());
        for (auto const& g : groups)
            m_cull_modes.append(g.cull_mode);
    }

private:
    VulkanShadowPipeline const& m_pipeline;
    GraphBindings m_graph_bindings;
    Mat4 m_light_view_projection;
    Vector<DrawGroup::CullMode> m_cull_modes;
};

}
