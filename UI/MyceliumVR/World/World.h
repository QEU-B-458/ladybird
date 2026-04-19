/*
 * MyceliumVR - a social VR platform built on the Ladybird browser engine
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <AK/HashMap.h>
#include <AK/String.h>
#include <AK/Vector.h>
#include <LibGfx/Color.h>
#include <entt/entt.hpp>
#include <entt/meta/meta.hpp>

namespace MyceliumVR {

using EntityId = entt::entity;

struct Transform {
    float position[3] { 0.0f, 0.0f, 0.0f };
    float rotation[4] { 0.0f, 0.0f, 0.0f, 1.0f };
    float scale[3] { 1.0f, 1.0f, 1.0f };
};

struct MeshRenderer {
    String mesh { "cube"_string };
    String material { "default"_string };
    String normal_map {};  // virtual path to normal map image; empty = no normal map
    bool dirty { true };
};

struct Panel {
    String url;
    float width { 1.0f };
    float height { 1.0f };
    bool dirty { true };
};

// Cull mode override for a single entity.
// Absence of this component means "inherit from the material" (default).
struct CullOverride {
    enum class Mode : u8 {
        Back,     // force back-face culling regardless of material doubleSided
        Front,    // force front-face culling (e.g. interior geometry, outline passes)
        Disabled, // force double-sided / no culling
    };
    Mode mode { Mode::Back };
};

// Tag components for EnTT
struct TransformDirty {};
struct Selected {};
struct Static {};
struct AlphaBlend {};
struct AlphaClip {};
struct AlphaHash {};

class World {
public:
    static void register_meta();

    World();
    World(World&&) = delete;
    World& operator=(World&&) = delete;
    World(World const&) = delete;
    World& operator=(World const&) = delete;

    EntityId spawn_entity();
    bool destroy_entity(EntityId);

    bool set_transform(EntityId, Transform const&);
    bool set_mesh(EntityId, String);
    bool set_material(EntityId, String);
    bool set_normal_map(EntityId, String);
    bool set_cull_override(EntityId, CullOverride);
    bool clear_cull_override(EntityId);
    bool create_panel(EntityId, String url, float width, float height);

    entt::registry& registry() { return m_registry; }
    entt::registry const& registry() const { return m_registry; }

    size_t alive_entity_count() const;
    size_t dirty_transform_count() const;
    // layout_dirty: mesh/entity structure changed → full vertex buffer rebuild required.
    bool layout_dirty() const { return m_layout_dirty; }
    // transform_dirty: at least one entity transform changed → instance buffer update required.
    bool transform_dirty() const { return m_transform_dirty; }
    void clear_dirty_flags();

private:
    void on_transform_changed(entt::registry&, entt::entity);
    void on_layout_changed(entt::registry&, entt::entity);

    entt::registry m_registry;
    bool m_layout_dirty { true };    // spawn/destroy/set_mesh/set_material/set_normal_map/create_panel
    bool m_transform_dirty { true }; // set_transform (also set whenever layout_dirty is set)
};

}
