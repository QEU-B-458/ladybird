/*
 * MyceliumVR - a social VR platform built on the Ladybird browser engine
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "WorldWorkerClient.h"
#include "../World/WorldManagementSystem.h"

namespace MyceliumVR {

void WorldWorkerClient::die()
{
    outln("Worker IPC connection died.");
}

Messages::Supervisor::HelloResponse WorldWorkerClient::hello(String role, u32 version, u32 pid, String token, String package)
{
    outln("Supervisor received Hello from Worker (role={}, version={}, pid={}, package={})", role, version, pid, package);
    (void)token;

    // Default success for now.
    Vector<String> capabilities;
    capabilities.append("ipc.supervisor.control"_string);
    capabilities.append("ipc.world.receive"_string);

    return { true, 1, 1, move(capabilities) };
}

void WorldWorkerClient::world_state_changed(u32 world_id, String state)
{
    outln("World {} state changed: {}", world_id, state);
}

void WorldWorkerClient::log_event(u32 world_id, String message)
{
    outln("[World {}] {}", world_id, message);
}

Messages::Supervisor::SpawnEntityResponse WorldWorkerClient::spawn_entity()
{
    if (!m_world_manager) return { 0 };
    return { static_cast<u32>(m_world_manager->active_world().spawn_entity()) };
}

Messages::Supervisor::DestroyEntityResponse WorldWorkerClient::destroy_entity(u32 entity_id)
{
    if (!m_world_manager) return { false };
    return { m_world_manager->active_world().destroy_entity(static_cast<EntityId>(entity_id)) };
}

Messages::Supervisor::SetMeshResponse WorldWorkerClient::set_mesh(u32 entity_id, String mesh)
{
    if (!m_world_manager) return { false };
    return { m_world_manager->active_world().set_mesh(static_cast<EntityId>(entity_id), move(mesh)) };
}

Messages::Supervisor::SetMaterialResponse WorldWorkerClient::set_material(u32 entity_id, String material)
{
    if (!m_world_manager) return { false };
    return { m_world_manager->active_world().set_material(static_cast<EntityId>(entity_id), move(material)) };
}

Messages::Supervisor::SetNormalMapResponse WorldWorkerClient::set_normal_map(u32 entity_id, String path)
{
    if (!m_world_manager) return { false };
    return { m_world_manager->active_world().set_normal_map(static_cast<EntityId>(entity_id), move(path)) };
}

Messages::Supervisor::CreatePanelResponse WorldWorkerClient::create_panel(u32 entity_id, String url, float width, float height)
{
    if (!m_world_manager) return { false };
    return { m_world_manager->active_world().create_panel(static_cast<EntityId>(entity_id), move(url), width, height) };
}

void WorldWorkerClient::script_log(String level, String source, String message)
{
    outln("[{}|{}] {}", level, source, message);
}

}
