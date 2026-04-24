/*
 * MyceliumVR - a social VR platform built on the Ladybird browser engine
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include "../VulkanRenderPass.h"
#include "../Pipeline/VulkanPipeline.h"
#include "../Backend/VulkanResourceManager.h"
#include "../Pipeline/VulkanMeshBuilder.h"
#include "../Pipeline/VulkanPBRManager.h"
#include "../../Support/MaterialLibrary.h"
#include "../../Support/MeshLibrary.h"
#include "../Pipeline/TextureLibrary.h"

namespace MyceliumVR {

class GeometryPass : public VulkanRenderPass {
public:
    struct GraphBindings {
        TextureHandle color_target {};
        TextureHandle depth_target {};
        TextureHandle shadow_map {};
        BufferHandle vertex_buffer {};
        BufferHandle instance_buffer {};
        BufferHandle indirect_buffer {};
        BufferHandle cluster_point_lights {};
        BufferHandle cluster_count {};
        BufferHandle cluster_index {};
    };

    GeometryPass(VulkanWorldPipeline const& pipeline, MaterialLibrary& materials, MeshLibrary& meshes, TextureLibrary& textures, VirtualFileSystem const* file_system);
    virtual ~GeometryPass() override = default;

    virtual ErrorOr<void> prepare(VulkanContext const& ctx, VkCommandPool command_pool) override;
    virtual void record_uploads(VkCommandBuffer command_buffer) override;
    virtual void execute(VkCommandBuffer command_buffer, VkExtent2D extent, PushConstants const& pc) override;
    virtual void execute(VkCommandBuffer command_buffer, RenderGraph& graph) override;
    virtual void setup(RenderPassBuilder&) override;
    virtual String name() const override { return MUST(String::from_utf8("Geometry"sv)); }
    virtual void destroy(VkDevice device, VmaAllocator allocator) override;

    void set_world(World const* world) { m_world = world; m_snapshot = {}; m_static_scene = {}; }
    void set_snapshot(ReadonlySpan<u8> snapshot) { m_snapshot = snapshot; m_world = nullptr; }
    void set_static_scene(ReadonlySpan<u8> static_scene) { m_static_scene = static_scene; }
    void force_rebuild() { m_last_processed_scene_revision = 0; }
    void set_scene_light(SceneLightData const& light) { m_scene_light = light; }
    void set_camera_position(float const pos[3]) { memcpy(m_camera_position, pos, sizeof(float) * 3); }
    void set_shadow_view_projection(Mat4 const& shadow_view_projection) { m_shadow_view_projection = shadow_view_projection; }
    void set_shadow_quality(ShadowQuality shadow_quality) { m_shadow_quality = shadow_quality; }
    void set_has_depth_prepass(bool b) { m_has_depth_prepass = b; }
    void set_cluster_descriptor_set(VkDescriptorSet ds) { m_cluster_descriptor_set = ds; }
    void set_cluster_params(float near_plane, float far_plane) { m_cluster_near = near_plane; m_cluster_far = far_plane; }
    void set_screen_size(u32 w, u32 h) { m_screen_w = w; m_screen_h = h; }
    void set_fallback_textures(VulkanTexture const& normal, VulkanTexture const& black)
    {
        m_flat_normal_texture = normal;
        m_fallback_black_texture = black;
    }

    void set_handle_mappings(HashMap<u32, ByteString> const* meshes, HashMap<u32, ByteString> const* materials)
    {
        m_mesh_handles = meshes;
        m_material_handles = materials;
    }

    VulkanBuffer const& indirect_buffer() const { return m_indirect_buffer; }
    VulkanBuffer const& indirect_ref_buffer() const { return m_indirect_ref_buffer; }
    VulkanBuffer const& aabb_world_buffer() const { return m_aabb_world_buffer; }
    VulkanBuffer const& vertex_buffer() const { return m_vertex_buffer; }
    VulkanBuffer const& instance_buffer() const { return m_instance_buffer; }
    VulkanBuffer const& vertex_staging_buffer() const { return m_vertex_staging; }
    VulkanBuffer const& instance_staging_buffer() const { return m_instance_staging; }
    VulkanBuffer const& indirect_staging_buffer() const { return m_indirect_staging; }
    VulkanBuffer const& aabb_staging_buffer() const { return m_aabb_world_staging; }
    Vector<DrawGroup> const& draw_groups() const { return m_draw_groups; }
    bool has_upload_work() const { return m_geometry_upload_pending; }
    bool has_vertex_upload() const { return m_vertex_pending_size > 0; }
    bool has_instance_upload() const { return m_instance_pending_size > 0; }
    bool has_indirect_ref_upload() const { return m_indirect_pending_size > 0; }
    bool has_aabb_upload() const { return m_aabb_pending_size > 0; }
    VkDeviceSize vertex_upload_size() const { return m_vertex_pending_size; }
    VkDeviceSize instance_upload_size() const { return m_instance_pending_size; }
    VkDeviceSize indirect_ref_upload_size() const { return m_indirect_pending_size; }
    VkDeviceSize aabb_upload_size() const { return m_aabb_pending_size; }
    ResourceState vertex_buffer_initial_state() const;
    ResourceState instance_buffer_initial_state() const;
    ResourceState indirect_live_buffer_initial_state() const;
    ResourceState indirect_ref_buffer_initial_state() const;
    ResourceState aabb_buffer_initial_state() const;
    
    VulkanPBRManager::BindlessSet& bindless_set() { return m_bindless_set; }
    void set_graph_bindings(GraphBindings bindings) { m_graph_bindings = bindings; }

private:
    VulkanWorldPipeline const& m_pipeline;
    MaterialLibrary& m_material_library;
    MeshLibrary& m_mesh_library;
    TextureLibrary& m_texture_library;
    VirtualFileSystem const* m_file_system { nullptr };

    World const* m_world { nullptr };
    ReadonlySpan<u8> m_snapshot;
    ReadonlySpan<u8> m_static_scene;
    HashMap<u32, ByteString> const* m_mesh_handles { nullptr };
    HashMap<u32, ByteString> const* m_material_handles { nullptr };
    SceneLightData m_scene_light;
    float m_camera_position[3] { 0.0f, 0.0f, 0.0f };

    VulkanTexture m_flat_normal_texture;
    VulkanTexture m_fallback_black_texture;

    VulkanBuffer m_vertex_buffer;
    VulkanBuffer m_instance_buffer;
    VulkanBuffer m_indirect_buffer;
    VulkanBuffer m_indirect_ref_buffer;
    VulkanBuffer m_aabb_world_buffer;
    VulkanBuffer m_vertex_staging;
    VulkanBuffer m_instance_staging;
    VulkanBuffer m_indirect_staging;
    VulkanBuffer m_aabb_world_staging;
    VkDeviceSize m_vertex_pending_size { 0 };
    VkDeviceSize m_instance_pending_size { 0 };
    VkDeviceSize m_indirect_pending_size { 0 };
    VkDeviceSize m_aabb_pending_size { 0 };
    bool m_geometry_upload_pending { false };
    Vector<DrawGroup> m_draw_groups;

    VulkanPBRManager::BindlessSet m_bindless_set;
    Vector<VkDescriptorSet> m_pbr_descriptor_sets;

    VulkanBuffer m_light_ubo_buffer;
    VkDescriptorPool m_light_ubo_descriptor_pool { VK_NULL_HANDLE };
    VkDescriptorSet m_light_ubo_descriptor_set { VK_NULL_HANDLE };

    VulkanBuffer m_material_ssbo_buffer;
    VkDescriptorPool m_material_ssbo_descriptor_pool { VK_NULL_HANDLE };
    VkDescriptorSet m_material_ssbo_descriptor_set { VK_NULL_HANDLE };
    GraphBindings m_graph_bindings;

    u32 m_last_processed_scene_revision { 0 };

    void update_light_ubo(VulkanContext const& ctx);
    void update_material_ssbo(VulkanContext const& ctx);
    void update_shadow_descriptor_set(VkDevice device, RenderGraph& graph);

    VkDescriptorPool m_shadow_descriptor_pool { VK_NULL_HANDLE };
    VkDescriptorSet m_shadow_descriptor_set { VK_NULL_HANDLE };
    VkSampler m_shadow_sampler { VK_NULL_HANDLE };
    VkImageView m_last_shadow_view { VK_NULL_HANDLE };
    Mat4 m_shadow_view_projection;
    ShadowQuality m_shadow_quality { default_shadow_quality };
    bool m_has_depth_prepass { false };
    bool m_supports_multi_draw_indirect { false };

    VkDescriptorSet m_cluster_descriptor_set { VK_NULL_HANDLE };
    float m_cluster_near { 0.1f };
    float m_cluster_far  { 100.0f };
    u32   m_screen_w { 1280 };
    u32   m_screen_h { 720 };
};

}
