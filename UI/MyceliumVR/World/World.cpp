/*
 * MyceliumVR - a social VR platform built on the Ladybird browser engine
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "World.h"

namespace MyceliumVR {

EntityId World::spawn_entity()
{
    Entity entity;
    entity.id = m_next_entity_id++;

    auto id = entity.id;
    m_entity_indices.set(id, m_entities.size());
    m_entities.append(move(entity));
    m_geometry_dirty = true;
    return id;
}

bool World::destroy_entity(EntityId id)
{
    auto index = m_entity_indices.get(id);
    if (!index.has_value())
        return false;

    m_entities[*index].alive = false;
    m_entity_indices.remove(id);
    m_geometry_dirty = true;
    return true;
}

Entity* World::entity(EntityId id)
{
    auto index = m_entity_indices.get(id);
    if (!index.has_value())
        return nullptr;
    return &m_entities[*index];
}

Entity const* World::entity(EntityId id) const
{
    auto index = m_entity_indices.get(id);
    if (!index.has_value())
        return nullptr;
    return &m_entities[*index];
}

bool World::set_transform(EntityId id, Transform const& transform)
{
    auto* entity = this->entity(id);
    if (!entity)
        return false;

    entity->transform = transform;
    entity->transform_dirty = true;
    m_geometry_dirty = true;
    return true;
}

bool World::set_mesh(EntityId id, String mesh)
{
    auto* entity = this->entity(id);
    if (!entity)
        return false;

    entity->mesh_renderer.mesh = move(mesh);
    entity->mesh_renderer.dirty = true;
    m_geometry_dirty = true;
    return true;
}

bool World::set_material(EntityId id, String material)
{
    auto* entity = this->entity(id);
    if (!entity)
        return false;

    entity->mesh_renderer.material = move(material);
    entity->mesh_renderer.dirty = true;
    m_geometry_dirty = true;
    return true;
}

bool World::set_normal_map(EntityId id, String normal_map)
{
    auto* entity = this->entity(id);
    if (!entity)
        return false;
    entity->mesh_renderer.normal_map = move(normal_map);
    entity->mesh_renderer.dirty = true;
    m_geometry_dirty = true;
    return true;
}

bool World::create_panel(EntityId id, String url, float width, float height)
{
    auto* entity = this->entity(id);
    if (!entity)
        return false;

    entity->panel = Panel {
        .url = move(url),
        .width = width,
        .height = height,
        .dirty = true,
    };
    m_geometry_dirty = true;
    return true;
}

size_t World::alive_entity_count() const
{
    size_t count = 0;
    for (auto const& entity : m_entities) {
        if (entity.alive)
            ++count;
    }
    return count;
}

size_t World::dirty_transform_count() const
{
    size_t count = 0;
    for (auto const& entity : m_entities) {
        if (entity.alive && entity.transform_dirty)
            ++count;
    }
    return count;
}

void World::clear_dirty_flags()
{
    for (auto& entity : m_entities) {
        entity.transform_dirty = false;
        entity.mesh_renderer.dirty = false;
        if (entity.panel.has_value())
            entity.panel->dirty = false;
    }
    m_geometry_dirty = false;
}

}
