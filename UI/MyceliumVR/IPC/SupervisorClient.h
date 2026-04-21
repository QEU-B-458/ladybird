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

#include <LibIPC/ConnectionToServer.h>
#include <LibIPC/Transport.h>

namespace MyceliumVR {

class WorldManagementSystem;

// Connection from the Worker to the Supervisor.
// The Worker "talks to" the Supervisor using this client.
class SupervisorClient final
    : public ::IPC::ConnectionToServer<WorldWorkerEndpoint, SupervisorEndpoint> {
    C_OBJECT(SupervisorClient);

public:
    virtual ~SupervisorClient() override = default;

    // -- WorldWorkerEndpoint::Stub --
    virtual Messages::WorldWorker::PingResponse ping(u64 time_ms) override;

    void set_world_manager(WorldManagementSystem* wms) { m_world_manager = wms; }

private:
    explicit SupervisorClient(NonnullOwnPtr<::IPC::Transport> transport)
        : ::IPC::ConnectionToServer<WorldWorkerEndpoint, SupervisorEndpoint>(*this, move(transport))
    {
    }

    WorldManagementSystem* m_world_manager { nullptr };
};

}
