/*
 * MyceliumVR - a social VR platform built on the Ladybird browser engine
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "GltfLoader.h"

#if defined(TRACY_ENABLE)
#    include <tracy/Tracy.hpp>
#endif

#include "VirtualFileSystem.h"

#include <AK/ByteString.h>
#include <cstdlib>

#define TINYGLTF3_IMPLEMENTATION
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#include "../ThirdParty/TinyGLTF/tiny_gltf_v3.h"
#pragma GCC diagnostic pop

namespace MyceliumVR {

static String tg3_string_to_ak_string(tg3_str value)
{
    if (!value.data || value.len == 0)
        return {};
    return MUST(String::from_utf8(StringView { value.data, value.len }));
}

static ErrorOr<size_t> checked_buffer_offset(tg3_model const& model, tg3_accessor const& accessor)
{
    if (accessor.buffer_view < 0 || static_cast<uint32_t>(accessor.buffer_view) >= model.buffer_views_count)
        return Error::from_string_literal("Accessor buffer view is out of bounds");
    auto const& buffer_view = model.buffer_views[accessor.buffer_view];
    if (buffer_view.buffer < 0 || static_cast<uint32_t>(buffer_view.buffer) >= model.buffers_count)
        return Error::from_string_literal("Buffer view buffer is out of bounds");

    auto const& buffer = model.buffers[buffer_view.buffer];
    auto total_offset = buffer_view.byte_offset + accessor.byte_offset;
    if (total_offset > buffer.data.count)
        return Error::from_string_literal("Accessor offset exceeds buffer size");
    return static_cast<size_t>(total_offset);
}

template<typename T>
static ErrorOr<void> append_float_accessor_components(Vector<float>& out, tg3_model const& model, tg3_accessor const& accessor, u32 expected_components)
{
    if (accessor.component_type != TG3_COMPONENT_TYPE_FLOAT)
        return Error::from_string_literal("Only float vertex attributes are supported in the first glTF loader pass");

    auto num_components = tg3_num_components(accessor.type);
    if (num_components != static_cast<int32_t>(expected_components))
        return Error::from_string_literal("Accessor component count does not match expected attribute layout");

    auto const& buffer_view = model.buffer_views[accessor.buffer_view];
    auto const& buffer = model.buffers[buffer_view.buffer];
    auto stride = tg3_accessor_byte_stride(&accessor, &buffer_view);
    if (stride <= 0)
        return Error::from_string_literal("Accessor stride is invalid");

    auto base_offset = TRY(checked_buffer_offset(model, accessor));
    auto required_components = static_cast<size_t>(accessor.count) * expected_components;
    TRY(out.try_ensure_capacity(out.size() + required_components));

    for (uint64_t element_index = 0; element_index < accessor.count; ++element_index) {
        auto element_offset = base_offset + (static_cast<size_t>(element_index) * static_cast<size_t>(stride));
        auto bytes_needed = static_cast<size_t>(expected_components) * sizeof(float);
        if (element_offset + bytes_needed > buffer.data.count)
            return Error::from_string_literal("Accessor data exceeds buffer bounds");
        auto const* values = reinterpret_cast<float const*>(buffer.data.data + element_offset);
        for (u32 component = 0; component < expected_components; ++component)
            out.append(values[component]);
    }

    return {};
}

static ErrorOr<void> append_index_accessor(Vector<u32>& out, tg3_model const& model, tg3_accessor const& accessor)
{
    if (accessor.type != TG3_TYPE_SCALAR)
        return Error::from_string_literal("Index accessor must be scalar");

    auto const& buffer_view = model.buffer_views[accessor.buffer_view];
    auto const& buffer = model.buffers[buffer_view.buffer];
    auto stride = tg3_accessor_byte_stride(&accessor, &buffer_view);
    if (stride <= 0)
        return Error::from_string_literal("Index accessor stride is invalid");

    auto component_size = tg3_component_size(accessor.component_type);
    if (component_size <= 0)
        return Error::from_string_literal("Index accessor component size is invalid");

    auto base_offset = TRY(checked_buffer_offset(model, accessor));
    TRY(out.try_ensure_capacity(out.size() + accessor.count));

    for (uint64_t element_index = 0; element_index < accessor.count; ++element_index) {
        auto element_offset = base_offset + (static_cast<size_t>(element_index) * static_cast<size_t>(stride));
        if (element_offset + static_cast<size_t>(component_size) > buffer.data.count)
            return Error::from_string_literal("Index accessor data exceeds buffer bounds");

        u32 value = 0;
        switch (accessor.component_type) {
        case TG3_COMPONENT_TYPE_UNSIGNED_BYTE:
            value = buffer.data.data[element_offset];
            break;
        case TG3_COMPONENT_TYPE_UNSIGNED_SHORT:
            value = *reinterpret_cast<uint16_t const*>(buffer.data.data + element_offset);
            break;
        case TG3_COMPONENT_TYPE_UNSIGNED_INT:
            value = *reinterpret_cast<uint32_t const*>(buffer.data.data + element_offset);
            break;
        default:
            return Error::from_string_literal("Only u8/u16/u32 index accessors are supported in the first glTF loader pass");
        }
        out.append(value);
    }

    return {};
}

// Resolve a tg3_texture_info index to a GltfTextureSlot.
// Returns an empty slot if the index is -1 or out of range.
static GltfTextureSlot resolve_texture_slot(tg3_model const& model, int32_t texture_index)
{
    GltfTextureSlot slot;
    if (texture_index < 0 || static_cast<uint32_t>(texture_index) >= model.textures_count)
        return slot;
    auto const& texture = model.textures[texture_index];
    if (texture.source < 0 || static_cast<uint32_t>(texture.source) >= model.images_count)
        return slot;
    auto const& image = model.images[texture.source];
    if (image.uri.len > 0) {
        slot.uri = tg3_string_to_ak_string(image.uri);
    } else if (image.buffer_view >= 0 && static_cast<uint32_t>(image.buffer_view) < model.buffer_views_count) {
        auto const& bv = model.buffer_views[image.buffer_view];
        if (bv.buffer >= 0 && static_cast<uint32_t>(bv.buffer) < model.buffers_count) {
            auto const& buf = model.buffers[bv.buffer];
            if (bv.byte_length > 0 && bv.byte_offset + bv.byte_length <= buf.data.count)
                slot.data.append(buf.data.data + bv.byte_offset, static_cast<size_t>(bv.byte_length));
        }
    }
    return slot;
}

static Optional<int32_t> find_attribute_accessor(tg3_primitive const& primitive, char const* attribute_name)
{
    for (uint32_t attribute_index = 0; attribute_index < primitive.attributes_count; ++attribute_index) {
        auto const& attribute = primitive.attributes[attribute_index];
        if (tg3_str_equals_cstr(attribute.key, attribute_name))
            return attribute.value;
    }
    return {};
}

// ── Matrix helpers for node-transform traversal ────────────────────────────────

// Column-major 4×4 multiply: out = a * b
static void mat4_mul(float const a[16], float const b[16], float out[16])
{
    for (int col = 0; col < 4; ++col) {
        for (int row = 0; row < 4; ++row) {
            float s = 0.0f;
            for (int k = 0; k < 4; ++k)
                s += a[k * 4 + row] * b[col * 4 + k];
            out[col * 4 + row] = s;
        }
    }
}

// Build a column-major 4×4 from glTF node TRS (doubles → floats).
// q = (x,y,z,w), t = (x,y,z), s = (x,y,z)
static void trs_to_mat4(double const t[3], double const q[4], double const s[3], float out[16])
{
    double qx = q[0], qy = q[1], qz = q[2], qw = q[3];

    // Rotation matrix (column-major) — standard quat→mat formula
    double r00 = 1.0 - 2.0*(qy*qy + qz*qz);
    double r10 = 2.0*(qx*qy + qz*qw);
    double r20 = 2.0*(qx*qz - qy*qw);

    double r01 = 2.0*(qx*qy - qz*qw);
    double r11 = 1.0 - 2.0*(qx*qx + qz*qz);
    double r21 = 2.0*(qy*qz + qx*qw);

    double r02 = 2.0*(qx*qz + qy*qw);
    double r12 = 2.0*(qy*qz - qx*qw);
    double r22 = 1.0 - 2.0*(qx*qx + qy*qy);

    // Column 0: R[:,0] * scale.x
    out[0]  = static_cast<float>(r00 * s[0]);
    out[1]  = static_cast<float>(r10 * s[0]);
    out[2]  = static_cast<float>(r20 * s[0]);
    out[3]  = 0.0f;
    // Column 1: R[:,1] * scale.y
    out[4]  = static_cast<float>(r01 * s[1]);
    out[5]  = static_cast<float>(r11 * s[1]);
    out[6]  = static_cast<float>(r21 * s[1]);
    out[7]  = 0.0f;
    // Column 2: R[:,2] * scale.z
    out[8]  = static_cast<float>(r02 * s[2]);
    out[9]  = static_cast<float>(r12 * s[2]);
    out[10] = static_cast<float>(r22 * s[2]);
    out[11] = 0.0f;
    // Column 3: translation
    out[12] = static_cast<float>(t[0]);
    out[13] = static_cast<float>(t[1]);
    out[14] = static_cast<float>(t[2]);
    out[15] = 1.0f;
}

// Recursively walk the node tree, accumulating world matrices.
// For each node that references a mesh, emit one GltfNodeAsset per primitive.
static void traverse_nodes(
    tg3_model const& model,
    int32_t node_index,
    float const parent_matrix[16],
    GltfSceneAsset& scene)
{
    if (node_index < 0 || static_cast<uint32_t>(node_index) >= model.nodes_count)
        return;

    auto const& node = model.nodes[node_index];

    // Build this node's local matrix
    float local[16];
    if (node.has_matrix) {
        for (int i = 0; i < 16; ++i)
            local[i] = static_cast<float>(node.matrix[i]);
    } else {
        trs_to_mat4(node.translation, node.rotation, node.scale, local);
    }

    // World matrix = parent * local
    float world[16];
    mat4_mul(parent_matrix, local, world);

    // Emit one GltfNodeAsset per mesh primitive on this node
    if (node.mesh >= 0 && static_cast<uint32_t>(node.mesh) < model.meshes_count) {
        auto const& gltf_mesh = model.meshes[node.mesh];
        // Find which index in scene.meshes this corresponds to (meshes are added in order)
        int32_t scene_mesh_index = static_cast<int32_t>(node.mesh);

        for (uint32_t prim_index = 0; prim_index < gltf_mesh.primitives_count; ++prim_index) {
            auto const& prim = gltf_mesh.primitives[prim_index];
            GltfNodeAsset na;
            na.name = tg3_string_to_ak_string(node.name);
            na.mesh_index = scene_mesh_index;
            na.primitive_index = static_cast<int32_t>(prim_index);
            for (int i = 0; i < 16; ++i)
                na.world_matrix[i] = world[i];

            // Material name from the primitive — use synthetic "materialN" for unnamed
            // materials (matching the naming in load_scene's material loop) so the
            // renderer can look up the correct MaterialAsset even when glTF materials
            // have no name field (e.g. Sponza from glTF-Sample-Assets).
            if (prim.material >= 0 && static_cast<uint32_t>(prim.material) < model.materials_count) {
                na.material_name = tg3_string_to_ak_string(model.materials[prim.material].name);
                if (na.material_name.is_empty())
                    na.material_name = MUST(String::formatted("material{}", prim.material));
            } else {
                na.material_name = "default"_string;
            }

            scene.nodes.append(move(na));
        }
    }

    // Recurse into children
    for (uint32_t ci = 0; ci < node.children_count; ++ci)
        traverse_nodes(model, node.children[ci], world, scene);
}

// ── VFS filesystem callbacks for tg3_parse_options ────────────────────────────
// Lets TinyGLTF3 load external .bin buffers and loose texture files through our
// VirtualFileSystem instead of raw fopen(). user_data = VirtualFileSystem const*.

static int32_t vfs_read_file(uint8_t** out_data, uint64_t* out_size,
    char const* path, uint32_t path_len, void* user_data)
{
    auto* vfs = static_cast<VirtualFileSystem const*>(user_data);
    auto result = vfs->read_file(StringView { path, path_len });
    if (result.is_error())
        return 0;
    auto buf = result.release_value();
    auto* bytes = static_cast<uint8_t*>(::malloc(buf.size()));
    if (!bytes)
        return 0;
    ::memcpy(bytes, buf.data(), buf.size());
    *out_data = bytes;
    *out_size = static_cast<uint64_t>(buf.size());
    return 1;
}

static void vfs_free_file(uint8_t* data, uint64_t, void*)
{
    ::free(data);
}

static int32_t vfs_file_exists(char const* path, uint32_t path_len, void* user_data)
{
    auto* vfs = static_cast<VirtualFileSystem const*>(user_data);
    return vfs->exists(StringView { path, path_len }) ? 1 : 0;
}

GltfLoader::GltfLoader(VirtualFileSystem const& file_system)
    : m_file_system(file_system)
{
}

ErrorOr<GltfSceneAsset> GltfLoader::load_scene(StringView virtual_path) const
{
#if defined(TRACY_ENABLE)
    ZoneScopedN("Asset/GltfLoad");
    ZoneText(virtual_path.characters_without_null_termination(), virtual_path.length());
#endif
    auto source = TRY(m_file_system.read_file(virtual_path));

    tg3_model model {};
    tg3_error_stack errors {};
    tg3_parse_options options {};
    tg3_error_stack_init(&errors);
    tg3_parse_options_init(&options);

    // Don't decode image pixels — we read raw bytes ourselves via resolve_texture_slot.
    options.images_as_is = 1;

    // Wire VFS filesystem callbacks so external .bin buffers and loose texture
    // URIs are loaded through our VirtualFileSystem, not raw fopen().
    options.fs.read_file   = vfs_read_file;
    options.fs.free_file   = vfs_free_file;
    options.fs.file_exists = vfs_file_exists;
    options.fs.user_data   = const_cast<VirtualFileSystem*>(&m_file_system);

    // Extract the directory portion of virtual_path as base_dir.
    // For "world://pkg/assets/Sponza.gltf" this is "world://pkg/assets".
    // TinyGLTF3 prepends base_dir + "/" to relative URIs in buffers and images.
    StringView base_dir;
    if (auto slash = virtual_path.find_last('/'); slash.has_value())
        base_dir = virtual_path.substring_view(0, *slash);

    int parse_result;
    {
#if defined(TRACY_ENABLE)
        ZoneScopedN("Asset/GltfParse");
#endif
        parse_result = tg3_parse_auto(
            &model,
            &errors,
            reinterpret_cast<uint8_t const*>(source.data()),
            source.size(),
            base_dir.is_empty() ? nullptr : base_dir.characters_without_null_termination(),
            static_cast<uint32_t>(base_dir.length()),
            &options);
    }

    if (parse_result != TG3_OK) {
        tg3_error_stack_free(&errors);
        tg3_model_free(&model);
        return Error::from_string_literal("Failed to parse glTF file");
    }

    GltfSceneAsset scene;
    if (model.default_scene >= 0 && static_cast<uint32_t>(model.default_scene) < model.scenes_count)
        scene.name = tg3_string_to_ak_string(model.scenes[model.default_scene].name);
    if (scene.name.is_empty())
        scene.name = MUST(String::from_utf8(virtual_path));

    for (uint32_t mesh_index = 0; mesh_index < model.meshes_count; ++mesh_index) {
        auto const& mesh = model.meshes[mesh_index];
        GltfMeshAsset mesh_asset;
        mesh_asset.name = tg3_string_to_ak_string(mesh.name);
        if (mesh_asset.name.is_empty())
            mesh_asset.name = MUST(String::formatted("mesh{}", mesh_index));

        for (uint32_t primitive_index = 0; primitive_index < mesh.primitives_count; ++primitive_index) {
            auto const& primitive = mesh.primitives[primitive_index];
            GltfPrimitive primitive_asset;

            if (primitive.material >= 0 && static_cast<uint32_t>(primitive.material) < model.materials_count) {
                primitive_asset.material_name = tg3_string_to_ak_string(model.materials[primitive.material].name);
                if (primitive_asset.material_name.is_empty())
                    primitive_asset.material_name = MUST(String::formatted("material{}", primitive.material));
            } else {
                primitive_asset.material_name = "default"_string;
            }

            if (auto positions_accessor_index = find_attribute_accessor(primitive, "POSITION"); positions_accessor_index.has_value()) {
                if (*positions_accessor_index < 0 || static_cast<uint32_t>(*positions_accessor_index) >= model.accessors_count)
                    return Error::from_string_literal("POSITION accessor index is out of bounds");
                TRY(append_float_accessor_components<float>(primitive_asset.positions, model, model.accessors[*positions_accessor_index], 3));
            }

            if (auto normals_accessor_index = find_attribute_accessor(primitive, "NORMAL"); normals_accessor_index.has_value()) {
                if (*normals_accessor_index < 0 || static_cast<uint32_t>(*normals_accessor_index) >= model.accessors_count)
                    return Error::from_string_literal("NORMAL accessor index is out of bounds");
                TRY(append_float_accessor_components<float>(primitive_asset.normals, model, model.accessors[*normals_accessor_index], 3));
            }

            if (auto texcoord_accessor_index = find_attribute_accessor(primitive, "TEXCOORD_0"); texcoord_accessor_index.has_value()) {
                if (*texcoord_accessor_index < 0 || static_cast<uint32_t>(*texcoord_accessor_index) >= model.accessors_count)
                    return Error::from_string_literal("TEXCOORD_0 accessor index is out of bounds");
                TRY(append_float_accessor_components<float>(primitive_asset.texcoords, model, model.accessors[*texcoord_accessor_index], 2));
            }

            if (auto tangents_accessor_index = find_attribute_accessor(primitive, "TANGENT"); tangents_accessor_index.has_value()) {
                if (*tangents_accessor_index < 0 || static_cast<uint32_t>(*tangents_accessor_index) >= model.accessors_count)
                    return Error::from_string_literal("TANGENT accessor index is out of bounds");
                // glTF TANGENT is vec4: xyz = tangent direction, w = bitangent sign.
                TRY(append_float_accessor_components<float>(primitive_asset.tangents, model, model.accessors[*tangents_accessor_index], 4));
            }

            if (primitive.indices >= 0) {
                if (static_cast<uint32_t>(primitive.indices) >= model.accessors_count)
                    return Error::from_string_literal("Index accessor index is out of bounds");
                TRY(append_index_accessor(primitive_asset.indices, model, model.accessors[primitive.indices]));
            }

            mesh_asset.primitives.append(move(primitive_asset));
        }

        scene.meshes.append(move(mesh_asset));
    }

    for (uint32_t material_index = 0; material_index < model.materials_count; ++material_index) {
        auto const& mat = model.materials[material_index];
        GltfMaterialAsset material_asset;

        material_asset.name = tg3_string_to_ak_string(mat.name);
        if (material_asset.name.is_empty())
            material_asset.name = MUST(String::formatted("material{}", material_index));

        auto const& pbr = mat.pbr_metallic_roughness;
        material_asset.albedo_texture             = resolve_texture_slot(model, pbr.base_color_texture.index);
        material_asset.metallic_roughness_texture = resolve_texture_slot(model, pbr.metallic_roughness_texture.index);
        material_asset.normal_texture             = resolve_texture_slot(model, mat.normal_texture.index);
        material_asset.occlusion_texture          = resolve_texture_slot(model, mat.occlusion_texture.index);
        material_asset.emissive_texture           = resolve_texture_slot(model, mat.emissive_texture.index);

        for (int j = 0; j < 4; ++j)
            material_asset.base_color_factor[j] = static_cast<float>(pbr.base_color_factor[j]);
        material_asset.metallic_factor  = static_cast<float>(pbr.metallic_factor);
        material_asset.roughness_factor = static_cast<float>(pbr.roughness_factor);
        for (int j = 0; j < 3; ++j)
            material_asset.emissive_factor[j] = static_cast<float>(mat.emissive_factor[j]);
        material_asset.alpha_cutoff = static_cast<float>(mat.alpha_cutoff);

        if (tg3_str_equals_cstr(mat.alpha_mode, "MASK"))
            material_asset.alpha_mode = GltfMaterialAsset::AlphaMode::Clip;
        else if (tg3_str_equals_cstr(mat.alpha_mode, "BLEND"))
            material_asset.alpha_mode = GltfMaterialAsset::AlphaMode::Blend;
        else
            material_asset.alpha_mode = GltfMaterialAsset::AlphaMode::Opaque;

        material_asset.cull_mode = mat.double_sided ? GltfMaterialAsset::CullMode::Disabled : GltfMaterialAsset::CullMode::Back;

        scene.materials.append(move(material_asset));
    }

    // Build node list: walk the default scene's root nodes, accumulating world matrices.
    // Each (node × primitive) pair becomes one GltfNodeAsset in scene.nodes.
    // If the file has no scene graph (only raw meshes), we synthesise one node per mesh
    // primitive at identity so spawnScene still works for simple single-mesh files.
    if (model.scenes_count > 0 && model.default_scene >= 0
        && static_cast<uint32_t>(model.default_scene) < model.scenes_count) {
        auto const& default_scene = model.scenes[model.default_scene];
        float identity[16] = { 1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1 };
        for (uint32_t ri = 0; ri < default_scene.nodes_count; ++ri)
            traverse_nodes(model, default_scene.nodes[ri], identity, scene);
    } else {
        // No scene graph: emit one node per (mesh × primitive) at identity.
        float identity[16] = { 1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1 };
        for (uint32_t mi = 0; mi < model.meshes_count; ++mi) {
            auto const& gltf_mesh = model.meshes[mi];
            for (uint32_t pi = 0; pi < gltf_mesh.primitives_count; ++pi) {
                auto const& prim = gltf_mesh.primitives[pi];
                GltfNodeAsset na;
                na.mesh_index = static_cast<int32_t>(mi);
                na.primitive_index = static_cast<int32_t>(pi);
                for (int i = 0; i < 16; ++i) na.world_matrix[i] = identity[i];
                if (prim.material >= 0 && static_cast<uint32_t>(prim.material) < model.materials_count) {
                    na.material_name = tg3_string_to_ak_string(model.materials[prim.material].name);
                    if (na.material_name.is_empty())
                        na.material_name = MUST(String::formatted("material{}", prim.material));
                } else {
                    na.material_name = "default"_string;
                }
                scene.nodes.append(move(na));
            }
        }
    }

    tg3_error_stack_free(&errors);
    tg3_model_free(&model);
    return scene;
}

}
