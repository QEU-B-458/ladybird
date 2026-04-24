/*
 * MyceliumVR - a social VR platform built on the Ladybird browser engine
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include "WorldHostEndpoint.h"
#include "WorldClientEndpoint.h"
#include "WorldRuntimeHost.h"
#include "../Networking/ControlBusClient.h"
#include "../ProcessTransport/TransportTypes.h"

#include <AK/ByteBuffer.h>
#include <AK/HashMap.h>
#include <AK/Optional.h>
#include <LibIPC/ConnectionFromClient.h>
#include <LibIPC/Transport.h>

namespace MyceliumVR {

class WorldManagementSystem;

class WorldHostConnection final
    : public IPC::ConnectionFromClient<WorldClientEndpoint, WorldHostEndpoint> {
    C_OBJECT(WorldHostConnection);

public:
    virtual ~WorldHostConnection() override;

    virtual void die() override;

    bool has_snapshot() const;
    ReadonlyBytes latest_snapshot() const;
    bool has_static_scene() const;
    ReadonlyBytes latest_static_scene() const;
    bool is_world_ready() const { return m_world_ready; }
    void configure_control_bus(u16 port, ByteString token);
    void pump_control_bus();
    void pump_transport_control();
    void request_shutdown();

private:
    WorldHostConnection(NonnullOwnPtr<IPC::Transport> transport, int client_id, WorldManagementSystem& world_manager, u32 world_id);
    void handle_control_bus_message(JsonObject const&);
    void remap_static_scene_region_from_fd(int fd, size_t region_bytes);
    void sync_transport_frame_mailbox() const;
    void sync_transport_static_scene_region() const;

    WorldManagementSystem& m_world_manager;
    u32 m_world_id { 0 };
    HashMap<ByteString, u32> m_meshes;
    HashMap<ByteString, u32> m_materials;
    HashMap<u32, u32> m_panels; // entity_id -> panel_handle
    mutable u32 m_last_static_scene_revision { 0 };
    mutable size_t m_last_static_scene_bytes { 0 };
    mutable Optional<ProcessTransport::StaticScenePublishInfo> m_last_transport_static_scene_info;
    mutable u32 m_last_ready_frame_id { 0 };
    mutable u32 m_last_ready_scene_revision { 0 };
    mutable size_t m_last_ready_slot_bytes { 0 };
    mutable Optional<ProcessTransport::FrameReadView> m_last_transport_frame_view;
    OwnPtr<ControlBusClient> m_control_bus_client;
    bool m_control_bus_initialized { false };
    bool m_world_ready { false };
    mutable bool m_logged_snapshot_camera_motion { false };
    mutable CameraState m_last_snapshot_camera_state;
};

}
