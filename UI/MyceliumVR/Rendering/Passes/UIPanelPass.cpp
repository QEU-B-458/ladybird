/*
 * MyceliumVR - a social VR platform built on the Ladybird browser engine
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "UIPanelPass.h"
#include "../VulkanCommon.h"
#include "../Backend/VulkanDebug.h"
#include "../../World/RenderSnapshot.h"

#include <UI/MyceliumVR/Support/Profiling.h>

namespace MyceliumVR {

static constexpr bool debug_ui_overlay_sync = true;

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

bool UIPanelPass::has_panel_draw() const
{
    if (m_panel_vertex_count == 0)
        return false;
    for (auto const& it : m_external_panel_images) {
        if (it.value.set != VK_NULL_HANDLE)
            return true;
    }
    return false;
}

bool UIPanelPass::has_overlay_draw() const
{
    return m_external_overlay_set != VK_NULL_HANDLE && m_overlay_vertex_count > 0;
}

void UIPanelPass::destroy(VkDevice device, VmaAllocator allocator)
{
    m_panel_vertex_buffer.destroy(allocator);
    m_panel_vertex_staging.destroy(allocator);

    for (auto& it : m_external_panel_images) {
        for (auto& binding : it.value.bindings) {
            if (binding.value.set != VK_NULL_HANDLE && m_external_overlay_pool != VK_NULL_HANDLE)
                vkFreeDescriptorSets(device, m_external_overlay_pool, 1, &binding.value.set);
            if (binding.value.view != VK_NULL_HANDLE)
                vkDestroyImageView(device, binding.value.view, nullptr);
        }
    }
    m_external_panel_images.clear();

    m_overlay_vertex_buffer.destroy(allocator);
    m_overlay_vertex_staging.destroy(allocator);

    if (m_external_overlay_pool != VK_NULL_HANDLE) {
        for (auto& entry : m_external_overlay_bindings) {
            if (entry.value.set != VK_NULL_HANDLE)
                vkFreeDescriptorSets(device, m_external_overlay_pool, 1, &entry.value.set);
            if (entry.value.view != VK_NULL_HANDLE)
                vkDestroyImageView(device, entry.value.view, nullptr);
        }
        m_external_overlay_bindings.clear();
        vkDestroyDescriptorPool(device, m_external_overlay_pool, nullptr);
    }
    if (m_external_overlay_sampler != VK_NULL_HANDLE)
        vkDestroySampler(device, m_external_overlay_sampler, nullptr);

    m_external_overlay_bound_image = VK_NULL_HANDLE;
    m_external_overlay_set = VK_NULL_HANDLE;
    m_external_overlay_pool = VK_NULL_HANDLE;
    m_external_overlay_view = VK_NULL_HANDLE;
    m_external_overlay_sampler = VK_NULL_HANDLE;
}

void UIPanelPass::set_external_panel_image(u32 handle, VkImage image, u32 width, u32 height)
{
    if (image != VK_NULL_HANDLE)
        m_dirty_external_panels.append({ handle, image, width, height });
}

void UIPanelPass::clear_external_panel_image(u32 handle)
{
    m_external_panel_images.remove(handle);
}

void UIPanelPass::set_external_overlay_image(VkImage image, u32 width, u32 height)
{
    if (m_external_overlay_image == image && m_external_overlay_width == width && m_external_overlay_height == height)
        return;
    m_external_overlay_image = image;
    m_external_overlay_width = width;
    m_external_overlay_height = height;
    m_external_overlay_dirty = true;
}

void UIPanelPass::clear_external_overlay()
{
    m_external_overlay_image = VK_NULL_HANDLE;
    m_external_overlay_width = 0;
    m_external_overlay_height = 0;
    m_external_overlay_dirty = true;
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

ErrorOr<void> UIPanelPass::prepare(VulkanContext const& ctx, VkCommandPool command_pool)
{
    ZoneScoped;
    (void)command_pool;
#if defined(TRACY_ENABLE)
    TracyPlot("UIPanel/PanelVertexDirty", static_cast<int64_t>(m_panel_vertices_dirty ? 1 : 0));
#endif
    m_graphics_queue_family = ctx.graphics_queue_family();

    m_panel_vertices_upload_pending = false;
    m_overlay_vertices_upload_pending = false;

    // Process dirty external panels (Zero-copy path)
    for (auto const& dirty : m_dirty_external_panels) {
        auto& panel = m_external_panel_images.ensure(dirty.handle);
        if (panel.image == dirty.image && panel.width == dirty.width && panel.height == dirty.height)
            continue;

        panel.image = dirty.image;
        panel.width = dirty.width;
        panel.height = dirty.height;

        if (m_external_overlay_sampler == VK_NULL_HANDLE) {
            VkSamplerCreateInfo sampler_ci {};
            sampler_ci.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
            sampler_ci.magFilter = VK_FILTER_LINEAR;
            sampler_ci.minFilter = VK_FILTER_LINEAR;
            sampler_ci.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
            sampler_ci.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
            sampler_ci.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
            sampler_ci.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
            sampler_ci.maxLod = 0.0f;
            vkCreateSampler(ctx.device(), &sampler_ci, nullptr, &m_external_overlay_sampler);
        }

        if (m_external_overlay_pool == VK_NULL_HANDLE) {
            VkDescriptorPoolSize pool_size { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 16 };
            VkDescriptorPoolCreateInfo pool_ci {};
            pool_ci.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
            pool_ci.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
            pool_ci.maxSets = 16;
            pool_ci.poolSizeCount = 1;
            pool_ci.pPoolSizes = &pool_size;
            vkCreateDescriptorPool(ctx.device(), &pool_ci, nullptr, &m_external_overlay_pool);
        }

        if (!panel.bindings.contains(panel.image) && panel.bindings.size() >= 2) {
            vkDeviceWaitIdle(ctx.device());
            for (auto& binding : panel.bindings) {
                if (binding.value.set != VK_NULL_HANDLE && m_external_overlay_pool != VK_NULL_HANDLE)
                    vkFreeDescriptorSets(ctx.device(), m_external_overlay_pool, 1, &binding.value.set);
                if (binding.value.view != VK_NULL_HANDLE)
                    vkDestroyImageView(ctx.device(), binding.value.view, nullptr);
            }
            panel.bindings.clear();
        }

        auto& binding = panel.bindings.ensure(panel.image);
        if (binding.view == VK_NULL_HANDLE || binding.set == VK_NULL_HANDLE) {
            VkImageViewCreateInfo view_ci {};
            view_ci.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
            view_ci.image = panel.image;
            view_ci.viewType = VK_IMAGE_VIEW_TYPE_2D;
            view_ci.format = VK_FORMAT_B8G8R8A8_UNORM;
            view_ci.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };

            if (vkCreateImageView(ctx.device(), &view_ci, nullptr, &binding.view) == VK_SUCCESS) {
                VkDescriptorSetAllocateInfo ds_ai {};
                ds_ai.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
                ds_ai.descriptorPool = m_external_overlay_pool;
                ds_ai.descriptorSetCount = 1;
                ds_ai.pSetLayouts = &m_pipeline.layout;
                if (vkAllocateDescriptorSets(ctx.device(), &ds_ai, &binding.set) == VK_SUCCESS) {
                    VkDescriptorImageInfo img_info { m_external_overlay_sampler, binding.view, VK_IMAGE_LAYOUT_GENERAL };
                    VkWriteDescriptorSet write {};
                    write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
                    write.dstSet = binding.set;
                    write.dstBinding = 0;
                    write.descriptorCount = 1;
                    write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
                    write.pImageInfo = &img_info;
                    vkUpdateDescriptorSets(ctx.device(), 1, &write, 0, nullptr);
                } else {
                    vkDestroyImageView(ctx.device(), binding.view, nullptr);
                    binding.view = VK_NULL_HANDLE;
                }
            }
        }

        panel.view = binding.view;
        panel.set = binding.set;
    }
    m_dirty_external_panels.clear();

    // External overlay VkImage — build VkImageView + descriptor set when the image changes.
    if (m_external_overlay_dirty && m_external_overlay_image != VK_NULL_HANDLE) {
        ZoneScopedN("ExternalOverlaySetup");
        VkDevice device = ctx.device();

        // Create sampler once (reuse across frames).
        if (m_external_overlay_sampler == VK_NULL_HANDLE) {
            VkSamplerCreateInfo sampler_ci {};
            sampler_ci.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
            sampler_ci.magFilter = VK_FILTER_LINEAR;
            sampler_ci.minFilter = VK_FILTER_LINEAR;
            sampler_ci.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
            sampler_ci.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
            sampler_ci.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
            sampler_ci.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
            sampler_ci.maxLod = 0.0f;
            if (vkCreateSampler(device, &sampler_ci, nullptr, &m_external_overlay_sampler) != VK_SUCCESS)
                dbgln("UIPanelPass: failed to create external overlay sampler");
        }

        if (m_external_overlay_sampler != VK_NULL_HANDLE && m_pipeline.layout != VK_NULL_HANDLE) {
            // Create / reset descriptor pool and allocate set.
            if (m_external_overlay_pool == VK_NULL_HANDLE) {
                VkDescriptorPoolSize pool_size { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 16 };
                VkDescriptorPoolCreateInfo pool_ci {};
                pool_ci.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
                pool_ci.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
                pool_ci.maxSets = 16;
                pool_ci.poolSizeCount = 1;
                pool_ci.pPoolSizes = &pool_size;
                vkCreateDescriptorPool(device, &pool_ci, nullptr, &m_external_overlay_pool);
            }

            // Keep the two alternating WebContent backing images resident instead of
            // destroying and recreating descriptor state every paint.
            if (!m_external_overlay_bindings.contains(m_external_overlay_image) && m_external_overlay_bindings.size() >= 2) {
                vkDeviceWaitIdle(device);
                for (auto& entry : m_external_overlay_bindings) {
                    if (entry.value.set != VK_NULL_HANDLE)
                        vkFreeDescriptorSets(device, m_external_overlay_pool, 1, &entry.value.set);
                    if (entry.value.view != VK_NULL_HANDLE)
                        vkDestroyImageView(device, entry.value.view, nullptr);
                }
                m_external_overlay_bindings.clear();
            }

            auto& binding = m_external_overlay_bindings.ensure(m_external_overlay_image);
            if (binding.view == VK_NULL_HANDLE || binding.set == VK_NULL_HANDLE) {
                binding.width = m_external_overlay_width;
                binding.height = m_external_overlay_height;

                VkImageViewCreateInfo view_ci {};
                view_ci.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
                view_ci.image = m_external_overlay_image;
                view_ci.viewType = VK_IMAGE_VIEW_TYPE_2D;
                view_ci.format = VK_FORMAT_B8G8R8A8_UNORM;
                view_ci.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
                if (vkCreateImageView(device, &view_ci, nullptr, &binding.view) != VK_SUCCESS) {
                    binding.view = VK_NULL_HANDLE;
                    dbgln("UIPanelPass: failed to create external overlay image view");
                }

                if (binding.view != VK_NULL_HANDLE) {
                    VkDescriptorSetAllocateInfo ds_ai {};
                    ds_ai.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
                    ds_ai.descriptorPool = m_external_overlay_pool;
                    ds_ai.descriptorSetCount = 1;
                    ds_ai.pSetLayouts = &m_pipeline.layout;
                    if (vkAllocateDescriptorSets(device, &ds_ai, &binding.set) == VK_SUCCESS) {
                        VkDescriptorImageInfo img_info { m_external_overlay_sampler, binding.view, VK_IMAGE_LAYOUT_GENERAL };
                        VkWriteDescriptorSet write {};
                        write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
                        write.dstSet = binding.set;
                        write.dstBinding = 0;
                        write.descriptorCount = 1;
                        write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
                        write.pImageInfo = &img_info;
                        vkUpdateDescriptorSets(device, 1, &write, 0, nullptr);
                    } else {
                        vkDestroyImageView(device, binding.view, nullptr);
                        binding.view = VK_NULL_HANDLE;
                    }
                }
            }

            if (auto it = m_external_overlay_bindings.find(m_external_overlay_image); it != m_external_overlay_bindings.end()) {
                m_external_overlay_bound_image = m_external_overlay_image;
                m_external_overlay_view = it->value.view;
                m_external_overlay_set = it->value.set;
            }

            // Ensure the fullscreen overlay quad vertices exist.
            if (m_overlay_vertex_count == 0) {
                auto vertices = VulkanMeshBuilder::build_overlay_vertices({});
                m_overlay_vertex_pending_size = TRY(stage_vertex_buffer(ctx, vertices, m_overlay_vertex_staging, m_overlay_vertex_buffer));
                m_overlay_vertex_count = static_cast<u32>(vertices.size());
                m_overlay_vertices_upload_pending = true;
            }
        }
        m_external_overlay_dirty = false;
    } else if (m_external_overlay_dirty) {
        m_external_overlay_bound_image = VK_NULL_HANDLE;
        m_external_overlay_set = VK_NULL_HANDLE;
        m_external_overlay_view = VK_NULL_HANDLE;
        m_external_overlay_dirty = false;
    }

    // Panel world-space vertices — only rebuild when the world layout changes.
    if (m_world || m_snapshot.size() >= sizeof(FrameHeader)) {
        bool needs_full_rebuild = m_panel_vertices_dirty || m_panel_vertex_buffer.buffer == VK_NULL_HANDLE;
        if (m_snapshot.size() >= sizeof(FrameHeader)) {
            auto const& header = *reinterpret_cast<FrameHeader const*>(m_snapshot.data());
            if (header.scene_revision != m_last_processed_scene_revision)
                needs_full_rebuild = true;
        }

        if (needs_full_rebuild) {
            ZoneScopedN("PanelVertexStage");
            Vector<TexturedVertex> panel_vertices;
            if (m_world) {
                panel_vertices = VulkanMeshBuilder::build_panel_vertices(*m_world);
            } else {
                auto const& header = *reinterpret_cast<FrameHeader const*>(m_snapshot.data());
                u8 const* ptr = m_snapshot.data() + sizeof(FrameHeader);
                ptr += sizeof(SnapshotCamera) * header.camera_count;
                ptr += sizeof(FrameDrawGroup) * header.draw_group_count;
                ptr += sizeof(SnapshotInstance) * header.instance_count;
                auto const* snapshot_panels = reinterpret_cast<SnapshotPanelEntry const*>(ptr);

                u32 panel_count = header.panel_count;
                StaticPanelEntry const* static_panels = nullptr;
                if (m_static_scene.size() >= sizeof(StaticSceneHeader)) {
                    auto const& static_header = *reinterpret_cast<StaticSceneHeader const*>(m_static_scene.data());
                    panel_count = static_header.panel_count;
                    static_panels = reinterpret_cast<StaticPanelEntry const*>(m_static_scene.data() + sizeof(StaticSceneHeader) + sizeof(StaticDrawGroup) * static_header.draw_group_count);
                }
                
                for (u32 i = 0; i < panel_count; ++i) {
                    float width = 1.0f, height = 1.0f;
                    u32 panel_handle = 0;

                    if (static_panels) {
                        width = static_panels[i].width;
                        height = static_panels[i].height;
                        panel_handle = static_panels[i].panel_handle;
                    } else {
                        panel_handle = snapshot_panels[i].panel_handle;
                        if (m_panel_handles) {
                            if (auto p = m_panel_handles->get(panel_handle); p.has_value()) {
                                width = p.value().width;
                                height = p.value().height;
                            }
                        }
                    }

                    // Find matching transform in snapshot_panels by handle
                    SnapshotPanelEntry const* snapshot_panel = nullptr;
                    for (u32 j = 0; j < header.panel_count; ++j) {
                        if (snapshot_panels[j].panel_handle == panel_handle) {
                            snapshot_panel = &snapshot_panels[j];
                            break;
                        }
                    }
                    
                    if (!snapshot_panel)
                        continue;

                    auto half_width = width * 0.5f;
                    auto half_height = height * 0.5f;
                    Array<Vec3, 4> corners {
                        Vec3 { -half_width, -half_height, 0.0f },
                        Vec3 { half_width, -half_height, 0.0f },
                        Vec3 { half_width, half_height, 0.0f },
                        Vec3 { -half_width, half_height, 0.0f },
                    };
                    
                    // Matrix is column-major mat4
                    auto transform_pos = [&](Vec3 const& v) {
                        return Vec3 {
                            v.x * snapshot_panel->transform[0] + v.y * snapshot_panel->transform[4] + v.z * snapshot_panel->transform[8] + snapshot_panel->transform[12],
                            v.x * snapshot_panel->transform[1] + v.y * snapshot_panel->transform[5] + v.z * snapshot_panel->transform[9] + snapshot_panel->transform[13],
                            v.x * snapshot_panel->transform[2] + v.y * snapshot_panel->transform[6] + v.z * snapshot_panel->transform[10] + snapshot_panel->transform[14]
                        };
                    };

                    for (auto& corner : corners) {
                        corner = transform_pos(corner);
                    }
                    
                    panel_vertices.append(TexturedVertex { .position { corners[0].x, corners[0].y, corners[0].z }, .uv { 0.0f, 1.0f } });
                    panel_vertices.append(TexturedVertex { .position { corners[1].x, corners[1].y, corners[1].z }, .uv { 1.0f, 1.0f } });
                    panel_vertices.append(TexturedVertex { .position { corners[2].x, corners[2].y, corners[2].z }, .uv { 1.0f, 0.0f } });
                    panel_vertices.append(TexturedVertex { .position { corners[0].x, corners[0].y, corners[0].z }, .uv { 0.0f, 1.0f } });
                    panel_vertices.append(TexturedVertex { .position { corners[2].x, corners[2].y, corners[2].z }, .uv { 1.0f, 0.0f } });
                    panel_vertices.append(TexturedVertex { .position { corners[3].x, corners[3].y, corners[3].z }, .uv { 0.0f, 0.0f } });
                }

                m_last_processed_scene_revision = header.scene_revision;
            }

            if (!panel_vertices.is_empty()) {
                m_panel_vertex_pending_size = TRY(stage_vertex_buffer(ctx, panel_vertices, m_panel_vertex_staging, m_panel_vertex_buffer));
                m_panel_vertex_count = static_cast<u32>(panel_vertices.size());
                m_panel_vertices_upload_pending = true;
            } else {
                m_panel_vertex_count = 0;
            }
            m_panel_vertices_dirty = false;
        }
    }

    return {};
}

void UIPanelPass::record_uploads(VkCommandBuffer cmd)
{
    if (!m_panel_vertices_upload_pending && !m_overlay_vertices_upload_pending)
        return;

    VulkanDebug::cmd_begin_label(cmd, "UIPanelUploads"sv, 0.2f, 0.8f, 0.8f);

    auto record_buffer_copy = [&](VulkanBuffer const& src, VulkanBuffer const& dst, VkDeviceSize size) {
        if (size == 0 || src.buffer == VK_NULL_HANDLE || dst.buffer == VK_NULL_HANDLE) return;
        VkBufferCopy region { .srcOffset = 0, .dstOffset = 0, .size = size };
        vkCmdCopyBuffer(cmd, src.buffer, dst.buffer, 1, &region);
    };

    if (m_panel_vertices_upload_pending)
        record_buffer_copy(m_panel_vertex_staging, m_panel_vertex_buffer, m_panel_vertex_pending_size);
    if (m_overlay_vertices_upload_pending)
        record_buffer_copy(m_overlay_vertex_staging, m_overlay_vertex_buffer, m_overlay_vertex_pending_size);

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
        VERIFY(m_graph_bindings.panel_vertex_buffer.value != 0);
        builder.read_vertex(m_graph_bindings.panel_vertex_buffer);
    }
    if (draws_overlay) {
        // External VkImages are not tracked by the render graph — barriers are manual in execute().
        VERIFY(m_graph_bindings.overlay_vertex_buffer.value != 0);
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
    if (m_panel_vertex_count > 0) {
        ZoneScopedN("UIPanelPass/DrawPanels");
        vkCmdBindPipeline(command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipeline.panel_pipeline);
        vkCmdPushConstants(command_buffer, m_pipeline.pipeline_layout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(PushConstants), &pc);
        vkCmdBindVertexBuffers(command_buffer, 0, 1, &m_panel_vertex_buffer.buffer, off);

        for (u32 i = 0; i < m_panel_vertex_count / 6; ++i) {
            u32 handle = 0;
            if (m_snapshot.size() >= sizeof(FrameHeader)) {
                auto const& header = *reinterpret_cast<FrameHeader const*>(m_snapshot.data());
                auto const* ptr = m_snapshot.data() + sizeof(FrameHeader);
                ptr += sizeof(SnapshotCamera) * header.camera_count;
                ptr += sizeof(FrameDrawGroup) * header.draw_group_count;
                ptr += sizeof(SnapshotInstance) * header.instance_count;
                auto const* snapshot_panels = reinterpret_cast<SnapshotPanelEntry const*>(ptr);
                if (i < header.panel_count)
                    handle = snapshot_panels[i].panel_handle;
            }

            if (handle != 0) {
                VkDescriptorSet panel_set = VK_NULL_HANDLE;
                if (auto ext_it = m_external_panel_images.find(handle); ext_it != m_external_panel_images.end()) {
                    panel_set = ext_it->value.set;
                    // Barrier for zero-copy image
                    if constexpr (debug_ui_overlay_sync) {
                        dbgln("UIPanelPass[frame={}]: acquire external panel image handle={} image={}",
                            m_debug_frame_id,
                            handle,
                            (void*)ext_it->value.image);
                    }
                    VkImageMemoryBarrier barrier {};
                    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
                    barrier.srcAccessMask = VK_ACCESS_MEMORY_WRITE_BIT;
                    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
                    barrier.oldLayout = VK_IMAGE_LAYOUT_GENERAL;
                    barrier.newLayout = VK_IMAGE_LAYOUT_GENERAL;
                    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_EXTERNAL;
                    barrier.dstQueueFamilyIndex = m_graphics_queue_family;
                    barrier.image = ext_it->value.image;
                    barrier.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
                    vkCmdPipelineBarrier(command_buffer, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);
                }

                if (panel_set != VK_NULL_HANDLE) {
                    vkCmdBindDescriptorSets(command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipeline.pipeline_layout, 0, 1, &panel_set, 0, nullptr);
                    vkCmdDraw(command_buffer, 6, 1, i * 6, 0);

                    if (auto ext_it = m_external_panel_images.find(handle); ext_it != m_external_panel_images.end()) {
                        if constexpr (debug_ui_overlay_sync) {
                            dbgln("UIPanelPass[frame={}]: release external panel image handle={} image={}",
                                m_debug_frame_id,
                                handle,
                                (void*)ext_it->value.image);
                        }
                        VkImageMemoryBarrier release_barrier {};
                        release_barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
                        release_barrier.srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
                        release_barrier.dstAccessMask = 0;
                        release_barrier.oldLayout = VK_IMAGE_LAYOUT_GENERAL;
                        release_barrier.newLayout = VK_IMAGE_LAYOUT_GENERAL;
                        release_barrier.srcQueueFamilyIndex = m_graphics_queue_family;
                        release_barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_EXTERNAL;
                        release_barrier.image = ext_it->value.image;
                        release_barrier.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
                        vkCmdPipelineBarrier(command_buffer,
                            VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                            VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
                            0, 0, nullptr, 0, nullptr, 1, &release_barrier);
                    }
                }
            }
        }
    }

    VkDescriptorSet overlay_set = m_external_overlay_set;

    if (m_overlay_vertex_count > 0 && overlay_set != VK_NULL_HANDLE) {
        ZoneScopedN("UIPanelPass/DrawOverlay");

        // For externally-imported images, make producer writes visible to sampling.
        if (m_external_overlay_set != VK_NULL_HANDLE && m_external_overlay_image != VK_NULL_HANDLE) {
            if constexpr (debug_ui_overlay_sync) {
                dbgln("UIPanelPass[frame={}]: acquire external overlay image={}",
                    m_debug_frame_id,
                    (void*)m_external_overlay_image);
            }
            VkImageMemoryBarrier acquire_barrier {};
            acquire_barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
            acquire_barrier.srcAccessMask = VK_ACCESS_MEMORY_WRITE_BIT;
            acquire_barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
            acquire_barrier.oldLayout = VK_IMAGE_LAYOUT_GENERAL;
            acquire_barrier.newLayout = VK_IMAGE_LAYOUT_GENERAL;
            acquire_barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_EXTERNAL;
            acquire_barrier.dstQueueFamilyIndex = m_graphics_queue_family;
            acquire_barrier.image = m_external_overlay_image;
            acquire_barrier.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
            vkCmdPipelineBarrier(command_buffer,
                VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
                VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                0, 0, nullptr, 0, nullptr, 1, &acquire_barrier);
        }

        vkCmdBindPipeline(command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipeline.overlay_pipeline);
        PushConstants identity_pc {};
        for (int i = 0; i < 16; ++i) identity_pc.view_projection[i] = (i % 5 == 0) ? 1.0f : 0.0f;
        vkCmdPushConstants(command_buffer, m_pipeline.pipeline_layout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(PushConstants), &identity_pc);
        vkCmdBindDescriptorSets(command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipeline.pipeline_layout, 0, 1, &overlay_set, 0, nullptr);
        vkCmdBindVertexBuffers(command_buffer, 0, 1, &m_overlay_vertex_buffer.buffer, off);
        if constexpr (debug_ui_overlay_sync) {
            dbgln("UIPanelPass[frame={}]: draw overlay image={} vertex_count={}",
                m_debug_frame_id,
                (void*)m_external_overlay_image,
                m_overlay_vertex_count);
        }
        vkCmdDraw(command_buffer, m_overlay_vertex_count, 1, 0, 0);

        if (m_external_overlay_set != VK_NULL_HANDLE && m_external_overlay_image != VK_NULL_HANDLE) {
            if constexpr (debug_ui_overlay_sync) {
                dbgln("UIPanelPass[frame={}]: release external overlay image={}",
                    m_debug_frame_id,
                    (void*)m_external_overlay_image);
            }
            VkImageMemoryBarrier release_barrier {};
            release_barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
            release_barrier.srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
            release_barrier.dstAccessMask = 0;
            release_barrier.oldLayout = VK_IMAGE_LAYOUT_GENERAL;
            release_barrier.newLayout = VK_IMAGE_LAYOUT_GENERAL;
            release_barrier.srcQueueFamilyIndex = m_graphics_queue_family;
            release_barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_EXTERNAL;
            release_barrier.image = m_external_overlay_image;
            release_barrier.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
            vkCmdPipelineBarrier(command_buffer,
                VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
                0, 0, nullptr, 0, nullptr, 1, &release_barrier);
        }
    }
}

}
