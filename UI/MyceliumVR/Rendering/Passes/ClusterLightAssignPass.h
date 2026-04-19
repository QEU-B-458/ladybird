/*
 * MyceliumVR - a social VR platform built on the Ladybird browser engine
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include "../VulkanRenderPass.h"
#include "../Pipeline/VulkanPipeline.h"
#include "../Backend/VulkanResourceManager.h"
#include "../VulkanCommon.h"

namespace MyceliumVR {

class ClusterLightAssignPass : public VulkanRenderPass {
public:
    struct GraphBindings {
        BufferHandle point_lights_buffer {};
        BufferHandle count_buffer {};
        BufferHandle index_buffer {};
    };

    ClusterLightAssignPass(VkPipeline pipeline, VkPipelineLayout layout, VkDescriptorSet descriptor_set);
    virtual ~ClusterLightAssignPass() override = default;

    virtual ErrorOr<void> prepare(VulkanContext const&, VkCommandPool) override { return {}; }
    virtual void record_uploads(VkCommandBuffer) override {}
    virtual void execute(VkCommandBuffer, VkExtent2D, PushConstants const&) override {}
    virtual void destroy(VkDevice, VmaAllocator) override;

    virtual String name() const override { return MUST(String::from_utf8("ClusterLightAssign"sv)); }
    virtual void setup(RenderPassBuilder&) override;
    virtual void execute(VkCommandBuffer, RenderGraph&) override;

    // Called once from ensure_vulkan_resources to create fixed-size SSBOs.
    ErrorOr<void> ensure_buffers(VulkanContext const& ctx);

    // Called each frame before building the render graph.
    void upload_lights(SceneLightData const& lights);
    void set_view_matrix(Mat4 const& view) { m_view_matrix = view; }
    void set_projection_params(float near_plane, float far_plane, float proj_x, float proj_y)
    {
        m_near = near_plane;
        m_far  = far_plane;
        m_proj_x = proj_x;
        m_proj_y = proj_y;
    }
    void set_graph_bindings(GraphBindings b) { m_graph_bindings = b; }

    VulkanBuffer const& point_light_buffer() const { return m_point_light_ssbo; }
    VulkanBuffer const& cluster_count_buffer() const { return m_cluster_count_ssbo; }
    VulkanBuffer const& cluster_index_buffer() const { return m_cluster_index_ssbo; }

    bool buffers_ready() const { return m_point_light_ssbo.buffer != VK_NULL_HANDLE; }

private:
    VmaAllocator m_allocator { nullptr };
    VkPipeline       m_pipeline       { VK_NULL_HANDLE };
    VkPipelineLayout m_layout         { VK_NULL_HANDLE };
    VkDescriptorSet  m_descriptor_set { VK_NULL_HANDLE };

    VulkanBuffer m_point_light_ssbo;   // host-visible — MaxPointLights * sizeof(GPUPointLight)
    VulkanBuffer m_cluster_count_ssbo; // GPU-local    — ClusterTotal * sizeof(uint)
    VulkanBuffer m_cluster_index_ssbo; // GPU-local    — ClusterTotal * MaxLightsPerCluster * sizeof(uint)

    Mat4  m_view_matrix {};
    float m_near   { 0.1f };
    float m_far    { 100.0f };
    float m_proj_x { 1.0f };
    float m_proj_y { -1.0f };
    u32   m_n_lights { 0 };
    GraphBindings m_graph_bindings;
};

}
