/*
 * MyceliumVR - a social VR platform built on the Ladybird browser engine
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "DepthPrepass.h"

#if defined(TRACY_ENABLE)
#    include <tracy/Tracy.hpp>
#endif

namespace MyceliumVR {

void DepthPrepass::set_draw_groups(Vector<DrawGroup> const& groups)
{
    m_opaque_back_count = 0;
    m_opaque_front_count = 0;
    m_opaque_disabled_count = 0;
    // Groups are sorted opaque-first (guaranteed by VulkanMeshBuilder::build_world_instanced).
    // We stop at the first non-opaque group since all opaque groups are contiguous.
    for (auto const& g : groups) {
        if (g.alpha_mode != 0)
            break;
        switch (g.cull_mode) {
        case DrawGroup::CullMode::Front:    ++m_opaque_front_count;    break;
        case DrawGroup::CullMode::Disabled: ++m_opaque_disabled_count; break;
        default:                            ++m_opaque_back_count;     break;
        }
    }
}

void DepthPrepass::setup(RenderPassBuilder& builder)
{
    if (!has_work())
        return;

    VERIFY(m_graph_bindings.depth_target.value != 0);
    VERIFY(m_graph_bindings.vertex_buffer.value != 0);
    VERIFY(m_graph_bindings.instance_buffer.value != 0);
    VERIFY(m_graph_bindings.indirect_buffer.value != 0);

    builder.read_vertex(m_graph_bindings.vertex_buffer);
    builder.read_vertex(m_graph_bindings.instance_buffer);
    builder.read_indirect(m_graph_bindings.indirect_buffer);

    VkClearValue clear_depth {};
    clear_depth.depthStencil = { 0.0f, 0 }; // reverse-Z: 0 = far plane
    builder.write_depth(m_graph_bindings.depth_target, { VK_ATTACHMENT_LOAD_OP_CLEAR, VK_ATTACHMENT_STORE_OP_STORE, clear_depth });
}

void DepthPrepass::execute(VkCommandBuffer cmd, RenderGraph& graph)
{
    ZoneScoped;
#if defined(TRACY_ENABLE)
    TracyVkZone(graph.tracy_context(), cmd, "DepthPrepass");
#endif
    if (!has_work())
        return;

    auto extent = graph.get_texture_extent(m_graph_bindings.depth_target);
    VkViewport viewport {
        .x = 0.0f,
        .y = 0.0f,
        .width = static_cast<float>(extent.width),
        .height = static_cast<float>(extent.height),
        .minDepth = 0.0f,
        .maxDepth = 1.0f,
    };
    VkRect2D scissor { { 0, 0 }, extent };
    vkCmdSetViewport(cmd, 0, 1, &viewport);
    vkCmdSetScissor(cmd, 0, 1, &scissor);

    vkCmdPushConstants(cmd, m_pipeline.layout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(PushConstants), &m_push_constants);

    VkBuffer vertex_buffer = graph.get_buffer(m_graph_bindings.vertex_buffer);
    VkBuffer instance_buffer = graph.get_buffer(m_graph_bindings.instance_buffer);
    VkDeviceSize offsets[] { 0, 0 };
    VkBuffer buffers[] { vertex_buffer, instance_buffer };
    vkCmdBindVertexBuffers(cmd, 0, 2, buffers, offsets);

    VkBuffer indirect_buffer = graph.get_buffer(m_graph_bindings.indirect_buffer);
    bool multi_draw = graph.context().supports_multi_draw_indirect();

    auto draw_bucket = [&](u32 begin, u32 count, VulkanWorldPipeline::CullMode cull_mode) {
        if (count == 0)
            return;
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipeline.pipeline_for(cull_mode));
        if (multi_draw) {
            vkCmdDrawIndirect(cmd, indirect_buffer, begin * sizeof(VkDrawIndirectCommand), count, sizeof(VkDrawIndirectCommand));
        } else {
            for (u32 i = begin; i < begin + count; ++i)
                vkCmdDrawIndirect(cmd, indirect_buffer, i * sizeof(VkDrawIndirectCommand), 1, sizeof(VkDrawIndirectCommand));
        }
    };

    {
        ZoneScopedN("DepthPrepass/Draws");
        u32 offset = 0;
        draw_bucket(offset, m_opaque_back_count,     VulkanWorldPipeline::CullMode::Back);
        offset += m_opaque_back_count;
        draw_bucket(offset, m_opaque_front_count,    VulkanWorldPipeline::CullMode::Front);
        offset += m_opaque_front_count;
        draw_bucket(offset, m_opaque_disabled_count, VulkanWorldPipeline::CullMode::Disabled);
    }
}

}
