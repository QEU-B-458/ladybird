/*
 * MyceliumVR - a social VR platform built on the Ladybird browser engine
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "MaterialLibrary.h"

#include <AK/StringHash.h>

namespace MyceliumVR {

static MaterialTextureSlot resolve_slot(GltfTextureSlot const& gltf_slot, StringView base_virtual_dir)
{
    MaterialTextureSlot slot;
    if (!gltf_slot.is_set())
        return slot;
    if (!gltf_slot.uri.is_empty()) {
        // External texture: combine the base directory with the relative URI.
        if (base_virtual_dir.is_empty())
            slot.path = gltf_slot.uri;
        else
            slot.path = MUST(String::formatted("{}/{}", base_virtual_dir, gltf_slot.uri));
    } else {
        // Embedded texture: carry the raw bytes; the renderer will upload them directly.
        slot.data = gltf_slot.data;
        auto hash = string_hash(reinterpret_cast<char const*>(slot.data.data()), slot.data.size());
        slot.embedded_cache_key = MUST(String::formatted("embedded_{:08x}_{}", hash, slot.data.size()));
    }
    return slot;
}

void MaterialLibrary::register_from_gltf(GltfSceneAsset const& scene, StringView base_virtual_dir)
{
    for (auto const& gltf_mat : scene.materials) {
        // Don't overwrite an already-registered material with the same name.
        if (m_cache.contains(gltf_mat.name))
            continue;

        MaterialAsset asset;

        asset.albedo             = resolve_slot(gltf_mat.albedo_texture,             base_virtual_dir);
        asset.metallic_roughness = resolve_slot(gltf_mat.metallic_roughness_texture, base_virtual_dir);
        asset.normal             = resolve_slot(gltf_mat.normal_texture,             base_virtual_dir);
        asset.emissive           = resolve_slot(gltf_mat.emissive_texture,           base_virtual_dir);
        asset.occlusion          = resolve_slot(gltf_mat.occlusion_texture,          base_virtual_dir);

        for (int i = 0; i < 4; ++i)
            asset.base_color_factor[i] = gltf_mat.base_color_factor[i];
        asset.metallic_factor  = gltf_mat.metallic_factor;
        asset.roughness_factor = gltf_mat.roughness_factor;
        for (int i = 0; i < 3; ++i)
            asset.emissive_factor[i] = gltf_mat.emissive_factor[i];
        asset.alpha_cutoff = gltf_mat.alpha_cutoff;

        switch (gltf_mat.alpha_mode) {
        case GltfMaterialAsset::AlphaMode::Clip:  asset.alpha_mode = MaterialAsset::AlphaMode::Clip;  break;
        case GltfMaterialAsset::AlphaMode::Blend: asset.alpha_mode = MaterialAsset::AlphaMode::Blend; break;
        default:                                  asset.alpha_mode = MaterialAsset::AlphaMode::Opaque; break;
        }

        switch (gltf_mat.cull_mode) {
        case GltfMaterialAsset::CullMode::Disabled: asset.cull_mode = MaterialAsset::CullMode::Disabled; break;
        default:                                    asset.cull_mode = MaterialAsset::CullMode::Back; break;
        }

        m_cache.set(gltf_mat.name, move(asset));
    }
}

void MaterialLibrary::register_alias(StringView alias_name, StringView source_name)
{
    auto alias_key = MUST(String::from_utf8(alias_name));
    if (m_cache.contains(alias_key))
        return;
    auto source_key = MUST(String::from_utf8(source_name));
    auto it = m_cache.find(source_key);
    if (it == m_cache.end())
        return;
    m_cache.set(alias_key, it->value);
}

MaterialAsset const* MaterialLibrary::resolve(StringView name) const
{
    auto it = m_cache.find(name);
    if (it == m_cache.end())
        return nullptr;
    return &it->value;
}

void MaterialLibrary::unload_world_resources(u32)
{
    // Clear entire cache for now.
    m_cache.clear();
}

}
