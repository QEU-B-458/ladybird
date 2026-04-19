/*
 * MyceliumVR - a social VR platform built on the Ladybird browser engine
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "ComputeCullPass.h"

namespace MyceliumVR {

ComputeCullPass::ComputeCullPass(VkPipeline pipeline, VkPipelineLayout layout, VkDescriptorSet descriptor_set)
    : m_pipeline(pipeline)
    , m_layout(layout)
    , m_descriptor_set(descriptor_set)
{
}

void ComputeCullPass::setup(RenderPassBuilder& builder)
{
    builder.set_queue_preference(QueueKind::Compute);
    if (m_group_count == 0)
        return;

    VERIFY(m_graph_bindings.ref_indirect.value != 0);
    VERIFY(m_graph_bindings.live_indirect.value != 0);
    VERIFY(m_graph_bindings.aabb_buffer.value != 0);
    builder.read_storage(m_graph_bindings.ref_indirect);
    builder.read_storage(m_graph_bindings.aabb_buffer);
    builder.write_storage(m_graph_bindings.live_indirect);
}

void ComputeCullPass::execute(VkCommandBuffer cmd, RenderGraph&)
{
    if (m_group_count == 0) return;

    struct CullPC { float vp[16]; u32 count; } pc;
    memcpy(pc.vp, m_pc.view_projection, 64);
    pc.count = m_group_count;

    {
        ZoneScopedN("ComputeCullPass/BindPipeline");
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_pipeline);
    }
    {
        ZoneScopedN("ComputeCullPass/PushConstants");
        vkCmdPushConstants(cmd, m_layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(CullPC), &pc);
    }
    {
        ZoneScopedN("ComputeCullPass/BindDescriptors");
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_layout, 0, 1, &m_descriptor_set, 0, nullptr);
    }
    {
        ZoneScopedN("ComputeCullPass/Dispatch");
        vkCmdDispatch(cmd, (m_group_count + 63) / 64, 1, 1);
    }
}

ErrorOr<void> ComputeCullPass::update_descriptor_set(VkDevice device, VulkanBuffer const& ref_indirect, VulkanBuffer const& live_indirect, VulkanBuffer const& aabb_buffer)
{
    if (m_descriptor_set == VK_NULL_HANDLE)
        return {};

    VkDescriptorBufferInfo infos[3] {
        { .buffer = ref_indirect.buffer, .offset = 0, .range = ref_indirect.size },
        { .buffer = live_indirect.buffer, .offset = 0, .range = live_indirect.size },
        { .buffer = aabb_buffer.buffer, .offset = 0, .range = aabb_buffer.size },
    };
    VkWriteDescriptorSet writes[3] {};
    for (u32 i = 0; i < 3; ++i) {
        writes[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[i].dstSet = m_descriptor_set;
        writes[i].dstBinding = i;
        writes[i].descriptorCount = 1;
        writes[i].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        writes[i].pBufferInfo = &infos[i];
    }
    vkUpdateDescriptorSets(device, 3, writes, 0, nullptr);
    return {};
}

}
