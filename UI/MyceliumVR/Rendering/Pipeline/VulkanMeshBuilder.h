/*
 * MyceliumVR - a social VR platform built on the Ladybird browser engine
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include "../VulkanCommon.h"
#include "../../World/World.h"
#include "../../Support/MeshLibrary.h"

#include <AK/String.h>
#include <AK/Vector.h>

namespace MyceliumVR {

struct DrawGroup {
    enum class CullMode : u8 {
        Back,
        Front,
        Disabled,
    };

    String mesh_name;
    String material;
    String normal_map;  // virtual path; empty = use flat normal fallback
    int alpha_mode { 0 }; // 0=Opaque, 1=Clip, 2=Blend, 3=Hash
    CullMode cull_mode { CullMode::Back };
    bool has_normal_map { false };
    u32 material_index { 0 }; // index into Material SSBO
    u32 first_vertex { 0 };
    u32 vertex_count { 0 };
    u32 first_instance { 0 };
    u32 instance_count { 0 };
    float centroid_z { 0.0f }; // average world-Z of mesh vertices; used for blend back-to-front sort
    // Local-space mesh AABB (set at group creation time from MeshAsset::aabb_min/max).
    // Used when expanding the world AABB as new instances are added to the group.
    float aabb_local_min[3] { 0.0f, 0.0f, 0.0f };
    float aabb_local_max[3] { 0.0f, 0.0f, 0.0f };
    // World-space AABB: union of all instance-transformed local AABBs.
    // Uploaded to aabb_world_buffer and tested by the frustum culling compute shader.
    float aabb_world_min[3] { 1e30f, 1e30f, 1e30f };   // start at +∞ (no instances yet)
    float aabb_world_max[3] { -1e30f, -1e30f, -1e30f }; // start at -∞
    // Entity IDs for each instance in draw order — used by rebuild_transforms_only.
    Vector<EntityId> instance_entity_ids;
};

class VulkanMeshBuilder {
public:
    static Vector<Vertex> build_probe_vertices();

    static void build_world_instanced(
        World const& world,
        MeshLibrary& mesh_library,
        Vector<Vertex>& out_mesh_vertices,
        Vector<GPUInstanceData>& out_instance_data,
        Vector<DrawGroup>& out_groups);

    static void rebuild_transforms_only(
        World const& world,
        Vector<DrawGroup>& groups,
        Vector<GPUInstanceData>& out_instance_data,
        Vector<GroupAABBGPU>& out_aabb_data);

    static Vector<TexturedVertex> build_panel_vertices(World const& world);

    struct OverlayView {
        Vector<u8> pixels;
        u32 width { 0 };
        u32 height { 0 };
    };
    static Vector<TexturedVertex> build_overlay_vertices(OverlayView const& overlay_view);

    static Array<float, 16> make_trs_matrix(Transform const& t);
};

}
