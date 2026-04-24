/*
 * MyceliumVR - a social VR platform built on the Ladybird browser engine
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "ProcessLocalWorldRuntimeHost.h"

#include "../Rendering/Renderer.h"

namespace MyceliumVR {

ProcessLocalWorldRuntimeHost::ProcessLocalWorldRuntimeHost(Renderer& renderer)
    : m_renderer(renderer)
{
}

void ProcessLocalWorldRuntimeHost::set_camera_state(CameraState const& camera_state)
{
    m_renderer.set_camera_state(camera_state);
}

CameraState ProcessLocalWorldRuntimeHost::camera_state() const
{
    return m_renderer.camera_state();
}

void ProcessLocalWorldRuntimeHost::set_scene_light(SceneLightData const& scene_light)
{
    m_renderer.set_scene_light(scene_light);
}

SceneLightData ProcessLocalWorldRuntimeHost::scene_light() const
{
    return m_renderer.scene_light();
}

void ProcessLocalWorldRuntimeHost::set_runtime_state(u32)
{
}

u32 ProcessLocalWorldRuntimeHost::register_mesh(WorldId world_id, ByteString const& virtual_path)
{
    auto scoped_path = ByteString::formatted("{}:{}", world_id, virtual_path);
    if (auto it = m_meshes.find(scoped_path); it != m_meshes.end())
        return it->value;
    u32 handle = (world_id << 24) | (m_meshes.size() + 1);
    m_meshes.set(move(scoped_path), handle);
    m_renderer.register_mesh(handle, virtual_path);
    return handle;
}

u32 ProcessLocalWorldRuntimeHost::register_material(WorldId world_id, ByteString const& virtual_path)
{
    auto scoped_path = ByteString::formatted("{}:{}", world_id, virtual_path);
    if (auto it = m_materials.find(scoped_path); it != m_materials.end())
        return it->value;
    u32 handle = (world_id << 24) | (m_materials.size() + 1);
    m_materials.set(move(scoped_path), handle);
    m_renderer.register_material(handle, virtual_path);
    return handle;
}

u32 ProcessLocalWorldRuntimeHost::register_panel(WorldId world_id, u32 entity_id, ByteString const& url, float width, float height)
{
    u64 panel_key = (static_cast<u64>(world_id) << 32) | entity_id;
    if (auto it = m_panels.find(panel_key); it != m_panels.end())
        return it->value;
    u32 handle = (world_id << 24) | (m_panels.size() + 1);
    m_panels.set(panel_key, handle);
    m_renderer.register_panel(handle, entity_id, url, width, height);
    return handle;
}

}
