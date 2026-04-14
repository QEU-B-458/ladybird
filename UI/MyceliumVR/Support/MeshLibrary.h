/*
 * MyceliumVR - a social VR platform built on the Ladybird browser engine
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include "GltfLoader.h"
#include "MaterialLibrary.h"

#include <AK/Error.h>
#include <AK/HashMap.h>
#include <AK/Optional.h>
#include <AK/String.h>
#include <AK/Vector.h>

namespace MyceliumVR {

class VirtualFileSystem;

struct MeshAsset {
    // All arrays are index-expanded and parallel: positions[i*3..i*3+2] matches normals[i*3..i*3+2].
    Vector<float> positions; // xyz,  3 floats per vertex
    Vector<float> normals;   // xyz,  3 floats per vertex; empty = renderer computes flat normals
    Vector<float> texcoords; // uv,   2 floats per vertex; empty = no texcoords
    Vector<float> tangents;  // xyzw, 4 floats per vertex; w = bitangent sign; empty = no tangents

    // Axis-aligned bounding box in local (model) space. Computed from positions at load time.
    // Used by GPU frustum culling: the renderer transforms these into clip space per draw group.
    float aabb_min[3] { 0.0f, 0.0f, 0.0f };
    float aabb_max[3] { 0.0f, 0.0f, 0.0f };
};

class MeshLibrary {
public:
    // material_library: optional; if non-null, materials from loaded glTF scenes are
    // automatically registered into it alongside the mesh geometry.
    explicit MeshLibrary(VirtualFileSystem const* = nullptr, MaterialLibrary* = nullptr);

    ErrorOr<MeshAsset const*> resolve_mesh(String const& mesh_name);

private:
    ErrorOr<MeshAsset> load_mesh(String const& mesh_name);
    static MeshAsset make_cube_mesh();
    static ErrorOr<MeshAsset> make_mesh_from_gltf_scene(GltfSceneAsset const&);

    VirtualFileSystem const* m_file_system { nullptr };
    MaterialLibrary* m_material_library { nullptr };
    mutable HashMap<String, MeshAsset> m_cached_meshes;
};

}
