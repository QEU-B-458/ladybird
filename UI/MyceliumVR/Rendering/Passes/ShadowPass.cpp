/*
 * MyceliumVR - a social VR platform built on the Ladybird browser engine
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "ShadowPass.h"

namespace MyceliumVR {

void ShadowPass::setup(RenderPassBuilder& builder)
{
    if (m_cull_modes.is_empty())
        return;

    VERIFY(m_graph_bindings.shadow_target.value != 0);
    VERIFY(m_graph_bindings.vertex_buffer.value != 0);
    VERIFY(m_graph_bindings.instance_buffer.value != 0);
    VERIFY(m_graph_bindings.indirect_buffer.value != 0);

    builder.read_vertex(m_graph_bindings.vertex_buffer);
    builder.read_vertex(m_graph_bindings.instance_buffer);
    builder.read_indirect(m_graph_bindings.indirect_buffer);

    VkClearValue clear_depth {};
    clear_depth.depthStencil = { 1.0f, 0 };
    builder.write_depth(m_graph_bindings.shadow_target, { VK_ATTACHMENT_LOAD_OP_CLEAR, VK_ATTACHMENT_STORE_OP_STORE, clear_depth });
}

void ShadowPass::execute(VkCommandBuffer cmd, RenderGraph& graph)
{
    ZoneScoped;
#if defined(TRACY_ENABLE)
    TracyVkZone(graph.tracy_context(), cmd, "ShadowPass");
#endif
    if (m_cull_modes.is_empty())
        return;

    auto extent = graph.get_texture_extent(m_graph_bindings.shadow_target);
    {
        ZoneScopedN("ShadowPass/ViewportScissor");
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
    }
    {
        ZoneScopedN("ShadowPass/DepthBias");
        vkCmdSetDepthBias(cmd, 1.25f, 0.0f, 1.75f);
    }
    {
        ZoneScopedN("ShadowPass/PushConstants");
        vkCmdPushConstants(cmd, m_pipeline.layout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(Mat4), &m_light_view_projection);
    }
    {
        ZoneScopedN("ShadowPass/BindBuffers");
        VkBuffer vertex_buffer = graph.get_buffer(m_graph_bindings.vertex_buffer);
        VkBuffer instance_buffer = graph.get_buffer(m_graph_bindings.instance_buffer);
        VkDeviceSize offsets[] { 0, 0 };
        VkBuffer buffers[] { vertex_buffer, instance_buffer };
        vkCmdBindVertexBuffers(cmd, 0, 2, buffers, offsets);
    }

    auto indirect_buffer = graph.get_buffer(m_graph_bindings.indirect_buffer);
    bool multi_draw = graph.context().supports_multi_draw_indirect();

    auto draw_bucket = [&](size_t begin, size_t end, DrawGroup::CullMode cull_mode) {
        if (begin >= end)
            return;
        VulkanWorldPipeline::CullMode vk_cull;
        switch (cull_mode) {
        case DrawGroup::CullMode::Disabled: vk_cull = VulkanWorldPipeline::CullMode::Disabled; break;
        case DrawGroup::CullMode::Front:    vk_cull = VulkanWorldPipeline::CullMode::Front;    break;
        default:                            vk_cull = VulkanWorldPipeline::CullMode::Back;     break;
        }
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipeline.pipeline_for(vk_cull));
        if (multi_draw) {
            vkCmdDrawIndirect(cmd, indirect_buffer, begin * sizeof(VkDrawIndirectCommand), static_cast<u32>(end - begin), sizeof(VkDrawIndirectCommand));
        } else {
            for (size_t i = begin; i < end; ++i)
                vkCmdDrawIndirect(cmd, indirect_buffer, i * sizeof(VkDrawIndirectCommand), 1, sizeof(VkDrawIndirectCommand));
        }
    };

    {
        ZoneScopedN("ShadowPass/Draws");
        Optional<size_t> bucket_begin;
        DrawGroup::CullMode current_cull_mode = DrawGroup::CullMode::Back;
        for (size_t i = 0; i < m_cull_modes.size(); ++i) {
            if (!bucket_begin.has_value()) {
                bucket_begin = i;
                current_cull_mode = m_cull_modes[i];
                continue;
            }
            if (m_cull_modes[i] != current_cull_mode) {
                draw_bucket(*bucket_begin, i, current_cull_mode);
                bucket_begin = i;
                current_cull_mode = m_cull_modes[i];
            }
        }
        if (bucket_begin.has_value())
            draw_bucket(*bucket_begin, m_cull_modes.size(), current_cull_mode);
    }
}

}
