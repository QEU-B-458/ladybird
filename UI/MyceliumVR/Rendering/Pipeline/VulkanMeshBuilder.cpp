/*
 * MyceliumVR - a social VR platform built on the Ladybird browser engine
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "VulkanMeshBuilder.h"

#include <AK/HashMap.h>

#if defined(TRACY_ENABLE)
#    include <tracy/Tracy.hpp>
#endif

namespace MyceliumVR {

Vector<Vertex> VulkanMeshBuilder::build_probe_vertices()
{
    Vector<Vertex> vertices;
    vertices.ensure_capacity(3);
    vertices.append(Vertex { .position { -0.5f, -0.5f, 0.0f }, .normal { 0.0f, 0.0f, 1.0f }, .color { 0.20f, 0.80f, 0.95f }, .uv { 0.0f, 1.0f }, .tangent {} });
    vertices.append(Vertex { .position { 0.5f, -0.5f, 0.0f }, .normal { 0.0f, 0.0f, 1.0f }, .color { 0.20f, 0.80f, 0.95f }, .uv { 1.0f, 1.0f }, .tangent {} });
    vertices.append(Vertex { .position { 0.0f, 0.5f, 0.0f }, .normal { 0.0f, 0.0f, 1.0f }, .color { 0.20f, 0.80f, 0.95f }, .uv { 0.5f, 0.0f }, .tangent {} });
    return vertices;
}

Array<float, 16> VulkanMeshBuilder::make_trs_matrix(Transform const& t)
{
    float qx = t.rotation[0], qy = t.rotation[1], qz = t.rotation[2], qw = t.rotation[3];
    float sx = t.scale[0], sy = t.scale[1], sz = t.scale[2];
    float tx = t.position[0], ty = t.position[1], tz = t.position[2];
    float r00 = 1.0f - 2.0f * (qy * qy + qz * qz);
    float r10 = 2.0f * (qx * qy + qz * qw);
    float r20 = 2.0f * (qx * qz - qy * qw);
    float r01 = 2.0f * (qx * qy - qz * qw);
    float r11 = 1.0f - 2.0f * (qx * qx + qz * qz);
    float r21 = 2.0f * (qy * qz + qx * qw);
    float r02 = 2.0f * (qx * qz + qy * qw);
    float r12 = 2.0f * (qy * qz - qx * qw);
    float r22 = 1.0f - 2.0f * (qx * qx + qy * qy);
    Array<float, 16> m {};
    m[0]  = sx * r00;  m[1]  = sx * r10;  m[2]  = sx * r20;  m[3]  = 0.0f;
    m[4]  = sy * r01;  m[5]  = sy * r11;  m[6]  = sy * r21;  m[7]  = 0.0f;
    m[8]  = sz * r02;  m[9]  = sz * r12;  m[10] = sz * r22;  m[11] = 0.0f;
    m[12] = tx;        m[13] = ty;        m[14] = tz;        m[15] = 1.0f;
    return m;
}

static void expand_group_aabb(DrawGroup& group, Transform const& transform)
{
    float const* bmin = group.aabb_local_min;
    float const* bmax = group.aabb_local_max;
    for (int mask = 0; mask < 8; ++mask) {
        Vec3 corner {
            (mask & 1) ? bmax[0] : bmin[0],
            (mask & 2) ? bmax[1] : bmin[1],
            (mask & 4) ? bmax[2] : bmin[2],
        };
        corner.x *= transform.scale[0];
        corner.y *= transform.scale[1];
        corner.z *= transform.scale[2];
        corner = rotate_by_quaternion(corner, transform.rotation[0], transform.rotation[1], transform.rotation[2], transform.rotation[3]);
        corner.x += transform.position[0];
        corner.y += transform.position[1];
        corner.z += transform.position[2];
        if (corner.x < group.aabb_world_min[0]) group.aabb_world_min[0] = corner.x;
        if (corner.y < group.aabb_world_min[1]) group.aabb_world_min[1] = corner.y;
        if (corner.z < group.aabb_world_min[2]) group.aabb_world_min[2] = corner.z;
        if (corner.x > group.aabb_world_max[0]) group.aabb_world_max[0] = corner.x;
        if (corner.y > group.aabb_world_max[1]) group.aabb_world_max[1] = corner.y;
        if (corner.z > group.aabb_world_max[2]) group.aabb_world_max[2] = corner.z;
    }
}

void VulkanMeshBuilder::build_world_instanced(
    World const& world,
    MeshLibrary& mesh_library,
    Vector<Vertex>& out_mesh_vertices,
    Vector<GPUInstanceData>& out_instance_data,
    Vector<DrawGroup>& out_groups)
{
#if defined(TRACY_ENABLE)
    ZoneScopedN("MeshBuilder/BuildWorldInstanced");
#endif
    Vector<Vector<EntityId>> group_entities;
    Vector<MeshAsset const*> group_meshes;

    out_mesh_vertices.clear();
    out_instance_data.clear();
    out_groups.clear();

    HashMap<String, u32> group_index_map;

    auto view = world.registry().view<Transform const, MeshRenderer const>(entt::exclude<Panel>);
    for (auto entity : view) {
        auto const& mesh_renderer = view.get<MeshRenderer const>(entity);

        auto const& mesh_name = mesh_renderer.mesh;
        auto const& normal_map = mesh_renderer.normal_map;
        auto const& raw_material = mesh_renderer.material;

        int alpha_mode = 0; // Opaque
        if (world.registry().all_of<AlphaClip>(entity)) alpha_mode = 1;
        else if (world.registry().all_of<AlphaBlend>(entity)) alpha_mode = 2;
        else if (world.registry().all_of<AlphaHash>(entity)) alpha_mode = 3;

        struct GroupKey {
            String mesh;
            String material;
            String normal_map;
            int alpha_mode;
            DrawGroup::CullMode cull_mode;
            bool has_normal_map;
        };

        auto get_group_key = [&]() {
            auto sv = mesh_name.bytes_as_string_view();
            bool const is_gltf = sv.ends_with(".glb"sv) || sv.ends_with(".gltf"sv);
            bool const is_default_material = raw_material.is_empty() || raw_material == "default"_string;
            String material = (is_gltf && is_default_material) ? mesh_name : raw_material;
            auto cull_mode = DrawGroup::CullMode::Back;
            bool has_normal_map = false;
            if (auto const* material_library = mesh_library.material_library(); material_library) {
                if (auto const* asset = material_library->resolve(material); asset) {
                    if (asset->cull_mode == MaterialAsset::CullMode::Disabled)
                        cull_mode = DrawGroup::CullMode::Disabled;
                    has_normal_map = asset->normal.is_set() || !normal_map.is_empty();
                }
            }
            // Entity-level CullOverride takes precedence over the material setting.
            if (auto const* override_comp = world.registry().try_get<CullOverride>(entity); override_comp) {
                switch (override_comp->mode) {
                case CullOverride::Mode::Back:     cull_mode = DrawGroup::CullMode::Back;     break;
                case CullOverride::Mode::Front:    cull_mode = DrawGroup::CullMode::Front;    break;
                case CullOverride::Mode::Disabled: cull_mode = DrawGroup::CullMode::Disabled; break;
                }
            }
            return GroupKey { mesh_name, material, normal_map, alpha_mode, cull_mode, has_normal_map };
        };

        GroupKey key = get_group_key();
        auto key_str = MUST(String::formatted("{}|{}|{}|{}|{}|{}", key.mesh, key.material, key.normal_map, key.alpha_mode, static_cast<int>(key.cull_mode), static_cast<int>(key.has_normal_map)));

        u32 group_idx = 0;
        bool found = false;
        if (auto it = group_index_map.find(key_str); it != group_index_map.end()) {
            group_idx = it->value;
            found = true;
        }

        if (!found) {
            auto mesh_result = mesh_library.resolve_mesh(mesh_name);
            if (mesh_result.is_error()) mesh_result = mesh_library.resolve_mesh("cube"_string);
            if (mesh_result.is_error()) continue;
            auto const* mesh_asset = mesh_result.release_value();

            DrawGroup group;
            group.mesh_name = key.mesh;
            group.material = key.material;
            group.normal_map = key.normal_map;
            group.alpha_mode = key.alpha_mode;
            group.cull_mode = key.cull_mode;
            group.has_normal_map = key.has_normal_map;
            group.first_vertex = 0;
            group.vertex_count = static_cast<u32>(mesh_asset->positions.size() / 3);
            group.aabb_local_min[0] = mesh_asset->aabb_min[0];
            group.aabb_local_min[1] = mesh_asset->aabb_min[1];
            group.aabb_local_min[2] = mesh_asset->aabb_min[2];
            group.aabb_local_max[0] = mesh_asset->aabb_max[0];
            group.aabb_local_max[1] = mesh_asset->aabb_max[1];
            group.aabb_local_max[2] = mesh_asset->aabb_max[2];
            group_idx = static_cast<u32>(out_groups.size());
            group_index_map.set(key_str, group_idx);
            out_groups.append(move(group));
            group_entities.append({});
            group_meshes.append(mesh_asset);
        }

        group_entities[group_idx].append(entity);
    }

    if (!out_groups.is_empty()) {
        Vector<DrawGroup> reordered_groups;
        Vector<Vector<EntityId>> reordered_group_entities;
        Vector<MeshAsset const*> reordered_group_meshes;
        reordered_groups.ensure_capacity(out_groups.size());
        reordered_group_entities.ensure_capacity(group_entities.size());
        reordered_group_meshes.ensure_capacity(group_meshes.size());

        // Sort by alpha_mode first so opaque geometry is contiguous at the front
        // of the indirect buffer — required for the depth prepass to draw only
        // opaque groups with a simple range-based multi-draw indirect call.
        // Order: opaque (0), alpha_clip (1), alpha_hash (3), blend (2).
        auto append_bucket = [&](int alpha_mode, DrawGroup::CullMode cull_mode, bool has_nmap) {
            for (size_t i = 0; i < out_groups.size(); ++i) {
                if (out_groups[i].alpha_mode != alpha_mode || out_groups[i].cull_mode != cull_mode || out_groups[i].has_normal_map != has_nmap)
                    continue;
                reordered_groups.append(move(out_groups[i]));
                reordered_group_entities.append(move(group_entities[i]));
                reordered_group_meshes.append(group_meshes[i]);
            }
        };

        for (int mode : { 0, 1, 3, 2 }) {
            append_bucket(mode, DrawGroup::CullMode::Back,     true);
            append_bucket(mode, DrawGroup::CullMode::Back,     false);
            append_bucket(mode, DrawGroup::CullMode::Front,    true);
            append_bucket(mode, DrawGroup::CullMode::Front,    false);
            append_bucket(mode, DrawGroup::CullMode::Disabled, true);
            append_bucket(mode, DrawGroup::CullMode::Disabled, false);
        }

        out_groups = move(reordered_groups);
        group_entities = move(reordered_group_entities);
        group_meshes = move(reordered_group_meshes);
    }

    size_t total_vertices = 0;
    size_t total_instances = 0;
    for (u32 i = 0; i < out_groups.size(); ++i) {
        total_vertices += out_groups[i].vertex_count;
        total_instances += group_entities[i].size();
    }
    out_mesh_vertices.ensure_capacity(total_vertices);
    out_instance_data.ensure_capacity(total_instances);

    for (u32 i = 0; i < out_groups.size(); ++i) {
        auto& group = out_groups[i];
        auto const* mesh_asset = group_meshes[i];
        group.material_index = i;
        group.first_vertex = static_cast<u32>(out_mesh_vertices.size());

        auto const& positions = mesh_asset->positions;
        auto const& normals_arr = mesh_asset->normals;
        auto const& texcoords = mesh_asset->texcoords;
        auto const& tangents_arr = mesh_asset->tangents;
        bool const has_normals = !normals_arr.is_empty();
        bool const has_texcoords = !texcoords.is_empty();
        bool const has_tangents = !tangents_arr.is_empty();

        for (size_t tri = 0; tri < group.vertex_count; tri += 3) {
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
                Vec3 normal = face_normal;
                if (has_normals)
                    normal = Vec3 { normals_arr[bi3], normals_arr[bi3 + 1], normals_arr[bi3 + 2] };
                float uv_u = 0.0f, uv_v = 0.0f;
                if (has_texcoords && bi2 + 1 < texcoords.size()) {
                    uv_u = texcoords[bi2];
                    uv_v = texcoords[bi2 + 1];
                }
                float tan_x = 0.0f, tan_y = 0.0f, tan_z = 0.0f, tan_w = 1.0f;
                if (has_tangents && bi4 + 3 < tangents_arr.size()) {
                    tan_x = tangents_arr[bi4];
                    tan_y = tangents_arr[bi4 + 1];
                    tan_z = tangents_arr[bi4 + 2];
                    tan_w = tangents_arr[bi4 + 3];
                }
                out_mesh_vertices.append(Vertex {
                    .position { positions[bi3], positions[bi3 + 1], positions[bi3 + 2] },
                    .normal { normal.x, normal.y, normal.z },
                    .color { 1.0f, 1.0f, 1.0f },
                    .uv { uv_u, uv_v },
                    .tangent { tan_x, tan_y, tan_z, tan_w },
                });
            }
        }

        if (group.vertex_count > 0) {
            float z_sum = 0.0f;
            for (u32 vi = group.first_vertex; vi < group.first_vertex + group.vertex_count; ++vi)
                z_sum += out_mesh_vertices[vi].position[2];
            group.centroid_z = z_sum / static_cast<float>(group.vertex_count);
        }

        group.first_instance = static_cast<u32>(out_instance_data.size());
        group.instance_count = static_cast<u32>(group_entities[i].size());
        group.instance_entity_ids = move(group_entities[i]);

        for (auto entity_id : group.instance_entity_ids) {
            auto const& transform = world.registry().get<Transform>(entity_id);
            GPUInstanceData data;
            auto m = make_trs_matrix(transform);
            memcpy(data.world_matrix, m.data(), 64);
            data.material_index = group.material_index;
            out_instance_data.append(data);
            expand_group_aabb(group, transform);
        }
    }
}

void VulkanMeshBuilder::rebuild_transforms_only(World const& world, Vector<DrawGroup>& groups, Vector<GPUInstanceData>& out_instance_data, Vector<GroupAABBGPU>& out_aabb_data)
{
#if defined(TRACY_ENABLE)
    ZoneScopedN("MeshBuilder/RebuildTransforms");
#endif
    size_t total_instances = 0;
    for (auto const& group : groups)
        total_instances += group.instance_entity_ids.size();

    out_instance_data.clear();
    out_instance_data.ensure_capacity(total_instances);
    out_aabb_data.clear();
    out_aabb_data.ensure_capacity(groups.size());

    for (auto& group : groups) {
        group.aabb_world_min[0] = group.aabb_world_min[1] = group.aabb_world_min[2] = 1e30f;
        group.aabb_world_max[0] = group.aabb_world_max[1] = group.aabb_world_max[2] = -1e30f;

        group.first_instance = static_cast<u32>(out_instance_data.size());
        for (auto entity_id : group.instance_entity_ids) {
            auto const* transform = world.registry().try_get<Transform>(entity_id);
            GPUInstanceData data;
            data.material_index = group.material_index;
            if (!transform) {
                static constexpr Array<float, 16> identity { 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1 };
                memcpy(data.world_matrix, identity.data(), 64);
            } else {
                auto m = make_trs_matrix(*transform);
                memcpy(data.world_matrix, m.data(), 64);
                expand_group_aabb(group, *transform);
            }
            out_instance_data.append(data);
        }

        GroupAABBGPU aabb;
        memcpy(aabb.min_xyz, group.aabb_world_min, 12); aabb.min_xyz[3] = 0.0f;
        memcpy(aabb.max_xyz, group.aabb_world_max, 12); aabb.max_xyz[3] = 0.0f;
        out_aabb_data.append(aabb);
    }
}

Vector<TexturedVertex> VulkanMeshBuilder::build_panel_vertices(World const& world)
{
#if defined(TRACY_ENABLE)
    ZoneScopedN("MeshBuilder/BuildPanelVertices");
#endif
    Vector<TexturedVertex> vertices;
    auto view = world.registry().view<Transform const, Panel const>();
    for (auto entity : view) {
        auto const& transform = view.get<Transform const>(entity);
        auto const& panel = view.get<Panel const>(entity);

        auto half_width = panel.width * transform.scale[0] * 0.5f;
        auto half_height = panel.height * transform.scale[1] * 0.5f;
        Array<Vec3, 4> corners {
            Vec3 { -half_width, -half_height, 0.0f },
            Vec3 { half_width, -half_height, 0.0f },
            Vec3 { half_width, half_height, 0.0f },
            Vec3 { -half_width, half_height, 0.0f },
        };
        for (auto& corner : corners) {
            corner = rotate_by_quaternion(corner, transform.rotation[0], transform.rotation[1], transform.rotation[2], transform.rotation[3]);
            corner.x += transform.position[0]; corner.y += transform.position[1]; corner.z += transform.position[2];
        }
        vertices.append(TexturedVertex { .position { corners[0].x, corners[0].y, corners[0].z }, .uv { 0.0f, 1.0f } });
        vertices.append(TexturedVertex { .position { corners[1].x, corners[1].y, corners[1].z }, .uv { 1.0f, 1.0f } });
        vertices.append(TexturedVertex { .position { corners[2].x, corners[2].y, corners[2].z }, .uv { 1.0f, 0.0f } });
        vertices.append(TexturedVertex { .position { corners[0].x, corners[0].y, corners[0].z }, .uv { 0.0f, 1.0f } });
        vertices.append(TexturedVertex { .position { corners[2].x, corners[2].y, corners[2].z }, .uv { 1.0f, 0.0f } });
        vertices.append(TexturedVertex { .position { corners[3].x, corners[3].y, corners[3].z }, .uv { 0.0f, 0.0f } });
    }
    return vertices;
}

Vector<TexturedVertex> VulkanMeshBuilder::build_overlay_vertices(OverlayView const&)
{
    Vector<TexturedVertex> vertices;
    vertices.ensure_capacity(6);
    static constexpr float kOverlayDepth = 0.999f;
    vertices.append(TexturedVertex { .position { -1.0f,  1.0f, kOverlayDepth }, .uv { 0.0f, 1.0f } });
    vertices.append(TexturedVertex { .position {  1.0f,  1.0f, kOverlayDepth }, .uv { 1.0f, 1.0f } });
    vertices.append(TexturedVertex { .position {  1.0f, -1.0f, kOverlayDepth }, .uv { 1.0f, 0.0f } });
    vertices.append(TexturedVertex { .position { -1.0f,  1.0f, kOverlayDepth }, .uv { 0.0f, 1.0f } });
    vertices.append(TexturedVertex { .position {  1.0f, -1.0f, kOverlayDepth }, .uv { 1.0f, 0.0f } });
    vertices.append(TexturedVertex { .position { -1.0f, -1.0f, kOverlayDepth }, .uv { 0.0f, 0.0f } });
    return vertices;
}

}
