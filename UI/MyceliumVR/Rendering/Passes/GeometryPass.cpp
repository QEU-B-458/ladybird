/*
 * MyceliumVR - a social VR platform built on the Ladybird browser engine
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "GeometryPass.h"
#include "../Backend/VulkanDebug.h"
#include "../Pipeline/VulkanPBRManager.h"
#include "../../World/RenderSnapshot.h"

#include <UI/MyceliumVR/Support/Profiling.h>

namespace MyceliumVR {

GeometryPass::GeometryPass(VulkanWorldPipeline const& pipeline, MaterialLibrary& materials, MeshLibrary& meshes, TextureLibrary& textures, VirtualFileSystem const* file_system)
    : m_pipeline(pipeline)
    , m_material_library(materials)
    , m_mesh_library(meshes)
    , m_texture_library(textures)
    , m_file_system(file_system)
{
}

ResourceState GeometryPass::vertex_buffer_initial_state() const
{
    if (m_vertex_buffer.buffer == VK_NULL_HANDLE || has_vertex_upload())
        return ResourceState::Undefined;
    return ResourceState::VertexBuffer;
}

ResourceState GeometryPass::instance_buffer_initial_state() const
{
    if (m_instance_buffer.buffer == VK_NULL_HANDLE || has_instance_upload())
        return ResourceState::Undefined;
    return ResourceState::VertexBuffer;
}

ResourceState GeometryPass::indirect_live_buffer_initial_state() const
{
    if (m_indirect_buffer.buffer == VK_NULL_HANDLE)
        return ResourceState::Undefined;
    return ResourceState::IndirectBuffer;
}

ResourceState GeometryPass::indirect_ref_buffer_initial_state() const
{
    if (m_indirect_ref_buffer.buffer == VK_NULL_HANDLE || has_indirect_ref_upload())
        return ResourceState::Undefined;
    return ResourceState::StorageRead;
}

ResourceState GeometryPass::aabb_buffer_initial_state() const
{
    if (m_aabb_world_buffer.buffer == VK_NULL_HANDLE || has_aabb_upload())
        return ResourceState::Undefined;
    return ResourceState::StorageRead;
}

void GeometryPass::destroy(VkDevice device, VmaAllocator allocator)
{
    m_vertex_buffer.destroy(allocator);
    m_instance_buffer.destroy(allocator);
    m_indirect_buffer.destroy(allocator);
    m_indirect_ref_buffer.destroy(allocator);
    m_aabb_world_buffer.destroy(allocator);
    m_vertex_staging.destroy(allocator);
    m_instance_staging.destroy(allocator);
    m_indirect_staging.destroy(allocator);
    m_aabb_world_staging.destroy(allocator);
    
    m_bindless_set.destroy(device);

    m_light_ubo_buffer.destroy(allocator);
    if (m_light_ubo_descriptor_pool != VK_NULL_HANDLE) vkDestroyDescriptorPool(device, m_light_ubo_descriptor_pool, nullptr);
    m_material_ssbo_buffer.destroy(allocator);
    if (m_material_ssbo_descriptor_pool != VK_NULL_HANDLE) vkDestroyDescriptorPool(device, m_material_ssbo_descriptor_pool, nullptr);
    if (m_shadow_descriptor_pool != VK_NULL_HANDLE) vkDestroyDescriptorPool(device, m_shadow_descriptor_pool, nullptr);
    if (m_shadow_sampler != VK_NULL_HANDLE) vkDestroySampler(device, m_shadow_sampler, nullptr);
    m_last_shadow_view = VK_NULL_HANDLE;
}

// Stage buffer data to a persistent host-visible staging buffer and ensure
// the device-local destination buffer exists. No GPU commands are issued here —
// the actual vkCmdCopyBuffer is recorded later in record_uploads().
template<typename T>
static ErrorOr<VkDeviceSize> stage_buffer(VulkanContext const& ctx, Vector<T> const& data,
    VulkanBuffer& staging, VulkanBuffer& device_buf, VkBufferUsageFlags usage)
{
    if (data.is_empty()) return VkDeviceSize { 0 };
    VkDeviceSize size = data.size() * sizeof(T);
    auto allocator = ctx.allocator();

    if (staging.buffer == VK_NULL_HANDLE || staging.size < size) {
        staging.destroy(allocator);
        VkDeviceSize grow_size = max(size, max(staging.size * 2, static_cast<VkDeviceSize>(1 * 1024 * 1024)));
        TRY(VulkanResourceManager::create_buffer(allocator, grow_size,
            VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
            VMA_MEMORY_USAGE_CPU_ONLY, staging));
    }
    if (device_buf.buffer == VK_NULL_HANDLE || device_buf.size < size) {
        device_buf.destroy(allocator);
        VkDeviceSize grow_size = max(size, max(device_buf.size * 2, static_cast<VkDeviceSize>(1 * 1024 * 1024)));
        TRY(VulkanResourceManager::create_buffer(allocator, grow_size,
            VK_BUFFER_USAGE_TRANSFER_DST_BIT | usage,
            VMA_MEMORY_USAGE_GPU_ONLY, device_buf));
    }

    void* mapped = nullptr;
    vmaMapMemory(allocator, staging.allocation, &mapped);
    memcpy(mapped, data.data(), size);
    vmaUnmapMemory(allocator, staging.allocation);
    return size;
}

ErrorOr<void> GeometryPass::prepare(VulkanContext const& ctx, VkCommandPool)
{
    ZoneScoped;
    m_supports_multi_draw_indirect = ctx.supports_multi_draw_indirect();
    Vector<Vertex> vertices;
    Vector<GPUInstanceData> instance_data;
    Vector<VkDrawIndirectCommand> indirect_cmds;
    Vector<GroupAABBGPU> aabb_data;
    bool world_dirty = false, instances_dirty = false;

    if (m_snapshot.size() >= sizeof(FrameHeader)) {
        auto const& header = *reinterpret_cast<FrameHeader const*>(m_snapshot.data());
        
        u8 const* snapshot_ptr = m_snapshot.data() + sizeof(FrameHeader);
        auto const* cameras = reinterpret_cast<SnapshotCamera const*>(snapshot_ptr); snapshot_ptr += sizeof(SnapshotCamera) * header.camera_count;
        auto const* frame_groups = reinterpret_cast<FrameDrawGroup const*>(snapshot_ptr); snapshot_ptr += sizeof(FrameDrawGroup) * header.draw_group_count;
        auto const* snapshot_instances = reinterpret_cast<SnapshotInstance const*>(snapshot_ptr); snapshot_ptr += sizeof(SnapshotInstance) * header.instance_count;
        auto const* snapshot_panels = reinterpret_cast<SnapshotPanelEntry const*>(snapshot_ptr); snapshot_ptr += sizeof(SnapshotPanelEntry) * header.panel_count;
        snapshot_ptr += sizeof(SnapshotLightEntry) * header.point_light_count;

        if (header.camera_count > 0 && header.primary_camera_idx < header.camera_count) {
            auto const& cam = cameras[header.primary_camera_idx];
            memcpy(m_camera_position, cam.position, sizeof(float) * 3);
        }

        u32 draw_group_count = header.draw_group_count;
        StaticDrawGroup const* static_groups = nullptr;

        bool needs_full_rebuild = (header.scene_revision != m_last_processed_scene_revision) || (m_vertex_buffer.buffer == VK_NULL_HANDLE);

        if (needs_full_rebuild) {
            vertices.clear();
            m_draw_groups.clear();
            if (m_static_scene.size() >= sizeof(StaticSceneHeader)) {
                auto const& static_header = *reinterpret_cast<StaticSceneHeader const*>(m_static_scene.data());
                draw_group_count = static_header.draw_group_count;
                static_groups = reinterpret_cast<StaticDrawGroup const*>(m_static_scene.data() + sizeof(StaticSceneHeader));
                
                outln("GeometryPass: Rebuilding scene at revision {} ({} groups)", header.scene_revision, draw_group_count);
                m_draw_groups.clear();

                for (u32 i = 0; i < draw_group_count; ++i) {
                    auto const& sg = static_groups[i];
                    u32 mesh_handle = sg.mesh_handle;
                    u32 material_handle = sg.material_handle;
                    u8 alpha_mode = sg.alpha_mode;
                    u8 cull_mode = sg.cull_mode;
                    u8 has_normal_map = sg.has_normal_map;
                    u32 sg_instance_count = 0;
                    u32 sg_instance_offset = 0;

                    if (i < header.draw_group_count) {
                        sg_instance_count = frame_groups[i].instance_count;
                        sg_instance_offset = frame_groups[i].instance_offset;
                    }

                    DrawGroup group;
                    group.mesh_name = "cube"_string;
                    if (m_mesh_handles) {
                        if (auto mesh_path = m_mesh_handles->get(mesh_handle); mesh_path.has_value())
                            group.mesh_name = MUST(String::from_utf8(mesh_path.value().view()));
                    }

                    group.material = "default"_string;
                    if (m_material_handles) {
                        if (auto mat_path = m_material_handles->get(material_handle); mat_path.has_value())
                            group.material = MUST(String::from_utf8(mat_path.value().view()));
                    }

                    group.alpha_mode = alpha_mode;
                    group.cull_mode = static_cast<DrawGroup::CullMode>(cull_mode);
                    group.has_normal_map = has_normal_map != 0;

                    auto mesh_result = m_mesh_library.resolve_mesh(group.mesh_name);
                    auto const* mesh_asset = mesh_result.is_error() ? m_mesh_library.resolve_mesh("cube"_string).value() : mesh_result.value();

                    group.vertex_count = static_cast<u32>(mesh_asset->positions.size() / 3);
                    group.first_vertex = static_cast<u32>(vertices.size());

                    auto const& positions = mesh_asset->positions;
                    auto const& normals_arr = mesh_asset->normals;
                    auto const& texcoords = mesh_asset->texcoords;
                    auto const& tangents_arr = mesh_asset->tangents;
                    bool const has_normals = !normals_arr.is_empty();
                    bool const has_texcoords = !texcoords.is_empty();
                    bool const has_tangents = !tangents_arr.is_empty();

                    memcpy(group.aabb_local_min, sg.aabb_local_min, sizeof(group.aabb_local_min));
                    memcpy(group.aabb_local_max, sg.aabb_local_max, sizeof(group.aabb_local_max));

                    for (size_t tri = 0; tri < static_cast<size_t>(group.vertex_count); tri += 3) {
                        Vec3 face_normal { 0.0f, 0.0f, 1.0f };
                        if (!has_normals) {
                            auto b0 = tri * 3, b1 = (tri + 1) * 3, b2 = (tri + 2) * 3;
                            Vec3 v0 { positions[b0], positions[b0 + 1], positions[b0 + 2] };
                            Vec3 v1 { positions[b1], positions[b1 + 1], positions[b1 + 2] };
                            Vec3 v2 { positions[b2], positions[b2 + 1], positions[b2 + 2] };
                            face_normal = normalize(cross(subtract(v1, v0), subtract(v2, v0)));
                        }
                        for (int v = 0; v < 3; ++v) {
                            auto bi3 = (tri + static_cast<size_t>(v)) * 3;
                            auto bi2 = (tri + static_cast<size_t>(v)) * 2;
                            auto bi4 = (tri + static_cast<size_t>(v)) * 4;
                            Vertex vertex {};
                            vertex.position[0] = positions[bi3]; vertex.position[1] = positions[bi3 + 1]; vertex.position[2] = positions[bi3 + 2];
                            vertex.color[0] = 1.0f; vertex.color[1] = 1.0f; vertex.color[2] = 1.0f;
                            if (has_normals) {
                                vertex.normal[0] = normals_arr[bi3]; vertex.normal[1] = normals_arr[bi3 + 1]; vertex.normal[2] = normals_arr[bi3 + 2];
                            } else {
                                vertex.normal[0] = face_normal.x; vertex.normal[1] = face_normal.y; vertex.normal[2] = face_normal.z;
                            }
                            if (has_texcoords) {
                                vertex.uv[0] = texcoords[bi2]; vertex.uv[1] = texcoords[bi2 + 1];
                            }
                            if (has_tangents) {
                                vertex.tangent[0] = tangents_arr[bi4]; vertex.tangent[1] = tangents_arr[bi4 + 1]; vertex.tangent[2] = tangents_arr[bi4 + 2]; vertex.tangent[3] = tangents_arr[bi4 + 3];
                            }
                            vertices.append(vertex);
                        }
                    }

                    group.first_instance = static_cast<u32>(instance_data.size());
                    group.instance_count = sg_instance_count;
                    group.material_index = i;
                    group.aabb_world_min[0] = -1e6f; group.aabb_world_min[1] = -1e6f; group.aabb_world_min[2] = -1e6f;
                    group.aabb_world_max[0] = 1e6f;  group.aabb_world_max[1] = 1e6f;  group.aabb_world_max[2] = 1e6f;

                    for (u32 j = 0; j < sg_instance_count; ++j) {
                        auto const& si = snapshot_instances[sg_instance_offset + j];
                        GPUInstanceData idata;
                        memcpy(idata.world_matrix, si.transform, sizeof(float) * 16);
                        idata.material_index = i;
                        instance_data.append(idata);
                    }

                    m_draw_groups.append(group);
                    indirect_cmds.append({ group.vertex_count, group.instance_count, group.first_vertex, static_cast<u32>(group.first_instance) });
                    GroupAABBGPU aabb;
                    memcpy(aabb.min_xyz, group.aabb_world_min, 12); aabb.min_xyz[3] = 0.0f;
                    memcpy(aabb.max_xyz, group.aabb_world_max, 12); aabb.max_xyz[3] = 0.0f;
                    aabb_data.append(aabb);
                }
                m_last_processed_scene_revision = header.scene_revision;
                world_dirty = true;
            } else if (m_draw_groups.is_empty()) {
                // First frame but no static scene? Fallback to probe.
                needs_full_rebuild = false; // Trigger fallback below
            } else {
                // Needs rebuild but no static scene? Keep old draw groups but update instances anyway.
                needs_full_rebuild = false;
            }
        }
        
        if (!needs_full_rebuild) {
            // Incremental update: transforms and instance counts only
            for (u32 i = 0; i < m_draw_groups.size(); ++i) {
                auto& group = m_draw_groups[i];
                u32 sg_instance_count = 0;
                u32 sg_instance_offset = 0;
                
                if (i < header.draw_group_count) {
                    sg_instance_count = frame_groups[i].instance_count;
                    sg_instance_offset = frame_groups[i].instance_offset;
                }

                group.first_instance = static_cast<u32>(instance_data.size());
                group.instance_count = sg_instance_count;

                for (u32 j = 0; j < sg_instance_count; ++j) {
                    auto const& si = snapshot_instances[sg_instance_offset + j];
                    GPUInstanceData idata;
                    memcpy(idata.world_matrix, si.transform, sizeof(float) * 16);
                    idata.material_index = i;
                    instance_data.append(idata);
                }

                indirect_cmds.append({ group.vertex_count, group.instance_count, group.first_vertex, static_cast<u32>(group.first_instance) });
                GroupAABBGPU aabb;
                memcpy(aabb.min_xyz, group.aabb_world_min, 12); aabb.min_xyz[3] = 0.0f;
                memcpy(aabb.max_xyz, group.aabb_world_max, 12); aabb.max_xyz[3] = 0.0f;
                aabb_data.append(aabb);
            }
            instances_dirty = true;
        }

        (void)snapshot_panels; // Suppress unused warning for now

        bool snapshot_has_geometry = (world_dirty || instances_dirty) ? !m_draw_groups.is_empty() : true;
        if (world_dirty && !snapshot_has_geometry) {
            vertices = VulkanMeshBuilder::build_probe_vertices();
            GPUInstanceData identity {};
            static constexpr Array<float, 16> identity_mat { 1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1 };
            memcpy(identity.world_matrix, identity_mat.data(), 64);
            identity.material_index = 0;
            instance_data.append(identity);

            DrawGroup probe_group;
            probe_group.mesh_name = "cube"_string;
            probe_group.material = "default"_string;
            probe_group.vertex_count = static_cast<u32>(vertices.size());
            probe_group.first_vertex = 0;
            probe_group.instance_count = 1;
            probe_group.first_instance = 0;
            probe_group.material_index = 0;
            probe_group.alpha_mode = 0;
            probe_group.cull_mode = DrawGroup::CullMode::Back;
            probe_group.has_normal_map = false;
            m_draw_groups.append(probe_group);
            
            indirect_cmds.append({ probe_group.vertex_count, 1, 0, 0 });
            GroupAABBGPU aabb;
            for (int i = 0; i < 3; i++) { aabb.min_xyz[i] = -1.0f; aabb.max_xyz[i] = 1.0f; }
            aabb.min_xyz[3] = 0.0f; aabb.max_xyz[3] = 0.0f;
            aabb_data.append(aabb);
            
            world_dirty = true;
        }
    } else if (!m_world) {
        vertices = VulkanMeshBuilder::build_probe_vertices();
        GPUInstanceData identity {};
        static constexpr Array<float, 16> identity_mat { 1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1 };
        memcpy(identity.world_matrix, identity_mat.data(), 64);
        identity.material_index = 0;
        instance_data.append(identity);

        DrawGroup probe_group;
        probe_group.mesh_name = "cube"_string;
        probe_group.material = "default"_string;
        probe_group.vertex_count = static_cast<u32>(vertices.size());
        probe_group.first_vertex = 0;
        probe_group.instance_count = 1;
        probe_group.first_instance = 0;
        probe_group.material_index = 0;
        probe_group.alpha_mode = 0;
        probe_group.cull_mode = DrawGroup::CullMode::Back;
        probe_group.has_normal_map = false;
        m_draw_groups.clear();
        m_draw_groups.append(probe_group);

        indirect_cmds.append({ probe_group.vertex_count, 1, 0, 0 });
        GroupAABBGPU aabb;
        for (int i = 0; i < 3; i++) { aabb.min_xyz[i] = -1.0f; aabb.max_xyz[i] = 1.0f; }
        aabb.min_xyz[3] = 0.0f; aabb.max_xyz[3] = 0.0f;
        aabb_data.append(aabb);

        world_dirty = true;
    }
 else if (m_world->layout_dirty() || m_vertex_buffer.buffer == VK_NULL_HANDLE) {
        m_draw_groups.clear();
        VulkanMeshBuilder::build_world_instanced(*m_world, m_mesh_library, vertices, instance_data, m_draw_groups);
        for (auto const& g : m_draw_groups) {
            indirect_cmds.append({ g.vertex_count, g.instance_count, g.first_vertex, g.first_instance });
            GroupAABBGPU aabb;
            memcpy(aabb.min_xyz, g.aabb_world_min, 12); aabb.min_xyz[3] = 0.0f;
            memcpy(aabb.max_xyz, g.aabb_world_max, 12); aabb.max_xyz[3] = 0.0f;
            aabb_data.append(aabb);
        }
        if (vertices.is_empty()) {
            vertices = VulkanMeshBuilder::build_probe_vertices();
            GPUInstanceData identity {};
            static constexpr Array<float, 16> identity_mat { 1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1 };
            memcpy(identity.world_matrix, identity_mat.data(), 64);
            identity.material_index = 0;
            instance_data.append(identity);
            m_draw_groups.clear();
        }
        world_dirty = true;
    } else if (m_world->transform_dirty()) {
        VulkanMeshBuilder::rebuild_transforms_only(*m_world, m_draw_groups, instance_data, aabb_data);
        instances_dirty = true;
    }

#if defined(TRACY_ENABLE)
    {
        int64_t opaque_draw_groups = 0;
        int64_t alpha_clip_draw_groups = 0;
        int64_t blend_draw_groups = 0;
        int64_t alpha_hash_draw_groups = 0;
        for (auto const& group : m_draw_groups) {
            switch (group.alpha_mode) {
            case 1:
                ++alpha_clip_draw_groups;
                break;
            case 2:
                ++blend_draw_groups;
                break;
            case 3:
                ++alpha_hash_draw_groups;
                break;
            default:
                ++opaque_draw_groups;
                break;
            }
        }
        TracyPlot("GeometryPass/OpaqueDrawGroups", opaque_draw_groups);
        TracyPlot("GeometryPass/AlphaClipDrawGroups", alpha_clip_draw_groups);
        TracyPlot("GeometryPass/BlendDrawGroups", blend_draw_groups);
        TracyPlot("GeometryPass/AlphaHashDrawGroups", alpha_hash_draw_groups);
        TracyPlot("GeometryPass/TotalDrawGroups", static_cast<int64_t>(m_draw_groups.size()));
    }
#endif

    if (world_dirty || instances_dirty) {
        if (world_dirty) {
            if (m_file_system) {
                TRY(VulkanPBRManager::update_material_descriptor_set(ctx.device(), m_texture_library, m_material_library, *m_file_system, m_draw_groups, m_flat_normal_texture, m_fallback_black_texture, m_bindless_set, m_pbr_descriptor_sets));
            }
        }
        update_material_ssbo(ctx);
    }

    // Stage geometry data to host-visible buffers (CPU only — no GPU submission).
    m_geometry_upload_pending = false;
    m_vertex_pending_size = 0;
    m_instance_pending_size = 0;
    m_indirect_pending_size = 0;
    m_aabb_pending_size = 0;

    if (world_dirty) {
        m_vertex_pending_size   = TRY(stage_buffer(ctx, vertices,      m_vertex_staging,   m_vertex_buffer,   VK_BUFFER_USAGE_VERTEX_BUFFER_BIT));
        m_instance_pending_size = TRY(stage_buffer(ctx, instance_data, m_instance_staging, m_instance_buffer, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT));
        m_indirect_pending_size = TRY(stage_buffer(ctx, indirect_cmds, m_indirect_staging, m_indirect_ref_buffer, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT));
        m_aabb_pending_size     = TRY(stage_buffer(ctx, aabb_data,     m_aabb_world_staging, m_aabb_world_buffer, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT));

        if (m_indirect_buffer.buffer == VK_NULL_HANDLE || m_indirect_buffer.size < m_indirect_pending_size) {
            m_indirect_buffer.destroy(ctx.allocator());
            VkDeviceSize grow_size = max(m_indirect_pending_size, max(m_indirect_buffer.size * 2, static_cast<VkDeviceSize>(1 * 1024 * 1024)));
            TRY(VulkanResourceManager::create_buffer(ctx.allocator(), grow_size,
                VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                VMA_MEMORY_USAGE_GPU_ONLY, m_indirect_buffer));
        }

        VulkanDebug::name(ctx.device(), m_vertex_staging.buffer,   VK_OBJECT_TYPE_BUFFER, "GeometryPass/VertexStaging"sv);
        VulkanDebug::name(ctx.device(), m_vertex_buffer.buffer,    VK_OBJECT_TYPE_BUFFER, "GeometryPass/VertexBuffer"sv);
        VulkanDebug::name(ctx.device(), m_instance_staging.buffer, VK_OBJECT_TYPE_BUFFER, "GeometryPass/InstanceStaging"sv);
        VulkanDebug::name(ctx.device(), m_instance_buffer.buffer,  VK_OBJECT_TYPE_BUFFER, "GeometryPass/InstanceBuffer"sv);
        VulkanDebug::name(ctx.device(), m_indirect_staging.buffer, VK_OBJECT_TYPE_BUFFER, "GeometryPass/IndirectStaging"sv);
        VulkanDebug::name(ctx.device(), m_indirect_ref_buffer.buffer, VK_OBJECT_TYPE_BUFFER, "GeometryPass/IndirectRefBuffer"sv);
        VulkanDebug::name(ctx.device(), m_indirect_buffer.buffer,  VK_OBJECT_TYPE_BUFFER, "GeometryPass/IndirectLiveBuffer"sv);
        VulkanDebug::name(ctx.device(), m_aabb_world_staging.buffer, VK_OBJECT_TYPE_BUFFER, "GeometryPass/AABBStaging"sv);
        VulkanDebug::name(ctx.device(), m_aabb_world_buffer.buffer,  VK_OBJECT_TYPE_BUFFER, "GeometryPass/AABBBuffer"sv);
        m_geometry_upload_pending = true;
    } else if (instances_dirty) {
        m_instance_pending_size = TRY(stage_buffer(ctx, instance_data, m_instance_staging, m_instance_buffer, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT));
        m_indirect_pending_size = TRY(stage_buffer(ctx, indirect_cmds, m_indirect_staging, m_indirect_ref_buffer, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT));
        m_aabb_pending_size     = TRY(stage_buffer(ctx, aabb_data,     m_aabb_world_staging, m_aabb_world_buffer, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT));
        m_geometry_upload_pending = true;
    }

    update_light_ubo(ctx);
    return {};
}

void GeometryPass::record_uploads(VkCommandBuffer cmd)
{
    if (!m_geometry_upload_pending) return;

    VulkanDebug::cmd_begin_label(cmd, "GeometryUploads"sv, 0.8f, 0.5f, 0.1f);

    auto record_copy = [&](VulkanBuffer const& src, VulkanBuffer const& dst, VkDeviceSize size) {
        if (size == 0 || src.buffer == VK_NULL_HANDLE || dst.buffer == VK_NULL_HANDLE) return;
        VkBufferCopy region { .srcOffset = 0, .dstOffset = 0, .size = size };
        vkCmdCopyBuffer(cmd, src.buffer, dst.buffer, 1, &region);
    };

    record_copy(m_vertex_staging,   m_vertex_buffer,   m_vertex_pending_size);
    record_copy(m_instance_staging, m_instance_buffer, m_instance_pending_size);
    record_copy(m_indirect_staging, m_indirect_ref_buffer, m_indirect_pending_size);
    record_copy(m_aabb_world_staging, m_aabb_world_buffer, m_aabb_pending_size);

    m_geometry_upload_pending = false;
    VulkanDebug::cmd_end_label(cmd); // GeometryUploads
}

void GeometryPass::setup(RenderPassBuilder& builder)
{
    VERIFY(m_graph_bindings.color_target.value != 0);
    VERIFY(m_graph_bindings.depth_target.value != 0);
    if (m_graph_bindings.shadow_map.value != 0)
        builder.read_sampled(m_graph_bindings.shadow_map);
    if (m_vertex_buffer.buffer != VK_NULL_HANDLE) {
        VERIFY(m_graph_bindings.vertex_buffer.value != 0);
        VERIFY(m_graph_bindings.instance_buffer.value != 0);
        builder.read_vertex(m_graph_bindings.vertex_buffer);
        builder.read_vertex(m_graph_bindings.instance_buffer);
    }
    if (!m_draw_groups.is_empty()) {
        VERIFY(m_graph_bindings.indirect_buffer.value != 0);
        builder.read_indirect(m_graph_bindings.indirect_buffer);
    }
    if (m_graph_bindings.cluster_count.value != 0) {
        builder.read_storage(m_graph_bindings.cluster_point_lights);
        builder.read_storage(m_graph_bindings.cluster_count);
        builder.read_storage(m_graph_bindings.cluster_index);
    }

    VkClearValue clear_color {};
    clear_color.color = { { 0.07f, 0.08f, 0.10f, 1.0f } };
    builder.write_color(m_graph_bindings.color_target, { VK_ATTACHMENT_LOAD_OP_CLEAR, VK_ATTACHMENT_STORE_OP_STORE, clear_color });
    if (m_has_depth_prepass) {
        // Depth prepass already cleared and wrote opaque depth; load it so
        // hardware early-z can reject fragments behind the prepass values.
        builder.write_depth(m_graph_bindings.depth_target, { VK_ATTACHMENT_LOAD_OP_LOAD, VK_ATTACHMENT_STORE_OP_STORE, {} });
    } else {
        VkClearValue clear_depth {};
        clear_depth.depthStencil = { 0.0f, 0 };
        builder.write_depth(m_graph_bindings.depth_target, { VK_ATTACHMENT_LOAD_OP_CLEAR, VK_ATTACHMENT_STORE_OP_STORE, clear_depth });
    }
}

void GeometryPass::execute(VkCommandBuffer cmd, RenderGraph& graph)
{
    ZoneScoped;
#if defined(TRACY_ENABLE)
    TracyVkZone(graph.tracy_context(), cmd, "GeometryPass");
#endif
    {
        ZoneScopedN("GeometryPass/UpdateShadowDescriptorSet");
        update_shadow_descriptor_set(graph.context().device(), graph);
    }
    {
        ZoneScopedN("GeometryPass/RecordCommands");
        execute(cmd, graph.render_extent(), graph.frame_push_constants());
    }
}

void GeometryPass::update_light_ubo(VulkanContext const& ctx)
{
    auto allocator = ctx.allocator();
    if (m_light_ubo_buffer.buffer == VK_NULL_HANDLE) {
        if (VulkanResourceManager::create_buffer(allocator, sizeof(SceneLightUBO),
                VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                VMA_MEMORY_USAGE_CPU_ONLY,
                m_light_ubo_buffer).is_error())
            return;

        VkDescriptorPoolSize ps { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1 };
        VkDescriptorPoolCreateInfo pi { .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO, .pNext = nullptr, .flags = 0, .maxSets = 1, .poolSizeCount = 1, .pPoolSizes = &ps };
        if (vkCreateDescriptorPool(ctx.device(), &pi, nullptr, &m_light_ubo_descriptor_pool) != VK_SUCCESS)
            return;

        VkDescriptorSetAllocateInfo ai { .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO, .pNext = nullptr, .descriptorPool = m_light_ubo_descriptor_pool, .descriptorSetCount = 1, .pSetLayouts = &m_pipeline.light_layout };
        if (vkAllocateDescriptorSets(ctx.device(), &ai, &m_light_ubo_descriptor_set) != VK_SUCCESS)
            return;

        VkDescriptorBufferInfo bi { .buffer = m_light_ubo_buffer.buffer, .offset = 0, .range = sizeof(SceneLightUBO) };
        VkWriteDescriptorSet w { .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, .pNext = nullptr, .dstSet = m_light_ubo_descriptor_set, .dstBinding = 0, .dstArrayElement = 0, .descriptorCount = 1, .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, .pImageInfo = nullptr, .pBufferInfo = &bi, .pTexelBufferView = nullptr };
        vkUpdateDescriptorSets(ctx.device(), 1, &w, 0, nullptr);

        VulkanDebug::name(ctx.device(), m_light_ubo_buffer.buffer,      VK_OBJECT_TYPE_BUFFER,          "GeometryPass/LightUBO"sv);
        VulkanDebug::name(ctx.device(), m_light_ubo_descriptor_pool,    VK_OBJECT_TYPE_DESCRIPTOR_POOL, "GeometryPass/LightDescriptorPool"sv);
        VulkanDebug::name(ctx.device(), m_light_ubo_descriptor_set,     VK_OBJECT_TYPE_DESCRIPTOR_SET,  "GeometryPass/LightDescriptorSet"sv);
    }

    SceneLightUBO ubo {};
    ubo.ambient[0] = m_scene_light.ambient_rgb[0];
    ubo.ambient[1] = m_scene_light.ambient_rgb[1];
    ubo.ambient[2] = m_scene_light.ambient_rgb[2];
    ubo.ambient[3] = m_scene_light.ambient_intensity;
    ubo.sun_direction[0] = m_scene_light.light_to_xyz[0];
    ubo.sun_direction[1] = m_scene_light.light_to_xyz[1];
    ubo.sun_direction[2] = m_scene_light.light_to_xyz[2];
    ubo.sun_direction[3] = 0.0f;
    ubo.sun_color[0] = m_scene_light.light_rgb[0];
    ubo.sun_color[1] = m_scene_light.light_rgb[1];
    ubo.sun_color[2] = m_scene_light.light_rgb[2];
    ubo.sun_color[3] = m_scene_light.light_intensity;
    ubo.eye_position[0] = m_camera_position[0];
    ubo.eye_position[1] = m_camera_position[1];
    ubo.eye_position[2] = m_camera_position[2];
    ubo.eye_position[3] = 1.0f;
    ubo.light_params[0] = m_scene_light.point_light_count;
    ubo.light_params[1] = m_graph_bindings.shadow_map.value != 0 ? m_shadow_quality : 0;
    ubo.light_params[2] = static_cast<int32_t>(m_screen_w);
    ubo.light_params[3] = static_cast<int32_t>(m_screen_h);
    for (int i = 0; i < 16; ++i)
        ubo.shadow_view_projection[i] = m_shadow_view_projection.elements[i];
    ubo.shadow_params[0] = m_graph_bindings.shadow_map.value != 0 ? 1.0f : 0.0f;
    ubo.shadow_params[1] = 0.0015f;
    ubo.shadow_params[2] = 1.0f / 2048.0f;
    ubo.shadow_params[3] = 1.0f / 2048.0f;
    float log_far_over_near = (m_cluster_far > m_cluster_near && m_cluster_near > 0.0f)
        ? __builtin_logf(m_cluster_far / m_cluster_near)
        : 1.0f;
    ubo.cluster_params[0] = m_cluster_near;
    ubo.cluster_params[1] = m_cluster_far;
    ubo.cluster_params[2] = log_far_over_near;
    ubo.cluster_params[3] = 0.0f;

#if defined(TRACY_ENABLE)
    {
        int64_t active_point_lights = 0;
        int64_t active_spot_lights = 0;
        for (int i = 0; i < m_scene_light.point_light_count && i < MaxPointLights; ++i) {
            if (m_scene_light.point_lights[i].type > 0)
                ++active_spot_lights;
            else
                ++active_point_lights;
        }
        TracyPlot("GeometryPass/ActivePointLights", active_point_lights);
        TracyPlot("GeometryPass/ActiveSpotLights", active_spot_lights);
        TracyPlot("GeometryPass/ShadowQuality", static_cast<int64_t>(ubo.light_params[1]));
    }
#endif

    void* data = nullptr;
    vmaMapMemory(allocator, m_light_ubo_buffer.allocation, &data);
    memcpy(data, &ubo, sizeof(SceneLightUBO));
    vmaUnmapMemory(allocator, m_light_ubo_buffer.allocation);
}

void GeometryPass::update_material_ssbo(VulkanContext const& ctx)
{
    if (m_draw_groups.is_empty()) return;
    auto allocator = ctx.allocator();

    size_t data_size = m_draw_groups.size() * sizeof(GPUMaterialData);
    if (m_material_ssbo_buffer.buffer == VK_NULL_HANDLE || m_material_ssbo_buffer.size < data_size) {
        m_material_ssbo_buffer.destroy(allocator);
        if (VulkanResourceManager::create_buffer(allocator, data_size,
                VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                VMA_MEMORY_USAGE_CPU_ONLY,
                m_material_ssbo_buffer).is_error())
            return;

        if (m_material_ssbo_descriptor_pool != VK_NULL_HANDLE) vkDestroyDescriptorPool(ctx.device(), m_material_ssbo_descriptor_pool, nullptr);
        VkDescriptorPoolSize ps { .type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, .descriptorCount = 1 };
        VkDescriptorPoolCreateInfo pi { .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO, .pNext = nullptr, .flags = 0, .maxSets = 1, .poolSizeCount = 1, .pPoolSizes = &ps };
        if (vkCreateDescriptorPool(ctx.device(), &pi, nullptr, &m_material_ssbo_descriptor_pool) != VK_SUCCESS)
            return;

        VkDescriptorSetAllocateInfo ai { .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO, .pNext = nullptr, .descriptorPool = m_material_ssbo_descriptor_pool, .descriptorSetCount = 1, .pSetLayouts = &m_pipeline.material_layout };
        if (vkAllocateDescriptorSets(ctx.device(), &ai, &m_material_ssbo_descriptor_set) != VK_SUCCESS)
            return;

        VkDescriptorBufferInfo bi { .buffer = m_material_ssbo_buffer.buffer, .offset = 0, .range = data_size };
        VkWriteDescriptorSet w { .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, .pNext = nullptr, .dstSet = m_material_ssbo_descriptor_set, .dstBinding = 0, .dstArrayElement = 0, .descriptorCount = 1, .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, .pImageInfo = nullptr, .pBufferInfo = &bi, .pTexelBufferView = nullptr };
        vkUpdateDescriptorSets(ctx.device(), 1, &w, 0, nullptr);
        
        VulkanDebug::name(ctx.device(), m_material_ssbo_buffer.buffer,      VK_OBJECT_TYPE_BUFFER,          "GeometryPass/MaterialSSBO"sv);
        VulkanDebug::name(ctx.device(), m_material_ssbo_descriptor_pool,   VK_OBJECT_TYPE_DESCRIPTOR_POOL, "GeometryPass/MaterialDescriptorPool"sv);
        VulkanDebug::name(ctx.device(), m_material_ssbo_descriptor_set,    VK_OBJECT_TYPE_DESCRIPTOR_SET,  "GeometryPass/MaterialDescriptorSet"sv);
    }

    auto get_tex_idx = [&](MaterialTextureSlot const& slot) -> int32_t {
        if (!slot.is_set()) return -1;
        if (slot.cached_bindless_index.has_value())
            return static_cast<int32_t>(slot.cached_bindless_index.value());

        String path;
        if (!slot.path.is_empty()) path = slot.path;
        else path = slot.embedded_cache_key;

        if (auto it = m_bindless_set.texture_index_by_path.find(path); it != m_bindless_set.texture_index_by_path.end()) {
            slot.cached_bindless_index = it->value;
            return static_cast<int32_t>(it->value);
        }
        return -1;
    };

    HashMap<String, GPUMaterialData> material_data_cache;
    Vector<GPUMaterialData> material_data;
    material_data.ensure_capacity(m_draw_groups.size());
    int64_t materials_using_normal_maps = 0;
    int64_t materials_using_emissive = 0;
    int64_t materials_using_occlusion = 0;
    for (size_t i = 0; i < m_draw_groups.size(); ++i) {
        auto& group = m_draw_groups[i];
        group.material_index = static_cast<u32>(i);
        
        if (auto it = material_data_cache.find(group.material); it != material_data_cache.end()) {
            material_data.append(it->value);
            continue;
        }

        GPUMaterialData data;
        auto const* asset = m_material_library.resolve(group.material);
        if (asset) {
            data.metallic_factor = asset->metallic_factor;
            data.roughness_factor = asset->roughness_factor;
            data.alpha_cutoff = asset->alpha_cutoff;
            data.alpha_mode = static_cast<int32_t>(asset->alpha_mode);
            data.emissive_factor[0] = asset->emissive_factor[0];
            data.emissive_factor[1] = asset->emissive_factor[1];
            data.emissive_factor[2] = asset->emissive_factor[2];
            data.base_color_factor[0] = asset->base_color_factor[0];
            data.base_color_factor[1] = asset->base_color_factor[1];
            data.base_color_factor[2] = asset->base_color_factor[2];
            data.base_color_factor[3] = asset->base_color_factor[3];

            data.albedo_idx = get_tex_idx(asset->albedo);
            data.normal_idx = get_tex_idx(asset->normal);
            data.metallic_roughness_idx = get_tex_idx(asset->metallic_roughness);
            data.emissive_idx = get_tex_idx(asset->emissive);
            data.occlusion_idx = get_tex_idx(asset->occlusion);

            // Entity-level normal map override takes precedence over the material slot.
            if (!group.normal_map.is_empty()) {
                if (auto it = m_bindless_set.texture_index_by_path.find(group.normal_map); it != m_bindless_set.texture_index_by_path.end())
                    data.normal_idx = static_cast<int32_t>(it->value);
            }

            if (data.normal_idx >= 0)
                ++materials_using_normal_maps;
            if (asset->emissive.is_set())
                ++materials_using_emissive;
            if (asset->occlusion.is_set())
                ++materials_using_occlusion;
        }
        material_data_cache.set(group.material, data);
        material_data.append(data);
    }

#if defined(TRACY_ENABLE)
    TracyPlot("GeometryPass/MaterialsUsingNormalMaps", materials_using_normal_maps);
    TracyPlot("GeometryPass/MaterialsUsingEmissive", materials_using_emissive);
    TracyPlot("GeometryPass/MaterialsUsingOcclusion", materials_using_occlusion);
    TracyPlot("GeometryPass/MaterialSSBOEntries", static_cast<int64_t>(material_data.size()));
#endif

    void* data_ptr = nullptr;
    vmaMapMemory(allocator, m_material_ssbo_buffer.allocation, &data_ptr);
    memcpy(data_ptr, material_data.data(), data_size);
    vmaUnmapMemory(allocator, m_material_ssbo_buffer.allocation);
}

void GeometryPass::update_shadow_descriptor_set(VkDevice device, RenderGraph& graph)
{
    if (m_shadow_descriptor_pool == VK_NULL_HANDLE) {
        VkDescriptorPoolSize pool_size { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1 };
        VkDescriptorPoolCreateInfo pool_info { VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO, nullptr, 0, 1, 1, &pool_size };
        if (vkCreateDescriptorPool(device, &pool_info, nullptr, &m_shadow_descriptor_pool) != VK_SUCCESS)
            return;

        VkDescriptorSetAllocateInfo alloc_info { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO, nullptr, m_shadow_descriptor_pool, 1, &m_pipeline.shadow_layout };
        if (vkAllocateDescriptorSets(device, &alloc_info, &m_shadow_descriptor_set) != VK_SUCCESS)
            return;

        VkSamplerCreateInfo sampler_info {};
        sampler_info.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
        sampler_info.magFilter = VK_FILTER_LINEAR;
        sampler_info.minFilter = VK_FILTER_LINEAR;
        sampler_info.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        sampler_info.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        sampler_info.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        sampler_info.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
        sampler_info.maxLod = 1.0f;
        if (vkCreateSampler(device, &sampler_info, nullptr, &m_shadow_sampler) != VK_SUCCESS)
            return;
    }

    VkImageView shadow_view = m_graph_bindings.shadow_map.value != 0
        ? graph.get_texture_view(m_graph_bindings.shadow_map)
        : m_fallback_black_texture.view;
    if (shadow_view == VK_NULL_HANDLE || shadow_view == m_last_shadow_view)
        return;
    m_last_shadow_view = shadow_view;

    VkDescriptorImageInfo image_info { m_shadow_sampler, shadow_view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
    VkWriteDescriptorSet write {
        .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
        .pNext = nullptr,
        .dstSet = m_shadow_descriptor_set,
        .dstBinding = 0,
        .dstArrayElement = 0,
        .descriptorCount = 1,
        .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
        .pImageInfo = &image_info,
        .pBufferInfo = nullptr,
        .pTexelBufferView = nullptr,
    };
    vkUpdateDescriptorSets(device, 1, &write, 0, nullptr);
}

void GeometryPass::execute(VkCommandBuffer command_buffer, VkExtent2D extent, PushConstants const& pc)
{
    ZoneScoped;
    {
        ZoneScopedN("GeometryPass/ViewportScissor");
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

    if (m_vertex_buffer.buffer == VK_NULL_HANDLE) return;

    {
        ZoneScopedN("GeometryPass/PushConstants");
        vkCmdPushConstants(command_buffer, m_pipeline.layout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(PushConstants), &pc);
    }

    {
        ZoneScopedN("GeometryPass/BindDescriptors");
        if (m_light_ubo_descriptor_set != VK_NULL_HANDLE) vkCmdBindDescriptorSets(command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipeline.layout, 1, 1, &m_light_ubo_descriptor_set, 0, nullptr);
        if (m_material_ssbo_descriptor_set != VK_NULL_HANDLE) vkCmdBindDescriptorSets(command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipeline.layout, 2, 1, &m_material_ssbo_descriptor_set, 0, nullptr);
        if (m_shadow_descriptor_set != VK_NULL_HANDLE) vkCmdBindDescriptorSets(command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipeline.layout, 3, 1, &m_shadow_descriptor_set, 0, nullptr);
        if (m_cluster_descriptor_set != VK_NULL_HANDLE) vkCmdBindDescriptorSets(command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipeline.layout, 4, 1, &m_cluster_descriptor_set, 0, nullptr);
        if (m_bindless_set.set != VK_NULL_HANDLE) vkCmdBindDescriptorSets(command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipeline.layout, 0, 1, &m_bindless_set.set, 0, nullptr);
    }

    {
        ZoneScopedN("GeometryPass/BindBuffers");
        VkDeviceSize off[] = { 0 };
        vkCmdBindVertexBuffers(command_buffer, 0, 1, &m_vertex_buffer.buffer, off);
        vkCmdBindVertexBuffers(command_buffer, 1, 1, &m_instance_buffer.buffer, off);
    }

    auto to_pipeline_alpha_mode = [](int alpha_mode) {
        switch (alpha_mode) {
        case 1:
            return VulkanWorldPipeline::AlphaMode::Clip;
        case 2:
            return VulkanWorldPipeline::AlphaMode::Blend;
        case 3:
            return VulkanWorldPipeline::AlphaMode::Hash;
        default:
            return VulkanWorldPipeline::AlphaMode::Opaque;
        }
    };

    auto draw_bucket = [&](size_t begin, size_t end, int alpha_mode, VulkanWorldPipeline::CullMode cull_mode, bool has_normal_map) {
        if (begin >= end)
            return;

        auto pipeline_alpha_mode = to_pipeline_alpha_mode(alpha_mode);
        bool depth_read_only = m_has_depth_prepass && pipeline_alpha_mode == VulkanWorldPipeline::AlphaMode::Opaque;
        vkCmdBindPipeline(command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipeline.pipeline_for(pipeline_alpha_mode, cull_mode, has_normal_map, depth_read_only));
        if (m_supports_multi_draw_indirect) {
            vkCmdDrawIndirect(command_buffer, m_indirect_buffer.buffer, begin * sizeof(VkDrawIndirectCommand), static_cast<u32>(end - begin), sizeof(VkDrawIndirectCommand));
        } else {
            for (size_t i = begin; i < end; ++i)
                vkCmdDrawIndirect(command_buffer, m_indirect_buffer.buffer, i * sizeof(VkDrawIndirectCommand), 1, sizeof(VkDrawIndirectCommand));
        }
    };

    auto draw_contiguous_buckets = [&](bool blend) {
        Optional<size_t> bucket_begin;
        auto current_cull_mode = VulkanWorldPipeline::CullMode::Back;
        bool current_has_nmap = true;
        int current_alpha_mode = 0;
        size_t last_matching_end = 0;

        for (size_t i = 0; i < m_draw_groups.size(); ++i) {
            bool is_blend = m_draw_groups[i].alpha_mode == 2;
            if (is_blend != blend)
                continue;

            VulkanWorldPipeline::CullMode group_cull_mode;
            switch (m_draw_groups[i].cull_mode) {
            case DrawGroup::CullMode::Disabled: group_cull_mode = VulkanWorldPipeline::CullMode::Disabled; break;
            case DrawGroup::CullMode::Front:    group_cull_mode = VulkanWorldPipeline::CullMode::Front;    break;
            default:                            group_cull_mode = VulkanWorldPipeline::CullMode::Back;     break;
            }
            bool group_has_nmap = m_draw_groups[i].has_normal_map;
            int group_alpha_mode = m_draw_groups[i].alpha_mode;

            if (!bucket_begin.has_value()) {
                bucket_begin = i;
                current_cull_mode = group_cull_mode;
                current_has_nmap = group_has_nmap;
                current_alpha_mode = group_alpha_mode;
            } else if (group_cull_mode != current_cull_mode || group_has_nmap != current_has_nmap || group_alpha_mode != current_alpha_mode) {
                draw_bucket(*bucket_begin, i, current_alpha_mode, current_cull_mode, current_has_nmap);
                bucket_begin = i;
                current_cull_mode = group_cull_mode;
                current_has_nmap = group_has_nmap;
                current_alpha_mode = group_alpha_mode;
            }
            last_matching_end = i + 1;
        }

        if (bucket_begin.has_value())
            draw_bucket(*bucket_begin, last_matching_end, current_alpha_mode, current_cull_mode, current_has_nmap);
    };

    {
        ZoneScopedN("GeometryPass/DrawOpaqueAndAlphaClip");
        draw_contiguous_buckets(false);
    }
    {
        ZoneScopedN("GeometryPass/DrawBlend");
        draw_contiguous_buckets(true);
    }
}

}
