/*
 * MyceliumVR - a social VR platform built on the Ladybird browser engine
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include "../VulkanRenderPass.h"
#include "../Pipeline/VulkanPipeline.h"
#include "../Backend/VulkanResourceManager.h"
#include "../Pipeline/VulkanMeshBuilder.h"

namespace MyceliumVR {

class UIPanelPass : public VulkanRenderPass {
public:
    struct GraphBindings {
        TextureHandle color_target {};
        TextureHandle depth_target {};
        TextureHandle panel_texture {};
        TextureHandle overlay_texture {};
        BufferHandle panel_vertex_buffer {};
        BufferHandle overlay_vertex_buffer {};
    };

    UIPanelPass(VulkanPanelPipeline const& pipeline);
    virtual ~UIPanelPass() override = default;

    virtual ErrorOr<void> prepare(VulkanContext const& ctx, VkCommandPool command_pool) override;
    virtual void record_uploads(VkCommandBuffer command_buffer) override;
    virtual void execute(VkCommandBuffer command_buffer, VkExtent2D extent, PushConstants const& pc) override;
    virtual void execute(VkCommandBuffer command_buffer, RenderGraph& graph) override;
    virtual void setup(RenderPassBuilder&) override;
    virtual String name() const override { return MUST(String::from_utf8("UIPanel"sv)); }
    virtual void destroy(VkDevice device, VmaAllocator allocator) override;

    struct RegisteredPanel {
        u32 entity_id;
        ByteString url;
        float width;
        float height;
    };

    void set_world(World const* world)
    {
        if (m_world != world) {
            m_world = world;
            m_panel_vertices_dirty = true;
            m_snapshot = {};
            m_static_scene = {};
        } else if (world && world->layout_dirty()) {
            m_panel_vertices_dirty = true;
        }
    }
    void set_snapshot(ReadonlySpan<u8> snapshot) { m_snapshot = snapshot; m_world = nullptr; }
    void set_static_scene(ReadonlySpan<u8> static_scene) { m_static_scene = static_scene; }
    void set_panel_handles(HashMap<u32, RegisteredPanel> const* handles) { m_panel_handles = handles; }
    void set_external_panel_image(u32 handle, VkImage image, u32 width, u32 height);
    void clear_external_panel_image(u32 handle);

    // Zero-copy Vulkan path: set a pre-rendered VkImage from WebContent.
    // The image is owned by WebContentView; UIPanelPass only borrows it per-frame.
    void set_external_overlay_image(VkImage image, u32 width, u32 height);
    void clear_external_overlay();
    VkImage external_overlay_image() const { return m_external_overlay_image; }
    void set_debug_frame_id(u64 frame_id) { m_debug_frame_id = frame_id; }
    void set_graph_bindings(GraphBindings bindings) { m_graph_bindings = bindings; }
    VulkanBuffer const& panel_vertex_buffer() const { return m_panel_vertex_buffer; }
    VulkanBuffer const& overlay_vertex_buffer() const { return m_overlay_vertex_buffer; }
    VulkanBuffer const& panel_vertex_staging_buffer() const { return m_panel_vertex_staging; }
    VulkanBuffer const& overlay_vertex_staging_buffer() const { return m_overlay_vertex_staging; }
    bool has_panel_draw_work() const { return has_panel_draw(); }
    bool has_overlay_draw_work() const { return has_overlay_draw(); }
    bool has_upload_work() const { return m_panel_vertices_upload_pending || m_overlay_vertices_upload_pending; }
    bool has_panel_vertex_upload() const { return m_panel_vertices_upload_pending; }
    bool has_overlay_vertex_upload() const { return m_overlay_vertices_upload_pending; }
    VkDeviceSize panel_vertex_upload_size() const { return m_panel_vertex_pending_size; }
    VkDeviceSize overlay_vertex_upload_size() const { return m_overlay_vertex_pending_size; }
    ResourceState panel_vertex_buffer_initial_state() const;
    ResourceState overlay_vertex_buffer_initial_state() const;

private:
    VulkanPanelPipeline const& m_pipeline;
    World const* m_world { nullptr };
    ReadonlySpan<u8> m_snapshot;
    ReadonlySpan<u8> m_static_scene;
    HashMap<u32, RegisteredPanel> const* m_panel_handles { nullptr };

    VulkanBuffer m_panel_vertex_buffer;
    VulkanBuffer m_panel_vertex_staging;
    u32 m_panel_vertex_count { 0 };
    VkDeviceSize m_panel_vertex_pending_size { 0 };

    struct ExternalPanel {
        u32 width { 0 };
        u32 height { 0 };
        struct Binding {
            VkImageView view { VK_NULL_HANDLE };
            VkDescriptorSet set { VK_NULL_HANDLE };
        };
        VkImage image { VK_NULL_HANDLE };
        VkImageView view { VK_NULL_HANDLE };
        VkDescriptorSet set { VK_NULL_HANDLE };
        HashMap<VkImage, Binding> bindings;
    };
    HashMap<u32, ExternalPanel> m_external_panel_images;
    struct DirtyExternalPanel {
        u32 handle;
        VkImage image;
        u32 width;
        u32 height;
    };
    Vector<DirtyExternalPanel> m_dirty_external_panels;

    bool m_panel_vertices_dirty { true }; // true on first frame to force initial build
    bool m_panel_vertices_upload_pending { false };

    VulkanBuffer m_overlay_vertex_buffer;
    VulkanBuffer m_overlay_vertex_staging;
    u32 m_overlay_vertex_count { 0 };
    VkDeviceSize m_overlay_vertex_pending_size { 0 };
    bool m_overlay_vertices_upload_pending { false };

    GraphBindings m_graph_bindings;

    u32 m_graphics_queue_family { 0 };
    u32 m_last_processed_scene_revision { 0 };

    // External overlay VkImage (zero-copy Vulkan path).
    VkImage m_external_overlay_image { VK_NULL_HANDLE };
    VkSampler m_external_overlay_sampler { VK_NULL_HANDLE };
    VkDescriptorPool m_external_overlay_pool { VK_NULL_HANDLE };
    struct ExternalOverlayBinding {
        VkImageView view { VK_NULL_HANDLE };
        VkDescriptorSet set { VK_NULL_HANDLE };
        u32 width { 0 };
        u32 height { 0 };
    };
    HashMap<VkImage, ExternalOverlayBinding> m_external_overlay_bindings;
    VkImage m_external_overlay_bound_image { VK_NULL_HANDLE };
    VkImageView m_external_overlay_view { VK_NULL_HANDLE };
    VkDescriptorSet m_external_overlay_set { VK_NULL_HANDLE };
    u32 m_external_overlay_width { 0 };
    u32 m_external_overlay_height { 0 };
    bool m_external_overlay_dirty { false };
    u64 m_debug_frame_id { 0 };

    bool has_panel_draw() const;
    bool has_overlay_draw() const;
};

}
