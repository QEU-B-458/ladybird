/*
 * MyceliumVR - a social VR platform built on the Ladybird browser engine
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "MeshLibrary.h"

#include "VirtualFileSystem.h"

#include <AK/Math.h>

namespace MyceliumVR {

static void compute_aabb(MeshAsset& mesh)
{
    if (mesh.positions.is_empty()) {
        mesh.aabb_min[0] = mesh.aabb_min[1] = mesh.aabb_min[2] = 0.0f;
        mesh.aabb_max[0] = mesh.aabb_max[1] = mesh.aabb_max[2] = 0.0f;
        return;
    }
    mesh.aabb_min[0] = mesh.aabb_min[1] = mesh.aabb_min[2] =  AK::Infinity<float>;
    mesh.aabb_max[0] = mesh.aabb_max[1] = mesh.aabb_max[2] = -AK::Infinity<float>;
    size_t const vertex_count = mesh.positions.size() / 3;
    for (size_t i = 0; i < vertex_count; ++i) {
        for (int axis = 0; axis < 3; ++axis) {
            float v = mesh.positions[i * 3 + axis];
            if (v < mesh.aabb_min[axis]) mesh.aabb_min[axis] = v;
            if (v > mesh.aabb_max[axis]) mesh.aabb_max[axis] = v;
        }
    }
}

MeshLibrary::MeshLibrary(VirtualFileSystem const* file_system, MaterialLibrary* material_library)
    : m_file_system(file_system)
    , m_material_library(material_library)
{
}

ErrorOr<MeshAsset const*> MeshLibrary::resolve_mesh(String const& mesh_name)
{
    if (auto it = m_cached_meshes.find(mesh_name); it != m_cached_meshes.end())
        return &it->value;

    auto mesh = TRY(load_mesh(mesh_name));
    m_cached_meshes.set(mesh_name, move(mesh));
    return &m_cached_meshes.find(mesh_name)->value;
}

ErrorOr<MeshAsset> MeshLibrary::load_mesh(String const& mesh_name)
{
    if (mesh_name.is_empty() || mesh_name == "cube"_string)
        return make_cube_mesh();

    auto sv = mesh_name.bytes_as_string_view();

    // Sub-mesh path: "world://path/File.glb#N" — return the Nth node's mesh.
    // Ensure the base path is loaded first (which registers all sub-meshes as a side-effect).
    if (auto hash_pos = sv.find('#'); hash_pos.has_value()) {
        auto base_sv = sv.substring_view(0, *hash_pos);
        auto base_str = TRY(String::from_utf8(base_sv));
        // Load base (if not cached) — this registers all "base#N" sub-meshes.
        if (!m_cached_meshes.contains(base_str))
            TRY(resolve_mesh(base_str)); // fills cache for base and all sub-paths
        // The sub-mesh should now be in cache.
        if (auto it = m_cached_meshes.find(mesh_name); it != m_cached_meshes.end())
            return it->value; // caller will re-insert via resolve_mesh — return a copy here
        return Error::from_string_literal("Sub-mesh index out of range");
    }

    if (sv.ends_with(".glb"sv) || sv.ends_with(".gltf"sv)) {
        if (!m_file_system)
            return Error::from_string_literal("Cannot load glTF mesh without a virtual file system");
        GltfLoader loader(*m_file_system);
        auto scene = TRY(loader.load_scene(sv));

        // Register materials from this scene so the renderer can look them up by name.
        if (m_material_library) {
            StringView base_dir = sv;
            if (auto slash = base_dir.find_last('/'); slash.has_value())
                base_dir = base_dir.substring_view(0, *slash);
            else
                base_dir = {};
            m_material_library->register_from_gltf(scene, base_dir);

            if (!scene.materials.is_empty())
                m_material_library->register_alias(sv, scene.materials[0].name);
        }

        // Eagerly register all (node × primitive) sub-meshes under "path#N".
        TRY(load_and_register_scene_sub_meshes(mesh_name, scene));

        return make_mesh_from_gltf_scene(scene);
    }

    return Error::from_string_literal("Unknown mesh asset id");
}

// Register one MeshAsset per GltfNodeAsset under "base_path#N".
// Also registers the per-primitive material alias for each sub-mesh.
ErrorOr<void> MeshLibrary::load_and_register_scene_sub_meshes(String const& base_path, GltfSceneAsset const& scene)
{
    for (size_t i = 0; i < scene.nodes.size(); ++i) {
        auto const& node = scene.nodes[i];
        auto sub_key = TRY(String::formatted("{}#{}", base_path, i));

        if (m_cached_meshes.contains(sub_key))
            continue;

        auto sub_mesh = TRY(make_sub_mesh_from_node(scene, node));
        m_cached_meshes.set(sub_key, move(sub_mesh));

        if (m_material_library && !node.material_name.is_empty()
            && node.material_name != "default"_string) {
            m_material_library->register_alias(sub_key, node.material_name);
        }
    }
    return {};
}

// Build a MeshAsset for a single (node × primitive) in local primitive space.
// We do NOT bake the world matrix here — the entity transform set by spawnScene
// (decomposed from node.world_matrix) is what the renderer uses to place the mesh.
// Baking and then also setting the entity transform would double-apply it.
ErrorOr<MeshAsset> MeshLibrary::make_sub_mesh_from_node(GltfSceneAsset const& scene, GltfNodeAsset const& node)
{
    if (node.mesh_index < 0 || static_cast<size_t>(node.mesh_index) >= scene.meshes.size())
        return Error::from_string_literal("Sub-mesh node has invalid mesh_index");
    auto const& gltf_mesh = scene.meshes[node.mesh_index];
    if (node.primitive_index < 0 || static_cast<size_t>(node.primitive_index) >= gltf_mesh.primitives.size())
        return Error::from_string_literal("Sub-mesh node has invalid primitive_index");
    auto const& prim = gltf_mesh.primitives[node.primitive_index];

    GltfSceneAsset single;
    GltfMeshAsset single_mesh;
    single_mesh.primitives.append(prim);
    single.meshes.append(move(single_mesh));
    return make_mesh_from_gltf_scene(single);
}

MeshAsset MeshLibrary::make_cube_mesh()
{
    MeshAsset mesh;
    // 6 faces × 2 triangles × 3 vertices = 36 vertices, 108 floats each for positions/normals.
    static constexpr float cube_positions[] = {
        // front  (+Z)
        -0.5f, -0.5f,  0.5f,  0.5f, -0.5f,  0.5f,  0.5f,  0.5f,  0.5f,
        -0.5f, -0.5f,  0.5f,  0.5f,  0.5f,  0.5f, -0.5f,  0.5f,  0.5f,
        // back   (-Z)
        -0.5f, -0.5f, -0.5f, -0.5f,  0.5f, -0.5f,  0.5f,  0.5f, -0.5f,
        -0.5f, -0.5f, -0.5f,  0.5f,  0.5f, -0.5f,  0.5f, -0.5f, -0.5f,
        // left   (-X)
        -0.5f, -0.5f, -0.5f, -0.5f, -0.5f,  0.5f, -0.5f,  0.5f,  0.5f,
        -0.5f, -0.5f, -0.5f, -0.5f,  0.5f,  0.5f, -0.5f,  0.5f, -0.5f,
        // right  (+X)
         0.5f, -0.5f, -0.5f,  0.5f,  0.5f, -0.5f,  0.5f,  0.5f,  0.5f,
         0.5f, -0.5f, -0.5f,  0.5f,  0.5f,  0.5f,  0.5f, -0.5f,  0.5f,
        // top    (+Y)
        -0.5f,  0.5f, -0.5f, -0.5f,  0.5f,  0.5f,  0.5f,  0.5f,  0.5f,
        -0.5f,  0.5f, -0.5f,  0.5f,  0.5f,  0.5f,  0.5f,  0.5f, -0.5f,
        // bottom (-Y)
        -0.5f, -0.5f, -0.5f,  0.5f, -0.5f, -0.5f,  0.5f, -0.5f,  0.5f,
        -0.5f, -0.5f, -0.5f,  0.5f, -0.5f,  0.5f, -0.5f, -0.5f,  0.5f,
    };
    static constexpr float cube_normals[] = {
        // front  (+Z)
         0.0f,  0.0f,  1.0f,  0.0f,  0.0f,  1.0f,  0.0f,  0.0f,  1.0f,
         0.0f,  0.0f,  1.0f,  0.0f,  0.0f,  1.0f,  0.0f,  0.0f,  1.0f,
        // back   (-Z)
         0.0f,  0.0f, -1.0f,  0.0f,  0.0f, -1.0f,  0.0f,  0.0f, -1.0f,
         0.0f,  0.0f, -1.0f,  0.0f,  0.0f, -1.0f,  0.0f,  0.0f, -1.0f,
        // left   (-X)
        -1.0f,  0.0f,  0.0f, -1.0f,  0.0f,  0.0f, -1.0f,  0.0f,  0.0f,
        -1.0f,  0.0f,  0.0f, -1.0f,  0.0f,  0.0f, -1.0f,  0.0f,  0.0f,
        // right  (+X)
         1.0f,  0.0f,  0.0f,  1.0f,  0.0f,  0.0f,  1.0f,  0.0f,  0.0f,
         1.0f,  0.0f,  0.0f,  1.0f,  0.0f,  0.0f,  1.0f,  0.0f,  0.0f,
        // top    (+Y)
         0.0f,  1.0f,  0.0f,  0.0f,  1.0f,  0.0f,  0.0f,  1.0f,  0.0f,
         0.0f,  1.0f,  0.0f,  0.0f,  1.0f,  0.0f,  0.0f,  1.0f,  0.0f,
        // bottom (-Y)
         0.0f, -1.0f,  0.0f,  0.0f, -1.0f,  0.0f,  0.0f, -1.0f,  0.0f,
         0.0f, -1.0f,  0.0f,  0.0f, -1.0f,  0.0f,  0.0f, -1.0f,  0.0f,
    };
    mesh.positions.append(cube_positions, AK::array_size(cube_positions));
    mesh.normals.append(cube_normals, AK::array_size(cube_normals));
    compute_aabb(mesh);
    return mesh;
}

ErrorOr<MeshAsset> MeshLibrary::make_mesh_from_gltf_scene(GltfSceneAsset const& scene)
{
    MeshAsset mesh;
    for (auto const& mesh_asset : scene.meshes) {
        for (auto const& primitive : mesh_asset.primitives) {
            bool has_normals = primitive.normals.size() == primitive.positions.size();
            bool has_texcoords = !primitive.texcoords.is_empty();
            bool has_tangents = !primitive.tangents.is_empty();

            if (!primitive.indices.is_empty()) {
                for (auto index : primitive.indices) {
                    auto pos_base = static_cast<size_t>(index) * 3;
                    if (pos_base + 2 >= primitive.positions.size())
                        return Error::from_string_literal("glTF primitive index exceeds position buffer");
                    mesh.positions.append(primitive.positions[pos_base + 0]);
                    mesh.positions.append(primitive.positions[pos_base + 1]);
                    mesh.positions.append(primitive.positions[pos_base + 2]);

                    if (has_normals) {
                        mesh.normals.append(primitive.normals[pos_base + 0]);
                        mesh.normals.append(primitive.normals[pos_base + 1]);
                        mesh.normals.append(primitive.normals[pos_base + 2]);
                    }

                    if (has_texcoords) {
                        auto uv_base = static_cast<size_t>(index) * 2;
                        if (uv_base + 1 < primitive.texcoords.size()) {
                            mesh.texcoords.append(primitive.texcoords[uv_base + 0]);
                            mesh.texcoords.append(primitive.texcoords[uv_base + 1]);
                        } else {
                            mesh.texcoords.append(0.0f);
                            mesh.texcoords.append(0.0f);
                        }
                    }

                    if (has_tangents) {
                        auto tan_base = static_cast<size_t>(index) * 4;
                        if (tan_base + 3 < primitive.tangents.size()) {
                            mesh.tangents.append(primitive.tangents[tan_base + 0]);
                            mesh.tangents.append(primitive.tangents[tan_base + 1]);
                            mesh.tangents.append(primitive.tangents[tan_base + 2]);
                            mesh.tangents.append(primitive.tangents[tan_base + 3]);
                        } else {
                            mesh.tangents.append(1.0f);
                            mesh.tangents.append(0.0f);
                            mesh.tangents.append(0.0f);
                            mesh.tangents.append(1.0f);
                        }
                    }
                }
            } else {
                // Non-indexed: copy arrays directly.
                mesh.positions.append(primitive.positions.data(), primitive.positions.size());
                if (has_normals)
                    mesh.normals.append(primitive.normals.data(), primitive.normals.size());
                if (has_texcoords)
                    mesh.texcoords.append(primitive.texcoords.data(), primitive.texcoords.size());
                if (has_tangents)
                    mesh.tangents.append(primitive.tangents.data(), primitive.tangents.size());
            }

            // If this primitive had normals but others didn't (mixed), pad with zeros to keep arrays parallel.
            if (!mesh.normals.is_empty() && mesh.normals.size() < mesh.positions.size()) {
                auto missing = mesh.positions.size() - mesh.normals.size();
                for (size_t i = 0; i < missing; ++i)
                    mesh.normals.append(0.0f);
            }
        }
    }

    if (mesh.positions.is_empty())
        return Error::from_string_literal("glTF scene did not contain any renderable triangle positions");

    // If normals or tangents ended up partially filled, drop them.
    if (!mesh.normals.is_empty() && mesh.normals.size() != mesh.positions.size())
        mesh.normals.clear();
    auto expected_tangents = (mesh.positions.size() / 3) * 4;
    if (!mesh.tangents.is_empty() && mesh.tangents.size() != expected_tangents)
        mesh.tangents.clear();

    compute_aabb(mesh);
    return mesh;
}

}
