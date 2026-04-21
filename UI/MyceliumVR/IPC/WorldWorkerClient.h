/*
 * MyceliumVR - a social VR platform built on the Ladybird browser engine
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <AK/ByteString.h>
#include <AK/Error.h>
#include <AK/NonnullOwnPtr.h>
#include <AK/String.h>

#include <SupervisorEndpoint.h>
#include <WorldWorkerEndpoint.h>

#include <LibIPC/ConnectionFromClient.h>
#include <LibIPC/Transport.h>

namespace MyceliumVR {

class WorldManagementSystem;

// Connection from the Supervisor to a Worker.
// The Supervisor "talks to" the Worker using this client.
class WorldWorkerClient final
    : public ::IPC::ConnectionFromClient<WorldWorkerEndpoint, SupervisorEndpoint> {
    C_OBJECT(WorldWorkerClient);

public:
    virtual ~WorldWorkerClient() override = default;

    virtual void die() override;

    // -- SupervisorEndpoint::Stub --
    virtual Messages::Supervisor::HelloResponse hello(String role, u32 protocol_version, u32 process_id, String launch_token, String world_package) override;
    virtual void world_state_changed(u32 world_id, String state) override;
    virtual void log_event(u32 world_id, String message) override;

    virtual Messages::Supervisor::SpawnEntityResponse spawn_entity() override;
    virtual Messages::Supervisor::DestroyEntityResponse destroy_entity(u32 entity_id) override;
    virtual Messages::Supervisor::SetMeshResponse set_mesh(u32 entity_id, String mesh) override;
    virtual Messages::Supervisor::SetMaterialResponse set_material(u32 entity_id, String material) override;
    virtual Messages::Supervisor::SetNormalMapResponse set_normal_map(u32 entity_id, String path) override;
    virtual Messages::Supervisor::CreatePanelResponse create_panel(u32 entity_id, String url, float width, float height) override;
    virtual void script_log(String level, String source, String message) override;

    void set_world_manager(WorldManagementSystem* wms) { m_world_manager = wms; }

private:
    explicit WorldWorkerClient(NonnullOwnPtr<::IPC::Transport> transport, int client_id)
        : ::IPC::ConnectionFromClient<WorldWorkerEndpoint, SupervisorEndpoint>(*this, move(transport), client_id)
    {
    }

    WorldManagementSystem* m_world_manager { nullptr };
};

}
