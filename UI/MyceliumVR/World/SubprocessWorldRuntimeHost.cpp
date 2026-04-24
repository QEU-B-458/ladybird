/*
 * MyceliumVR - a social VR platform built on the Ladybird browser engine
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "SubprocessWorldRuntimeHost.h"
#include "../Networking/ControlBusServer.h"
#include <AK/JsonObject.h>

namespace MyceliumVR {

SubprocessWorldRuntimeHost::SubprocessWorldRuntimeHost() = default;
SubprocessWorldRuntimeHost::~SubprocessWorldRuntimeHost() = default;

void SubprocessWorldRuntimeHost::set_camera_state(CameraState const& camera_state)
{
    m_camera_state = camera_state;
}

CameraState SubprocessWorldRuntimeHost::camera_state() const
{
    return m_camera_state;
}

void SubprocessWorldRuntimeHost::set_scene_light(SceneLightData const& scene_light)
{
    m_scene_light = scene_light;
}

SceneLightData SubprocessWorldRuntimeHost::scene_light() const
{
    return m_scene_light;
}

void SubprocessWorldRuntimeHost::set_runtime_state(u32)
{
    // Subprocess host doesn't need to push this back down yet;
    // WorldRuntime in subprocess sets it locally.
}

u32 SubprocessWorldRuntimeHost::register_mesh(WorldId world_id, ByteString const& virtual_path)
{
    if (auto it = m_meshes.find(virtual_path); it != m_meshes.end())
        return it->value;

    // Use a unique range per world: (world_id << 24) | local_id
    u32 handle = (world_id << 24) | (m_meshes.size() + 1);
    m_meshes.set(virtual_path, handle);

    if (m_control_bus) {
        JsonObject data;
        data.set("handle"sv, handle);
        data.set("virtual_path"sv, String::from_byte_string(virtual_path).release_value_but_fixme_should_propagate_errors());
        m_control_bus->send_event("mesh.registered"sv, data);
    }

    return handle;
}

u32 SubprocessWorldRuntimeHost::register_material(WorldId world_id, ByteString const& virtual_path)
{
    if (auto it = m_materials.find(virtual_path); it != m_materials.end())
        return it->value;

    u32 handle = (world_id << 24) | (m_materials.size() + 1);
    m_materials.set(virtual_path, handle);

    if (m_control_bus) {
        JsonObject data;
        data.set("handle"sv, handle);
        data.set("virtual_path"sv, String::from_byte_string(virtual_path).release_value_but_fixme_should_propagate_errors());
        m_control_bus->send_event("material.registered"sv, data);
    }

    return handle;
}

u32 SubprocessWorldRuntimeHost::register_panel(WorldId world_id, u32 entity_id, ByteString const& url, float width, float height)
{
    if (auto it = m_panels.find(entity_id); it != m_panels.end())
        return it->value.handle;

    u32 handle = (world_id << 24) | (m_panels.size() + 1);
    m_panels.set(entity_id, { handle, entity_id, url, width, height });

    if (m_control_bus) {
        JsonObject data;
        data.set("handle"sv, handle);
        data.set("entity_id"sv, entity_id);
        data.set("url"sv, String::from_byte_string(url).release_value_but_fixme_should_propagate_errors());
        data.set("width"sv, width);
        data.set("height"sv, height);
        m_control_bus->send_event("panel.registered"sv, data);
    }

    return handle;
}

SubprocessWorldRuntimeHost::RegistrationSnapshot SubprocessWorldRuntimeHost::get_all_registrations() const
{
    RegistrationSnapshot snapshot;
    snapshot.meshes = m_meshes;
    snapshot.materials = m_materials;
    for (auto const& it : m_panels) {
        snapshot.panels.append({
            .handle = it.value.handle,
            .entity_id = it.value.entity_id,
            .url = it.value.url,
            .width = it.value.width,
            .height = it.value.height,
        });
    }
    return snapshot;
}

}
