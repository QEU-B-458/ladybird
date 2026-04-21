/*
 * MyceliumVR - a social VR platform built on the Ladybird browser engine
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include "../VulkanRenderPass.h"
#include "../Pipeline/VulkanPipeline.h"
#include "../Backend/VulkanResourceManager.h"
#include "../Pipeline/VulkanMeshBuilder.h"
#include <LibGfx/Bitmap.h>

namespace MyceliumVR {

struct BitmapView {
    Gfx::Bitmap const* bitmap { nullptr };
    u32 width { 0 };
    u32 height { 0 };
};

struct OverlayView {
    Vector<u8> pixels;
    u32 width { 0 };
    u32 height { 0 };
};

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

    void set_world(World const* world)
    {
        if (m_world != world) {
            m_world = world;
            m_panel_vertices_dirty = true;
        } else if (world && world->layout_dirty()) {
            m_panel_vertices_dirty = true;
        }
    }
    void set_panel_bitmap(Vector<u8> pixels, u32 w, u32 h);
    void set_panel_bitmap_view(BitmapView view);
    void clear_panel_bitmap();

    void set_overlay_view(OverlayView view);
    void set_overlay_bitmap_view(BitmapView view);
    void clear_overlay_bitmap();

    // Zero-copy Vulkan path: set a pre-rendered VkImage from WebContent instead of a CPU bitmap.
    // The image is owned by WebContentView; UIPanelPass only borrows it per-frame.
    void set_external_overlay_image(VkImage image, u32 width, u32 height);
    void clear_external_overlay();
    void set_graph_bindings(GraphBindings bindings) { m_graph_bindings = bindings; }
    VulkanBuffer const& panel_vertex_buffer() const { return m_panel_vertex_buffer; }
    VulkanBuffer const& overlay_vertex_buffer() const { return m_overlay_vertex_buffer; }
    VulkanBuffer const& panel_vertex_staging_buffer() const { return m_panel_vertex_staging; }
    VulkanBuffer const& overlay_vertex_staging_buffer() const { return m_overlay_vertex_staging; }
    VulkanBuffer const& panel_texture_staging_buffer() const { return m_panel_staging_buffer; }
    VulkanBuffer const& overlay_texture_staging_buffer() const { return m_overlay_staging_buffer; }
    VulkanTexture const& panel_texture() const { return m_panel_texture; }
    VulkanTexture const& overlay_texture() const { return m_overlay_texture; }
    bool has_panel_draw_work() const { return has_panel_draw(); }
    bool has_overlay_draw_work() const { return has_overlay_draw(); }
    bool has_upload_work() const { return m_panel_texture_upload_pending || m_overlay_texture_upload_pending || m_panel_vertices_upload_pending || m_overlay_vertices_upload_pending; }
    bool has_panel_texture_upload() const { return m_panel_texture_upload_pending; }
    bool has_overlay_texture_upload() const { return m_overlay_texture_upload_pending; }
    bool has_panel_vertex_upload() const { return m_panel_vertices_upload_pending; }
    bool has_overlay_vertex_upload() const { return m_overlay_vertices_upload_pending; }
    VkDeviceSize panel_vertex_upload_size() const { return m_panel_vertex_pending_size; }
    VkDeviceSize overlay_vertex_upload_size() const { return m_overlay_vertex_pending_size; }
    ResourceState panel_vertex_buffer_initial_state() const;
    ResourceState overlay_vertex_buffer_initial_state() const;
    ResourceState panel_texture_initial_state() const;
    ResourceState overlay_texture_initial_state() const;

private:
    VulkanPanelPipeline const& m_pipeline;
    World const* m_world { nullptr };

    VulkanBuffer m_panel_vertex_buffer;
    VulkanBuffer m_panel_vertex_staging;
    u32 m_panel_vertex_count { 0 };
    VkDeviceSize m_panel_vertex_pending_size { 0 };
    VulkanTexture m_panel_texture;
    bool m_panel_texture_dirty { false };
    bool m_panel_vertices_dirty { true }; // true on first frame to force initial build
    bool m_panel_texture_upload_pending { false };
    bool m_panel_vertices_upload_pending { false };
    VulkanBuffer m_panel_staging_buffer;

    VulkanBuffer m_overlay_vertex_buffer;
    VulkanBuffer m_overlay_vertex_staging;
    u32 m_overlay_vertex_count { 0 };
    VkDeviceSize m_overlay_vertex_pending_size { 0 };
    VulkanTexture m_overlay_texture;
    bool m_overlay_texture_dirty { false };
    bool m_overlay_texture_upload_pending { false };
    bool m_overlay_vertices_upload_pending { false };
    VulkanBuffer m_overlay_staging_buffer;

    BitmapView m_panel_bitmap_view;
    BitmapView m_overlay_bitmap_view;
    Vector<u8> m_panel_pixels;
    u32 m_panel_width { 0 };
    u32 m_panel_height { 0 };
    OverlayView m_overlay_view;
    GraphBindings m_graph_bindings;

    u32 m_graphics_queue_family { 0 };

    // External overlay VkImage (zero-copy Vulkan path).
    VkImage m_external_overlay_image { VK_NULL_HANDLE };
    VkImageView m_external_overlay_view { VK_NULL_HANDLE };
    VkSampler m_external_overlay_sampler { VK_NULL_HANDLE };
    VkDescriptorPool m_external_overlay_pool { VK_NULL_HANDLE };
    VkDescriptorSet m_external_overlay_set { VK_NULL_HANDLE };
    u32 m_external_overlay_width { 0 };
    u32 m_external_overlay_height { 0 };
    bool m_external_overlay_dirty { false };

    ErrorOr<void> ensure_staging_buffer(VulkanContext const& ctx, VkDeviceSize size, VulkanBuffer& buffer);
    ErrorOr<void> ensure_texture_descriptor_set(VkDevice device, VkDescriptorSetLayout layout, VulkanTexture& texture);
    bool has_panel_draw() const;
    bool has_overlay_draw() const;
};

}
