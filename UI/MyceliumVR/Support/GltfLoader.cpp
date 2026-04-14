/*
 * MyceliumVR - a social VR platform built on the Ladybird browser engine
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "GltfLoader.h"

#include "VirtualFileSystem.h"

#include <AK/ByteString.h>

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

GltfLoader::GltfLoader(VirtualFileSystem const& file_system)
    : m_file_system(file_system)
{
}

ErrorOr<GltfSceneAsset> GltfLoader::load_scene(StringView virtual_path) const
{
    auto source = TRY(m_file_system.read_file(virtual_path));

    tg3_model model {};
    tg3_error_stack errors {};
    tg3_parse_options options {};
    tg3_error_stack_init(&errors);
    tg3_parse_options_init(&options);

    auto parse_result = tg3_parse_auto(
        &model,
        &errors,
        reinterpret_cast<uint8_t const*>(source.data()),
        source.size(),
        nullptr,
        0,
        &options);

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

            if (primitive.material >= 0 && static_cast<uint32_t>(primitive.material) < model.materials_count)
                primitive_asset.material_name = tg3_string_to_ak_string(model.materials[primitive.material].name);
            if (primitive_asset.material_name.is_empty())
                primitive_asset.material_name = "default"_string;

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

        scene.materials.append(move(material_asset));
    }

    tg3_error_stack_free(&errors);
    tg3_model_free(&model);
    return scene;
}

}
