/*
 * MyceliumVR - a social VR platform built on the Ladybird browser engine
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "ClusterLightAssignPass.h"

#include <UI/MyceliumVR/Support/Profiling.h>

namespace MyceliumVR {

ClusterLightAssignPass::ClusterLightAssignPass(VkPipeline pipeline, VkPipelineLayout layout, VkDescriptorSet descriptor_set)
    : m_pipeline(pipeline)
    , m_layout(layout)
    , m_descriptor_set(descriptor_set)
{
}

ErrorOr<void> ClusterLightAssignPass::ensure_buffers(VulkanContext const& ctx)
{
    if (m_point_light_ssbo.buffer != VK_NULL_HANDLE)
        return {};

    m_allocator = ctx.allocator();

    VkDeviceSize point_light_size = static_cast<VkDeviceSize>(MaxPointLights) * sizeof(GPUPointLight);
    TRY(VulkanResourceManager::create_buffer(m_allocator, point_light_size,
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
        VMA_MEMORY_USAGE_CPU_ONLY,
        m_point_light_ssbo));

    VkDeviceSize count_size = static_cast<VkDeviceSize>(ClusterTotal) * sizeof(u32);
    TRY(VulkanResourceManager::create_buffer(m_allocator, count_size,
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
        VMA_MEMORY_USAGE_GPU_ONLY,
        m_cluster_count_ssbo));

    VkDeviceSize index_size = static_cast<VkDeviceSize>(ClusterTotal) * MaxLightsPerCluster * sizeof(u32);
    TRY(VulkanResourceManager::create_buffer(m_allocator, index_size,
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
        VMA_MEMORY_USAGE_GPU_ONLY,
        m_cluster_index_ssbo));

    // Wire compute descriptor set to the freshly created buffers.
    if (m_descriptor_set != VK_NULL_HANDLE) {
        VkDescriptorBufferInfo infos[3] {
            { m_point_light_ssbo.buffer,   0, m_point_light_ssbo.size   },
            { m_cluster_count_ssbo.buffer, 0, m_cluster_count_ssbo.size },
            { m_cluster_index_ssbo.buffer, 0, m_cluster_index_ssbo.size },
        };
        VkWriteDescriptorSet writes[3] {};
        for (u32 i = 0; i < 3; ++i) {
            writes[i].sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            writes[i].dstSet          = m_descriptor_set;
            writes[i].dstBinding      = i;
            writes[i].descriptorCount = 1;
            writes[i].descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            writes[i].pBufferInfo     = &infos[i];
        }
        vkUpdateDescriptorSets(ctx.device(), 3, writes, 0, nullptr);
    }

    return {};
}

void ClusterLightAssignPass::upload_lights(SceneLightData const& lights)
{
    m_n_lights = static_cast<u32>(min(lights.point_light_count, MaxPointLights));
    if (m_point_light_ssbo.buffer == VK_NULL_HANDLE || m_n_lights == 0)
        return;

    // Map and fill the host-visible point-light SSBO.  The compute shader will
    // read this buffer; no GPU-side copy is needed.
    void* mapped = nullptr;
    if (vmaMapMemory(m_allocator, m_point_light_ssbo.allocation, &mapped) != VK_SUCCESS)
        return;

    auto* dst = static_cast<GPUPointLight*>(mapped);
    for (u32 i = 0; i < m_n_lights; ++i) {
        auto const& src = lights.point_lights[i];
        dst[i].position_radius[0] = src.position[0];
        dst[i].position_radius[1] = src.position[1];
        dst[i].position_radius[2] = src.position[2];
        dst[i].position_radius[3] = src.radius;
        dst[i].color_intensity[0] = src.color[0];
        dst[i].color_intensity[1] = src.color[1];
        dst[i].color_intensity[2] = src.color[2];
        dst[i].color_intensity[3] = src.intensity;
        dst[i].spot_direction[0]  = src.direction[0];
        dst[i].spot_direction[1]  = src.direction[1];
        dst[i].spot_direction[2]  = src.direction[2];
        dst[i].spot_direction[3]  = static_cast<float>(src.type);
        dst[i].spot_cone[0]       = src.cone_inner_cos;
        dst[i].spot_cone[1]       = src.cone_outer_cos;
        dst[i].spot_cone[2]       = 0.0f;
        dst[i].spot_cone[3]       = 0.0f;
    }
    vmaUnmapMemory(m_allocator, m_point_light_ssbo.allocation);
}

void ClusterLightAssignPass::destroy(VkDevice, VmaAllocator allocator)
{
    m_point_light_ssbo.destroy(allocator);
    m_cluster_count_ssbo.destroy(allocator);
    m_cluster_index_ssbo.destroy(allocator);
}

void ClusterLightAssignPass::setup(RenderPassBuilder& builder)
{
    builder.set_queue_preference(QueueKind::Compute);
    if (!buffers_ready())
        return;

    VERIFY(m_graph_bindings.point_lights_buffer.value != 0);
    VERIFY(m_graph_bindings.count_buffer.value != 0);
    VERIFY(m_graph_bindings.index_buffer.value != 0);

    builder.read_storage(m_graph_bindings.point_lights_buffer);
    builder.write_storage(m_graph_bindings.count_buffer);
    builder.write_storage(m_graph_bindings.index_buffer);
}

void ClusterLightAssignPass::execute(VkCommandBuffer cmd, RenderGraph&)
{
    ZoneScoped;
    if (!buffers_ready())
        return;

    struct ClusterPC {
        float view[16];
        float near;
        float far;
        u32   n_lights;
        float proj_x;
        float proj_y;
    } push;
    static_assert(sizeof(ClusterPC) == 84);

    for (int i = 0; i < 16; ++i)
        push.view[i] = m_view_matrix.elements[i];
    push.near     = m_near;
    push.far      = m_far;
    push.n_lights = m_n_lights;
    push.proj_x   = m_proj_x;
    push.proj_y   = m_proj_y;

    {
        ZoneScopedN("ClusterLightAssign/BindPipeline");
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_pipeline);
    }
    {
        ZoneScopedN("ClusterLightAssign/PushConstants");
        vkCmdPushConstants(cmd, m_layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(ClusterPC), &push);
    }
    {
        ZoneScopedN("ClusterLightAssign/BindDescriptors");
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_layout, 0, 1, &m_descriptor_set, 0, nullptr);
    }
    {
        ZoneScopedN("ClusterLightAssign/Dispatch");
        // ceil(ClusterTotal / 64) = ceil(3456 / 64) = 54 workgroups
        vkCmdDispatch(cmd, (ClusterTotal + 63) / 64, 1, 1);
    }
}

}
