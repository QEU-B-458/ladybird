/*
 * MyceliumVR - a social VR platform built on the Ladybird browser engine
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <AK/HashMap.h>
#include <AK/String.h>
#include <AK/Vector.h>
#include <LibGfx/Color.h>

namespace MyceliumVR {

using EntityId = u32;

struct Transform {
    float position[3] { 0.0f, 0.0f, 0.0f };
    float rotation[4] { 0.0f, 0.0f, 0.0f, 1.0f };
    float scale[3] { 1.0f, 1.0f, 1.0f };
};

struct MeshRenderer {
    String mesh { "cube"_string };
    String material { "default"_string };
    bool dirty { true };
};

struct Panel {
    String url;
    float width { 1.0f };
    float height { 1.0f };
    bool dirty { true };
};

struct Entity {
    EntityId id { 0 };
    Transform transform;
    MeshRenderer mesh_renderer;
    Optional<Panel> panel;
    bool alive { true };
    bool transform_dirty { true };
};

class World {
public:
    World() = default;
    World(World&&) = default;
    World& operator=(World&&) = default;
    World(World const&) = delete;
    World& operator=(World const&) = delete;

    EntityId spawn_entity();
    bool destroy_entity(EntityId);

    Entity* entity(EntityId);
    Entity const* entity(EntityId) const;

    bool set_transform(EntityId, Transform const&);
    bool set_mesh(EntityId, String);
    bool set_material(EntityId, String);
    bool create_panel(EntityId, String url, float width, float height);

    Vector<Entity> const& entities() const { return m_entities; }
    size_t alive_entity_count() const;
    size_t dirty_transform_count() const;
    void clear_dirty_flags();

private:
    EntityId m_next_entity_id { 1 };
    Vector<Entity> m_entities;
    HashMap<EntityId, size_t> m_entity_indices;
};

}
