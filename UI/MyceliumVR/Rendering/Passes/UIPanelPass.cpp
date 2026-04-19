/*
 * MyceliumVR - a social VR platform built on the Ladybird browser engine
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "UIPanelPass.h"
#include "../Backend/VulkanDebug.h"

#if defined(TRACY_ENABLE)
#    include <tracy/Tracy.hpp>
#endif

namespace MyceliumVR {

UIPanelPass::UIPanelPass(VulkanPanelPipeline const& pipeline)
    : m_pipeline(pipeline)
{
}

ResourceState UIPanelPass::panel_vertex_buffer_initial_state() const
{
    if (m_panel_vertex_buffer.buffer == VK_NULL_HANDLE || has_panel_vertex_upload())
        return ResourceState::Undefined;
    return ResourceState::VertexBuffer;
}

ResourceState UIPanelPass::overlay_vertex_buffer_initial_state() const
{
    if (m_overlay_vertex_buffer.buffer == VK_NULL_HANDLE || has_overlay_vertex_upload())
        return ResourceState::Undefined;
    return ResourceState::VertexBuffer;
}

ResourceState UIPanelPass::panel_texture_initial_state() const
{
    if (m_panel_texture.image == VK_NULL_HANDLE || has_panel_texture_upload())
        return ResourceState::Undefined;
    return ResourceState::ShaderRead;
}

ResourceState UIPanelPass::overlay_texture_initial_state() const
{
    if (m_overlay_texture.image == VK_NULL_HANDLE || has_overlay_texture_upload())
        return ResourceState::Undefined;
    return ResourceState::ShaderRead;
}

bool UIPanelPass::has_panel_draw() const
{
    return m_panel_vertex_count > 0 && m_panel_texture.descriptor_set != VK_NULL_HANDLE;
}

bool UIPanelPass::has_overlay_draw() const
{
    return m_overlay_vertex_count > 0 && m_overlay_texture.descriptor_set != VK_NULL_HANDLE;
}

void UIPanelPass::destroy(VkDevice device, VmaAllocator allocator)
{
    m_panel_vertex_buffer.destroy(allocator);
    m_panel_vertex_staging.destroy(allocator);
    m_panel_texture.destroy(device, allocator);
    m_panel_staging_buffer.destroy(allocator);
    m_overlay_vertex_buffer.destroy(allocator);
    m_overlay_vertex_staging.destroy(allocator);
    m_overlay_texture.destroy(device, allocator);
    m_overlay_staging_buffer.destroy(allocator);
}

void UIPanelPass::set_panel_bitmap(Vector<u8> pixels, u32 w, u32 h)
{
    if (m_panel_pixels != pixels) {
        m_panel_pixels = move(pixels);
        m_panel_width = w;
        m_panel_height = h;
        m_panel_texture_dirty = true;
    }
}

void UIPanelPass::set_panel_bitmap_view(BitmapView view)
{
    m_panel_bitmap_view = view;
    m_panel_texture_dirty = true;
}

void UIPanelPass::clear_panel_bitmap()
{
    m_panel_pixels.clear();
    m_panel_width = m_panel_height = 0;
    m_panel_bitmap_view = {};
    m_panel_texture_dirty = true;
}

void UIPanelPass::set_overlay_view(OverlayView view)
{
    if (m_overlay_view.pixels != view.pixels) {
        m_overlay_view = move(view);
        m_overlay_texture_dirty = true;
    }
}

void UIPanelPass::set_overlay_bitmap_view(BitmapView view)
{
    if (m_overlay_bitmap_view.bitmap == view.bitmap && m_overlay_bitmap_view.width == view.width && m_overlay_bitmap_view.height == view.height)
        return;
    m_overlay_bitmap_view = view;
    m_overlay_texture_dirty = true;
}

void UIPanelPass::clear_overlay_bitmap()
{
    m_overlay_view = {};
    m_overlay_bitmap_view = {};
    m_overlay_texture_dirty = true;
}

ErrorOr<void> UIPanelPass::ensure_staging_buffer(VulkanContext const& ctx, VkDeviceSize size, VulkanBuffer& buffer)
{
    auto allocator = ctx.allocator();
    if (buffer.buffer != VK_NULL_HANDLE && buffer.size >= size) return {};
    buffer.destroy(allocator);
    VkDeviceSize grow_size = max(size, max(buffer.size * 2, static_cast<VkDeviceSize>(1 * 1024 * 1024)));
    TRY(VulkanResourceManager::create_buffer(allocator, grow_size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VMA_MEMORY_USAGE_CPU_ONLY, buffer));
    return {};
}

ErrorOr<void> UIPanelPass::ensure_texture_descriptor_set(VkDevice device, VkDescriptorSetLayout layout, VulkanTexture& texture)
{
    if (texture.descriptor_set != VK_NULL_HANDLE) return {};
    if (texture.image == VK_NULL_HANDLE || texture.view == VK_NULL_HANDLE || texture.sampler == VK_NULL_HANDLE || layout == VK_NULL_HANDLE)
        return {};

    if (texture.descriptor_pool == VK_NULL_HANDLE) {
        VkDescriptorPoolSize pool_size { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1 };
        VkDescriptorPoolCreateInfo pool_ci { VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO, nullptr, 0, 1, 1, &pool_size };
        TRY(check_vulkan_result(vkCreateDescriptorPool(device, &pool_ci, nullptr, &texture.descriptor_pool), "vkCreateDescriptorPool for texture"sv));
    }

    VkDescriptorSetAllocateInfo ds_ai { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO, nullptr, texture.descriptor_pool, 1, &layout };
    TRY(check_vulkan_result(vkAllocateDescriptorSets(device, &ds_ai, &texture.descriptor_set), "vkAllocateDescriptorSets for texture"sv));

    VkDescriptorImageInfo img_info { texture.sampler, texture.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
    VkWriteDescriptorSet write {
        .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
        .pNext = nullptr,
        .dstSet = texture.descriptor_set,
        .dstBinding = 0,
        .dstArrayElement = 0,
        .descriptorCount = 1,
        .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
        .pImageInfo = &img_info,
        .pBufferInfo = nullptr,
        .pTexelBufferView = nullptr
    };
    vkUpdateDescriptorSets(device, 1, &write, 0, nullptr);
    return {};
}

template<typename T>
static ErrorOr<VkDeviceSize> stage_vertex_buffer(VulkanContext const& ctx, Vector<T> const& data,
    VulkanBuffer& staging, VulkanBuffer& device_buf)
{
    if (data.is_empty()) return VkDeviceSize { 0 };
    VkDeviceSize size = data.size() * sizeof(T);
    auto allocator = ctx.allocator();
    if (staging.buffer == VK_NULL_HANDLE || staging.size < size) {
        staging.destroy(allocator);
        TRY(VulkanResourceManager::create_buffer(allocator, size,
            VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
            VMA_MEMORY_USAGE_CPU_ONLY, staging));
    }
    if (device_buf.buffer == VK_NULL_HANDLE || device_buf.size < size) {
        device_buf.destroy(allocator);
        TRY(VulkanResourceManager::create_buffer(allocator, size,
            VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
            VMA_MEMORY_USAGE_GPU_ONLY, device_buf));
    }
    void* mapped = nullptr;
    vmaMapMemory(allocator, staging.allocation, &mapped);
    memcpy(mapped, data.data(), size);
    vmaUnmapMemory(allocator, staging.allocation);
    return size;
}

ErrorOr<void> UIPanelPass::prepare(VulkanContext const& ctx, VkCommandPool)
{
    ZoneScoped;
#if defined(TRACY_ENABLE)
    TracyPlot("UIPanel/PanelTextureDirty", static_cast<int64_t>(m_panel_texture_dirty ? 1 : 0));
    TracyPlot("UIPanel/OverlayTextureDirty", static_cast<int64_t>(m_overlay_texture_dirty ? 1 : 0));
    TracyPlot("UIPanel/PanelVertexDirty", static_cast<int64_t>(m_panel_vertices_dirty ? 1 : 0));
    TracyPlot("UIPanel/PanelStagingBytes", static_cast<int64_t>(m_panel_staging_buffer.size));
    TracyPlot("UIPanel/OverlayStagingBytes", static_cast<int64_t>(m_overlay_staging_buffer.size));
    TracyPlot("UIPanel/PanelTextureWidth", static_cast<int64_t>(m_panel_texture.width));
    TracyPlot("UIPanel/PanelTextureHeight", static_cast<int64_t>(m_panel_texture.height));
    TracyPlot("UIPanel/OverlayTextureWidth", static_cast<int64_t>(m_overlay_texture.width));
    TracyPlot("UIPanel/OverlayTextureHeight", static_cast<int64_t>(m_overlay_texture.height));
#endif
    auto allocator = ctx.allocator();
    m_panel_texture_upload_pending = false;
    m_panel_vertices_upload_pending = false;
    m_overlay_texture_upload_pending = false;
    m_overlay_vertices_upload_pending = false;

    // Panel texture — copy CPU bitmap into staging buffer (no GPU commands yet).
    if (m_panel_texture_dirty) {
        ZoneScopedN("PanelTextureStage");
        u32 w = 0, h = 0;
        if (m_panel_bitmap_view.bitmap) {
            w = m_panel_bitmap_view.width; h = m_panel_bitmap_view.height;
        } else if (!m_panel_pixels.is_empty()) {
            w = m_panel_width; h = m_panel_height;
        }
        if (w > 0 && h > 0) {
#if defined(TRACY_ENABLE)
            TracyPlot("UIPanel/PanelUploadBytes", static_cast<int64_t>(w) * h * 4);
#endif
            bool newly_created = m_panel_texture.image == VK_NULL_HANDLE;
            TRY(VulkanResourceManager::ensure_texture_resource(ctx.device(), allocator, w, h, m_panel_texture));
            if (newly_created) {
                VulkanDebug::set_object_name(ctx.device(), VK_OBJECT_TYPE_IMAGE, (uint64_t)m_panel_texture.image, "UIPanelPass/PanelTexture"sv);
            }
            VkDeviceSize size = static_cast<VkDeviceSize>(w) * h * 4;
            TRY(ensure_staging_buffer(ctx, size, m_panel_staging_buffer));
            void* mapped = nullptr;
            vmaMapMemory(allocator, m_panel_staging_buffer.allocation, &mapped);
            if (m_panel_bitmap_view.bitmap)
                (void)VulkanResourceManager::copy_bitmap_to_mapped_staging(*m_panel_bitmap_view.bitmap, mapped, w, h);
            else
                memcpy(mapped, m_panel_pixels.data(), size);
            vmaUnmapMemory(allocator, m_panel_staging_buffer.allocation);
            TRY(ensure_texture_descriptor_set(ctx.device(), m_pipeline.layout, m_panel_texture));
            m_panel_texture_upload_pending = true;
        }
        m_panel_texture_dirty = false;
    }

    // Overlay texture — copy CPU bitmap into staging buffer.
    if (m_overlay_texture_dirty) {
        ZoneScopedN("OverlayTextureStage");
        u32 w = 0, h = 0;
        if (m_overlay_bitmap_view.bitmap) {
            w = m_overlay_bitmap_view.width; h = m_overlay_bitmap_view.height;
        } else if (!m_overlay_view.pixels.is_empty()) {
            w = m_overlay_view.width; h = m_overlay_view.height;
        }
        if (w > 0 && h > 0) {
#if defined(TRACY_ENABLE)
            TracyPlot("UIPanel/OverlayUploadBytes", static_cast<int64_t>(w) * h * 4);
#endif
            bool newly_created = m_overlay_texture.image == VK_NULL_HANDLE;
            TRY(VulkanResourceManager::ensure_texture_resource(ctx.device(), allocator, w, h, m_overlay_texture));
            if (newly_created) {
                VulkanDebug::set_object_name(ctx.device(), VK_OBJECT_TYPE_IMAGE, (uint64_t)m_overlay_texture.image, "UIPanelPass/OverlayTexture"sv);
            }
            VkDeviceSize size = static_cast<VkDeviceSize>(w) * h * 4;
            TRY(ensure_staging_buffer(ctx, size, m_overlay_staging_buffer));
            void* mapped = nullptr;
            vmaMapMemory(allocator, m_overlay_staging_buffer.allocation, &mapped);
            if (m_overlay_bitmap_view.bitmap)
                (void)VulkanResourceManager::copy_bitmap_to_mapped_staging(*m_overlay_bitmap_view.bitmap, mapped, w, h);
            else
                memcpy(mapped, m_overlay_view.pixels.data(), size);
            vmaUnmapMemory(allocator, m_overlay_staging_buffer.allocation);
            TRY(ensure_texture_descriptor_set(ctx.device(), m_pipeline.layout, m_overlay_texture));
            m_overlay_texture_upload_pending = true;

            // Overlay quad is a fixed fullscreen NDC quad — only rebuild once.
            if (m_overlay_vertex_count == 0) {
                auto vertices = VulkanMeshBuilder::build_overlay_vertices({});
                m_overlay_vertex_pending_size = TRY(stage_vertex_buffer(ctx, vertices, m_overlay_vertex_staging, m_overlay_vertex_buffer));
                m_overlay_vertex_count = static_cast<u32>(vertices.size());
                m_overlay_vertices_upload_pending = true;
            }
        } else {
            m_overlay_vertex_count = 0;
        }
        m_overlay_texture_dirty = false;
    }

    // Panel world-space vertices — only rebuild when the world layout changes.
    if (m_world && (m_panel_vertices_dirty || m_panel_vertex_buffer.buffer == VK_NULL_HANDLE)) {
        ZoneScopedN("PanelVertexStage");
        auto panel_vertices = VulkanMeshBuilder::build_panel_vertices(*m_world);
        if (!panel_vertices.is_empty()) {
            m_panel_vertex_pending_size = TRY(stage_vertex_buffer(ctx, panel_vertices, m_panel_vertex_staging, m_panel_vertex_buffer));
            m_panel_vertex_count = static_cast<u32>(panel_vertices.size());
            m_panel_vertices_upload_pending = true;
        } else {
            m_panel_vertex_count = 0;
        }
        m_panel_vertices_dirty = false;
    }

    return {};
}

void UIPanelPass::record_uploads(VkCommandBuffer cmd)
{
    bool any_transfer = m_panel_texture_upload_pending || m_overlay_texture_upload_pending
        || m_panel_vertices_upload_pending || m_overlay_vertices_upload_pending;
    if (!any_transfer) return;

    VulkanDebug::cmd_begin_label(cmd, "UIPanelUploads"sv, 0.2f, 0.8f, 0.8f);

    auto record_image_upload = [&](VulkanBuffer const& staging, VulkanTexture const& tex) {
        if (staging.buffer == VK_NULL_HANDLE || tex.image == VK_NULL_HANDLE) return;
        VulkanResourceManager::copy_buffer_to_image(cmd, staging.buffer, tex.image, tex.width, tex.height);
    };

    auto record_buffer_copy = [&](VulkanBuffer const& src, VulkanBuffer const& dst, VkDeviceSize size) {
        if (size == 0 || src.buffer == VK_NULL_HANDLE || dst.buffer == VK_NULL_HANDLE) return;
        VkBufferCopy region { .srcOffset = 0, .dstOffset = 0, .size = size };
        vkCmdCopyBuffer(cmd, src.buffer, dst.buffer, 1, &region);
    };

    if (m_panel_texture_upload_pending)
        record_image_upload(m_panel_staging_buffer, m_panel_texture);
    if (m_overlay_texture_upload_pending)
        record_image_upload(m_overlay_staging_buffer, m_overlay_texture);
    if (m_panel_vertices_upload_pending)
        record_buffer_copy(m_panel_vertex_staging, m_panel_vertex_buffer, m_panel_vertex_pending_size);
    if (m_overlay_vertices_upload_pending)
        record_buffer_copy(m_overlay_vertex_staging, m_overlay_vertex_buffer, m_overlay_vertex_pending_size);

    m_panel_texture_upload_pending = false;
    m_overlay_texture_upload_pending = false;
    m_panel_vertices_upload_pending = false;
    m_overlay_vertices_upload_pending = false;
    VulkanDebug::cmd_end_label(cmd); // UIPanelUploads
}

void UIPanelPass::setup(RenderPassBuilder& builder)
{
    bool const draws_panel = has_panel_draw();
    bool const draws_overlay = has_overlay_draw();
    if (!draws_panel && !draws_overlay)
        return;

    VERIFY(m_graph_bindings.color_target.value != 0);
    if (draws_panel) {
        VERIFY(m_graph_bindings.depth_target.value != 0);
        VERIFY(m_graph_bindings.panel_texture.value != 0);
        VERIFY(m_graph_bindings.panel_vertex_buffer.value != 0);
        builder.read_sampled(m_graph_bindings.panel_texture);
        builder.read_vertex(m_graph_bindings.panel_vertex_buffer);
    }
    if (draws_overlay) {
        VERIFY(m_graph_bindings.overlay_texture.value != 0);
        VERIFY(m_graph_bindings.overlay_vertex_buffer.value != 0);
        builder.read_sampled(m_graph_bindings.overlay_texture);
        builder.read_vertex(m_graph_bindings.overlay_vertex_buffer);
    }

    VkClearValue unused_clear {};
    builder.write_color(m_graph_bindings.color_target, { VK_ATTACHMENT_LOAD_OP_LOAD, VK_ATTACHMENT_STORE_OP_STORE, unused_clear });
    if (draws_panel)
        builder.write_depth(m_graph_bindings.depth_target, { VK_ATTACHMENT_LOAD_OP_LOAD, VK_ATTACHMENT_STORE_OP_STORE, unused_clear });
}

void UIPanelPass::execute(VkCommandBuffer cmd, RenderGraph& graph)
{
    ZoneScoped;
#if defined(TRACY_ENABLE)
    TracyVkZone(graph.tracy_context(), cmd, "UIPanelPass");
#endif
    execute(cmd, graph.render_extent(), graph.frame_push_constants());
}

void UIPanelPass::execute(VkCommandBuffer command_buffer, VkExtent2D extent, PushConstants const& pc)
{
    ZoneScoped;
    {
        ZoneScopedN("UIPanelPass/ViewportScissor");
        VkViewport viewport {
            .x = 0.0f,
            .y = 0.0f,
            .width = static_cast<float>(extent.width),
            .height = static_cast<float>(extent.height),
            .minDepth = 0.0f,
            .maxDepth = 1.0f,
        };
        VkRect2D scissor { { 0, 0 }, extent };
        vkCmdSetViewport(command_buffer, 0, 1, &viewport);
        vkCmdSetScissor(command_buffer, 0, 1, &scissor);
    }

    VkDeviceSize off[] = { 0 };
    if (m_panel_vertex_count > 0 && m_panel_texture.descriptor_set != VK_NULL_HANDLE) {
        ZoneScopedN("UIPanelPass/DrawPanel");
        vkCmdBindPipeline(command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipeline.pipeline);
        vkCmdPushConstants(command_buffer, m_pipeline.pipeline_layout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(PushConstants), &pc);
        vkCmdBindDescriptorSets(command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipeline.pipeline_layout, 0, 1, &m_panel_texture.descriptor_set, 0, nullptr);
        vkCmdBindVertexBuffers(command_buffer, 0, 1, &m_panel_vertex_buffer.buffer, off);
        vkCmdDraw(command_buffer, m_panel_vertex_count, 1, 0, 0);
    }

    if (m_overlay_vertex_count > 0 && m_overlay_texture.descriptor_set != VK_NULL_HANDLE) {
        ZoneScopedN("UIPanelPass/DrawOverlay");
        vkCmdBindPipeline(command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipeline.pipeline);
        PushConstants identity_pc {};
        // Identity for overlay NDC
        for (int i = 0; i < 16; ++i) identity_pc.view_projection[i] = (i%5==0) ? 1.0f : 0.0f;
        vkCmdPushConstants(command_buffer, m_pipeline.pipeline_layout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(PushConstants), &identity_pc);
        vkCmdBindDescriptorSets(command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipeline.pipeline_layout, 0, 1, &m_overlay_texture.descriptor_set, 0, nullptr);
        vkCmdBindVertexBuffers(command_buffer, 0, 1, &m_overlay_vertex_buffer.buffer, off);
        vkCmdDraw(command_buffer, m_overlay_vertex_count, 1, 0, 0);
    }
}

}
