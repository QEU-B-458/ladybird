/*
 * MyceliumVR - a social VR platform built on the Ladybird browser engine
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "PostProcessPass.h"

namespace MyceliumVR {

PostProcessPass::PostProcessPass(StringView name, VulkanPostPipeline const& pipeline, PostProcessMode mode, VkDescriptorSet descriptor_set, VkSampler sampler)
    : m_name(MUST(String::from_utf8(name)))
    , m_pipeline(pipeline)
    , m_mode(mode)
    , m_descriptor_set(descriptor_set)
    , m_sampler(sampler)
{
}

void PostProcessPass::setup(RenderPassBuilder& builder)
{
    builder.read_sampled(m_graph_bindings.input_a);
    if (m_graph_bindings.input_b.value != 0)
        builder.read_sampled(m_graph_bindings.input_b);
    
    VkClearValue clear {};
    builder.write_color(m_graph_bindings.output, { VK_ATTACHMENT_LOAD_OP_CLEAR, VK_ATTACHMENT_STORE_OP_STORE, clear });
}

void PostProcessPass::execute(VkCommandBuffer cmd, RenderGraph& graph)
{
    auto extent = graph.get_texture_extent(m_graph_bindings.output);
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

    auto view_a = graph.get_texture_view(m_graph_bindings.input_a);
    auto view_b = m_graph_bindings.input_b.value != 0 ? graph.get_texture_view(m_graph_bindings.input_b) : VK_NULL_HANDLE;
    update_descriptor_set(graph.context().device(), view_a, view_b);

    PostProcessPushConstants push_constants;
    push_constants.inv_width = extent.width > 0 ? 1.0f / static_cast<float>(extent.width) : 0.0f;
    push_constants.inv_height = extent.height > 0 ? 1.0f / static_cast<float>(extent.height) : 0.0f;
    push_constants.threshold = m_threshold;
    push_constants.bloom_strength = m_bloom_strength;
    push_constants.mode = static_cast<u32>(m_mode);

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipeline.pipeline);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipeline.pipeline_layout, 0, 1, &m_descriptor_set, 0, nullptr);
    vkCmdPushConstants(cmd, m_pipeline.pipeline_layout, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(PostProcessPushConstants), &push_constants);
    vkCmdDraw(cmd, 3, 1, 0, 0);
}

void PostProcessPass::execute(VkCommandBuffer, VkExtent2D, PushConstants const&)
{
    // Not used in RenderGraph path.
}

ErrorOr<void> PostProcessPass::prepare(VulkanContext const&, VkCommandPool)
{
    return {};
}

void PostProcessPass::destroy(VkDevice, VmaAllocator)
{
    // Pipelines are managed by VulkanRenderer::Impl.
}

void PostProcessPass::update_descriptor_set(VkDevice device, VkImageView view_a, VkImageView view_b)
{
    if (view_a == m_last_view_a && view_b == m_last_view_b)
        return;

    VkDescriptorImageInfo image_infos[2] {
        { m_sampler, view_a, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL },
        { m_sampler, view_b != VK_NULL_HANDLE ? view_b : view_a, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL },
    };

    VkWriteDescriptorSet writes[2] {};
    for (u32 i = 0; i < 2; ++i) {
        writes[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[i].dstSet = m_descriptor_set;
        writes[i].dstBinding = i;
        writes[i].descriptorCount = 1;
        writes[i].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        writes[i].pImageInfo = &image_infos[i];
    }
    vkUpdateDescriptorSets(device, 2, writes, 0, nullptr);

    m_last_view_a = view_a;
    m_last_view_b = view_b;
}

}
