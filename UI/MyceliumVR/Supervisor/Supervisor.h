/*
 * MyceliumVR - a social VR platform built on the Ladybird browser engine
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include "../IPC/TcpLoopbackTransport.h"
#include "../IPC/WorldWorkerClient.h"

#include <AK/ByteString.h>
#include <AK/Error.h>
#include <AK/Optional.h>
#include <AK/Vector.h>
#include <LibCore/Process.h>

namespace MyceliumVR {

struct WorldWorkerBootstrapConfig {
    IPC::TcpLoopbackEndpoint endpoint;
    ByteString ipc_address;
    ByteString launch_token;
    ByteString world_package;
    ByteString world_path;
    Optional<u32> channel_target_world;
    ByteString channel_payload;
    Optional<u32> portal_target_world;
    bool send_camera_pose { false };
    bool test_capability_denial { false };
    bool test_watchdog { false };
    bool test_quota { false };
};

struct SupervisorSpawnedWorker {
    Core::Process process;
    RefPtr<WorldWorkerClient> ipc_client;
    u64 session_id { 0 };
    u32 world_id { 0 };
    ByteString ipc_address;
    ByteString launch_token;
    ByteString world_package;
    ByteString world_path;
    Optional<u32> channel_target_world;
    ByteString channel_payload;
    Optional<u32> portal_target_world;
    bool send_camera_pose { false };
    bool test_capability_denial { false };
    bool test_watchdog { false };
    bool test_quota { false };
};

struct SupervisorHandshakeResult {
    u64 session_id { 0 };
    u32 world_id { 0 };
    String world_package;
    String worker_role;
    Vector<String> capabilities;
};

struct WorldWorkerStateEvent {
    u32 world_id { 0 };
    String state;
};

struct WorkerLogEvent {
    u32 world_id { 0 };
    String message;
};

class WorldWorkerProcess {
public:
    static ErrorOr<int> run(WorldWorkerBootstrapConfig const&);
};

class SupervisorSelfTest {
public:
    static ErrorOr<void> run();
    static ErrorOr<void> run_channel_self_test();
    static ErrorOr<void> run_portal_self_test();
    static ErrorOr<void> run_hardening_self_test();

private:
    static ErrorOr<SupervisorSpawnedWorker> spawn_worker(IPC::TcpLoopbackListener const&, u32 world_id, ByteString world_package, ByteString world_path, Optional<u32> channel_target_world = {}, ByteString channel_payload = {}, Optional<u32> portal_target_world = {}, bool send_camera_pose = false, bool test_capability_denial = false, bool test_watchdog = false, bool test_quota = false, Optional<ByteString> ipc_address_override = {});
    static ErrorOr<NonnullOwnPtr<IPC::IIpcConnection>> accept_worker_connection(IPC::TcpLoopbackListener&);
    static ErrorOr<SupervisorHandshakeResult> accept_and_handshake(IPC::IIpcConnection&, SupervisorSpawnedWorker const&);
    static ErrorOr<WorldWorkerStateEvent> wait_for_world_state(IPC::IIpcConnection&, StringView expected_state);
    static ErrorOr<WorkerLogEvent> wait_for_log_event(IPC::IIpcConnection&, StringView expected_message);
};

}
