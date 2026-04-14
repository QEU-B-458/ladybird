/*
 * MyceliumVR - a social VR platform built on the Ladybird browser engine
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include "GltfLoader.h"

#include <AK/Error.h>
#include <AK/HashMap.h>
#include <AK/String.h>
#include <AK/StringView.h>
#include <AK/Vector.h>

namespace MyceliumVR {

// A resolved texture slot: exactly one of path or data is non-empty, or neither (slot unset).
// path: VFS virtual path (e.g. "world://models/albedo.png") — load via TextureLibrary.
// data: raw PNG/JPEG bytes for a texture embedded in a GLB file — no VFS entry exists.
struct MaterialTextureSlot {
    String path;
    Vector<u8> data;

    bool is_set() const { return !path.is_empty() || !data.is_empty(); }
};

// Data-only PBR material description. No GPU state.
// All texture slots are optional; unset slots should use renderer-side fallback textures.
struct MaterialAsset {
    MaterialTextureSlot albedo;
    MaterialTextureSlot metallic_roughness; // G = roughness, B = metallic (glTF packed)
    MaterialTextureSlot normal;
    MaterialTextureSlot emissive;
    MaterialTextureSlot occlusion;

    float base_color_factor[4] { 1.0f, 1.0f, 1.0f, 1.0f };
    float metallic_factor  { 1.0f };
    float roughness_factor { 1.0f };
    float emissive_factor[3] { 0.0f, 0.0f, 0.0f };
    float alpha_cutoff { 0.5f };

    enum class AlphaMode { Opaque, Clip, Blend, Hash };
    AlphaMode alpha_mode { AlphaMode::Opaque };
};

// Caches MaterialAsset by name. Populated by registering parsed glTF scenes.
// Has no GPU state and no VFS dependency — pure data.
class MaterialLibrary {
public:
    // Register all materials from a parsed glTF scene.
    // base_virtual_dir: VFS directory containing the glTF file, used to resolve relative
    //                   texture URIs. E.g. for "world://models/helmet.glb" pass "world://models".
    //                   Pass an empty string if all textures are embedded (GLB).
    void register_from_gltf(GltfSceneAsset const&, StringView base_virtual_dir);

    // Register alias_name as an alias for source_name. No-op if alias already exists
    // or source doesn't exist. Used so a glTF mesh path resolves its first material
    // without an explicit setMaterial() call.
    void register_alias(StringView alias_name, StringView source_name);

    // Look up a material by name. Returns nullptr if the name is not registered.
    MaterialAsset const* resolve(StringView name) const;

private:
    HashMap<String, MaterialAsset> m_cache;
};

}
