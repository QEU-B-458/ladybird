/*
 * MyceliumVR - a social VR platform built on the Ladybird browser engine
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "World.h"

#include <entt/meta/factory.hpp>

#include <UI/MyceliumVR/Support/Profiling.h>

namespace MyceliumVR {

void World::register_meta()
{
    using namespace entt::literals;

    entt::meta_factory<Transform>{}
        .type("Transform"_hs)
        .data<&Transform::position>("position"_hs)
        .data<&Transform::rotation>("rotation"_hs)
        .data<&Transform::scale>("scale"_hs);

    entt::meta_factory<MeshRenderer>{}
        .type("MeshRenderer"_hs)
        .data<&MeshRenderer::mesh>("mesh"_hs)
        .data<&MeshRenderer::material>("material"_hs)
        .data<&MeshRenderer::normal_map>("normal_map"_hs);

    entt::meta_factory<Panel>{}
        .type("Panel"_hs)
        .data<&Panel::url>("url"_hs)
        .data<&Panel::width>("width"_hs)
        .data<&Panel::height>("height"_hs);

    entt::meta_factory<Name>{}.type("Name"_hs).data<&Name::value>("value"_hs);
    entt::meta_factory<Parent>{}.type("Parent"_hs).data<&Parent::id>("id"_hs);
    
    entt::meta_factory<ScriptComponent>{}.type("ScriptComponent"_hs)
        .data<&ScriptComponent::module_path>("module_path"_hs)
        .data<&ScriptComponent::enabled>("enabled"_hs)
        .data<&ScriptComponent::entrypoint>("entrypoint"_hs);

    entt::meta_factory<ScriptRuntimeHandle>{}.type("ScriptRuntimeHandle"_hs)
        .data<&ScriptRuntimeHandle::value>("value"_hs);

    entt::meta_factory<TransformDirty>{}.type("TransformDirty"_hs);
    entt::meta_factory<Selected>{}.type("Selected"_hs);
    entt::meta_factory<Static>{}.type("Static"_hs);
    entt::meta_factory<AlphaBlend>{}.type("AlphaBlend"_hs);
    entt::meta_factory<AlphaClip>{}.type("AlphaClip"_hs);
    entt::meta_factory<AlphaHash>{}.type("AlphaHash"_hs);
    entt::meta_factory<CullOverride>{}.type("CullOverride"_hs);
}

World::World()
{
    m_registry.on_update<Transform>().connect<&World::on_transform_changed>(this);

    m_registry.on_construct<MeshRenderer>().connect<&World::on_layout_changed>(this);
    m_registry.on_destroy<MeshRenderer>().connect<&World::on_layout_changed>(this);
    m_registry.on_update<MeshRenderer>().connect<&World::on_layout_changed>(this);

    m_registry.on_construct<Panel>().connect<&World::on_layout_changed>(this);
    m_registry.on_destroy<Panel>().connect<&World::on_layout_changed>(this);
    m_registry.on_update<Panel>().connect<&World::on_layout_changed>(this);
}

void World::on_transform_changed(entt::registry&, entt::entity entity)
{
    m_registry.emplace_or_replace<TransformDirty>(entity);
    m_transform_dirty = true;
}

void World::on_layout_changed(entt::registry&, entt::entity)
{
    m_layout_dirty = true;
    m_transform_dirty = true;
}

EntityId World::spawn_entity()
{
#if defined(TRACY_ENABLE)
    ZoneScoped;
#endif
    auto id = m_registry.create();
    m_registry.emplace<Transform>(id);
    m_registry.emplace<MeshRenderer>(id);
    return id;
}

bool World::destroy_entity(EntityId id)
{
#if defined(TRACY_ENABLE)
    ZoneScoped;
#endif
    if (!m_registry.valid(id))
        return false;

    m_registry.destroy(id);
    return true;
}

bool World::set_transform(EntityId id, Transform const& transform)
{
#if defined(TRACY_ENABLE)
    ZoneScoped;
#endif
    if (!m_registry.valid(id))
        return false;

    m_registry.replace<Transform>(id, transform);
    return true;
}

bool World::set_mesh(EntityId id, String mesh)
{
#if defined(TRACY_ENABLE)
    ZoneScoped;
#endif
    if (!m_registry.valid(id))
        return false;

    auto& renderer = m_registry.get_or_emplace<MeshRenderer>(id);
    renderer.mesh = move(mesh);
    renderer.dirty = true;
    m_registry.patch<MeshRenderer>(id);
    return true;
}

bool World::set_material(EntityId id, String material)
{
#if defined(TRACY_ENABLE)
    ZoneScoped;
#endif
    if (!m_registry.valid(id))
        return false;

    auto& renderer = m_registry.get_or_emplace<MeshRenderer>(id);
    renderer.material = move(material);
    renderer.dirty = true;
    m_registry.patch<MeshRenderer>(id);
    return true;
}

bool World::set_normal_map(EntityId id, String normal_map)
{
#if defined(TRACY_ENABLE)
    ZoneScoped;
#endif
    if (!m_registry.valid(id))
        return false;

    auto& renderer = m_registry.get_or_emplace<MeshRenderer>(id);
    renderer.normal_map = move(normal_map);
    renderer.dirty = true;
    m_registry.patch<MeshRenderer>(id);
    return true;
}

bool World::set_cull_override(EntityId id, CullOverride override)
{
#if defined(TRACY_ENABLE)
    ZoneScoped;
#endif
    if (!m_registry.valid(id))
        return false;

    m_registry.emplace_or_replace<CullOverride>(id, override);
    m_layout_dirty = true;
    m_transform_dirty = true;
    return true;
}

bool World::clear_cull_override(EntityId id)
{
#if defined(TRACY_ENABLE)
    ZoneScoped;
#endif
    if (!m_registry.valid(id))
        return false;

    m_registry.remove<CullOverride>(id);
    m_layout_dirty = true;
    m_transform_dirty = true;
    return true;
}

bool World::create_panel(EntityId id, String url, float width, float height)
{
#if defined(TRACY_ENABLE)
    ZoneScoped;
#endif
    if (!m_registry.valid(id))
        return false;

    m_registry.emplace_or_replace<Panel>(id, Panel {
        .url = move(url),
        .width = width,
        .height = height,
        .dirty = true,
    });
    return true;
}

size_t World::alive_entity_count() const
{
#if defined(TRACY_ENABLE)
    ZoneScoped;
#endif
    return m_registry.storage<entt::entity>()->size();
}

size_t World::dirty_transform_count() const
{
#if defined(TRACY_ENABLE)
    ZoneScoped;
#endif
    return m_registry.view<TransformDirty>().size();
}

void World::clear_dirty_flags()
{
#if defined(TRACY_ENABLE)
    ZoneScoped;
#endif
    m_registry.clear<TransformDirty>();
    m_registry.view<MeshRenderer>().each([](auto& renderer) {
        renderer.dirty = false;
    });
    m_registry.view<Panel>().each([](auto& panel) {
        panel.dirty = false;
    });
    m_layout_dirty = false;
    m_transform_dirty = false;
}

}
