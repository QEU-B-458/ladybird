/*
 * MyceliumVR - a social VR platform built on the Ladybird browser engine
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include "../World/World.h"

#include <AK/Error.h>
#include <AK/String.h>
#include <AK/Vector.h>

namespace MyceliumVR {

class VirtualFileSystem;

struct GltfPrimitive {
    Vector<float> positions;
    Vector<float> normals;
    Vector<float> texcoords;
    Vector<float> tangents; // xyzw, 4 floats per vertex; w = bitangent sign (+1 or -1)
    Vector<u32> indices;
    String material_name;
};

struct GltfMeshAsset {
    String name;
    Vector<GltfPrimitive> primitives;
};

// A reference to a texture within a glTF asset.
// Exactly one of uri or data is non-empty; both empty means the slot is unset.
// uri: relative file path (external .gltf textures)
// data: raw PNG/JPEG bytes from a GLB buffer view (embedded textures)
struct GltfTextureSlot {
    String uri;
    Vector<u8> data;

    bool is_set() const { return !uri.is_empty() || !data.is_empty(); }
};

struct GltfMaterialAsset {
    enum class CullMode {
        Back,
        Disabled,
    };

    String name;

    GltfTextureSlot albedo_texture;
    GltfTextureSlot metallic_roughness_texture; // G = roughness, B = metallic (glTF packed)
    GltfTextureSlot normal_texture;
    GltfTextureSlot emissive_texture;
    GltfTextureSlot occlusion_texture;

    float base_color_factor[4] { 1.0f, 1.0f, 1.0f, 1.0f };
    float metallic_factor { 1.0f };
    float roughness_factor { 1.0f };
    float emissive_factor[3] { 0.0f, 0.0f, 0.0f };
    float alpha_cutoff { 0.5f };

    enum class AlphaMode { Opaque, Clip, Blend };
    AlphaMode alpha_mode { AlphaMode::Opaque };
    CullMode cull_mode { CullMode::Back };
};

// One entry per (node × primitive) in the glTF scene graph.
// world_matrix is pre-multiplied parent-to-root, column-major.
// Use these to spawn one entity per entry with the correct transform and material.
struct GltfNodeAsset {
    String name;
    int32_t mesh_index { -1 };     // index into GltfSceneAsset::meshes
    int32_t primitive_index { 0 }; // index into meshes[mesh_index].primitives
    float world_matrix[16] {       // column-major 4×4, identity by default
        1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1
    };
    String material_name; // glTF material name for this primitive
};

struct GltfSceneAsset {
    String name;
    Vector<GltfMeshAsset> meshes;
    Vector<GltfMaterialAsset> materials;
    Vector<GltfNodeAsset> nodes; // one entry per (node × primitive) in scene graph
};

class GltfLoader {
public:
    explicit GltfLoader(VirtualFileSystem const&);

    ErrorOr<GltfSceneAsset> load_scene(StringView virtual_path) const;

private:
    VirtualFileSystem const& m_file_system;
};

}
