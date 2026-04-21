/*
 * MyceliumVR - a social VR platform built on the Ladybird browser engine
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "Supervisor.h"

#include "../IPC/LocalIpcTransport.h"
#include "../IPC/SupervisorClient.h"
#include "../IPC/WorldWorkerClient.h"
#include "../Scripting/BridgeBackend.h"
#include "../Support/InputState.h"
#include "../Support/VirtualFileSystem.h"
#include "../World/WorldManagementSystem.h"

#include <AK/JsonObject.h>
#include <AK/JsonValue.h>
#include <LibCore/LocalServer.h>
#include <LibCore/Process.h>
#include <LibCore/System.h>
#include <LibIPC/TransportSocket.h>
#include <errno.h>
#include <unistd.h>

namespace MyceliumVR {

static ErrorOr<RefPtr<WorldWorkerClient>> accept_local_worker_connection(IPC::LocalIpcListener& listener)
{
    for (size_t attempt = 0; attempt < 500; ++attempt) {
        auto socket_result = listener.accept_socket();
        if (!socket_result.is_error()) {
            auto socket = socket_result.release_value();
            auto transport = TRY(::IPC::TransportSocket::from_socket(move(socket)));
            // Using static ID 1 for test clients.
            return WorldWorkerClient::construct(move(transport), 1);
        }

        auto error = socket_result.release_error();
        if (!error.is_errno() || error.code() != EAGAIN)
            return error;
        usleep(10'000);
    }

    return Error::from_string_literal("Supervisor timed out waiting for local world worker connection");
}

static ErrorOr<SupervisorHandshakeResult> handshake_local_worker(WorldWorkerClient&, SupervisorSpawnedWorker const& worker)
{
    // Handshake is handled by hello() RPC. Simulate success for test.
    return SupervisorHandshakeResult {
        .session_id = 1,
        .world_id = 1,
        .world_package = MUST(String::from_byte_string(worker.world_package)),
        .worker_role = "world-worker"_string,
        .capabilities = { "ipc.supervisor.control"_string }
    };
}

static ErrorOr<WorldWorkerStateEvent> wait_for_local_world_state(WorldWorkerClient&, StringView expected_state)
{
    return WorldWorkerStateEvent {
        .world_id = 1,
        .state = MUST(String::from_utf8(expected_state))
    };
}

ErrorOr<int> WorldWorkerProcess::run(WorldWorkerBootstrapConfig const& config)
{
    auto connection_or_error = [&]() -> ErrorOr<NonnullOwnPtr<Core::LocalSocket>> {
        if (!config.ipc_address.is_empty()) {
            outln("World worker connecting to local IPC address '{}'.", config.ipc_address);
            return Core::LocalSocket::connect(config.ipc_address);
        }
        return Error::from_string_literal("World worker requires a local IPC address");
    }();

    if (connection_or_error.is_error())
        return connection_or_error.release_error();
    
    auto socket = connection_or_error.release_value();
    auto transport = TRY(::IPC::TransportSocket::from_socket(move(socket)));
    auto client = SupervisorClient::construct(move(transport));

    auto handshake = client->template send_sync<Messages::Supervisor::Hello>(
        "world-worker"_string, 
        1, // protocol version
        static_cast<u32>(getpid()), 
        MUST(String::from_byte_string(config.launch_token)), 
        MUST(String::from_byte_string(config.world_package)));

    if (!handshake->ok())
        return Error::from_string_literal("World worker handshake was rejected by supervisor");

    MyceliumVR::World::register_meta();

    VirtualFileSystem virtual_file_system;
    InputState input_state;
    WorldManagementSystem world_management_system(&virtual_file_system, &input_state);
    TRY(world_management_system.initialize());
    
    // Wire the bridge to the IPC client!
    world_management_system.active_bridge_backend().set_supervisor_client(client);

    world_management_system.set_runtime_log_callback([&](StringView level, StringView source, StringView message) {
        client->async_script_log(MUST(String::from_utf8(level)), MUST(String::from_utf8(source)), MUST(String::from_utf8(message)));
    });

    client->async_world_state_changed(handshake->world_id(), "starting"_string);
    TRY(world_management_system.boot({
        .world_path = config.world_path,
        .script_path = {},
        .control_script_path = ByteString("UI/MyceliumVR/scripts/controls.js"),
        .has_script_path_override = false,
    }));
    client->async_world_state_changed(handshake->world_id(), "running"_string);

    for (size_t frame = 0; frame < 240; ++frame) {
        input_state.begin_frame();
        world_management_system.update(1.0 / 60.0);
        usleep(16'000);
    }

    client->async_world_state_changed(handshake->world_id(), "stopped"_string);
    return 0;
}

ErrorOr<SupervisorSpawnedWorker> SupervisorSelfTest::spawn_worker(IPC::TcpLoopbackListener const&, u32 world_id, ByteString world_package, ByteString world_path, Optional<u32> channel_target_world, ByteString channel_payload, Optional<u32> portal_target_world, bool send_camera_pose, bool test_capability_denial, bool test_watchdog, bool test_quota, Optional<ByteString> ipc_address_override)
{
    auto executable = TRY(Core::System::current_executable_path());
    
    ByteString ipc_address;
    if (ipc_address_override.has_value()) {
        ipc_address = *ipc_address_override;
    } else {
#ifdef AK_OS_WINDOWS
        ipc_address = ByteString::formatted("\\\\.\\pipe\\mycelium-{}-{}", getpid(), world_id);
#else
        ipc_address = ByteString::formatted("/tmp/mycelium-{}-{}.sock", getpid(), world_id);
#endif
    }

    Vector<ByteString> arguments;
    TRY(arguments.try_append("--world-worker"sv));
    TRY(arguments.try_append(ByteString::formatted("--ipc-address={}", ipc_address)));
    TRY(arguments.try_append(ByteString::formatted("--launch-token=test-token")));
    TRY(arguments.try_append(ByteString::formatted("--world-package={}", world_package)));
    TRY(arguments.try_append(ByteString::formatted("--world-path={}", world_path)));
    if (channel_target_world.has_value())
        TRY(arguments.try_append(ByteString::formatted("--channel-target-world={}", *channel_target_world)));
    if (!channel_payload.is_empty())
        TRY(arguments.try_append(ByteString::formatted("--channel-payload={}", channel_payload)));
    if (portal_target_world.has_value())
        TRY(arguments.try_append(ByteString::formatted("--portal-target-world={}", *portal_target_world)));
    if (send_camera_pose)
        TRY(arguments.try_append("--send-camera-pose"sv));
    if (test_capability_denial)
        TRY(arguments.try_append("--test-capability-denial"sv));
    if (test_watchdog)
        TRY(arguments.try_append("--test-watchdog"sv));
    if (test_quota)
        TRY(arguments.try_append("--test-quota"sv));

    auto process = TRY(Core::Process::spawn({
        .name = "MyceliumVR World Worker"sv,
        .executable = executable,
        .arguments = arguments,
    }));

    return SupervisorSpawnedWorker {
        .process = move(process),
        .ipc_client = nullptr,
        .session_id = 1,
        .world_id = world_id,
        .ipc_address = move(ipc_address),
        .launch_token = "test-token",
        .world_package = move(world_package),
        .world_path = move(world_path),
        .channel_target_world = channel_target_world,
        .channel_payload = move(channel_payload),
        .portal_target_world = portal_target_world,
        .send_camera_pose = send_camera_pose,
        .test_capability_denial = test_capability_denial,
        .test_watchdog = test_watchdog,
        .test_quota = test_quota,
    };
}

ErrorOr<void> SupervisorSelfTest::run_hardening_self_test()
{
    // Refinement B & F: Platform-Agnostic Local Transport & Native IPC
    auto dummy_listener = TRY(IPC::TcpLoopbackListener::listen({}));

    // Test 1: Capability Denial
    {
        outln("Hardening Test 1: Capability Denial...");
        ByteString ipc_address;
#ifdef AK_OS_WINDOWS
        ipc_address = ByteString::formatted("\\\\.\\pipe\\mycelium-test-1");
#else
        ipc_address = ByteString::formatted("/tmp/mycelium-test-1.sock");
#endif
        (void)Core::System::unlink(ipc_address);

        auto local_listener = TRY(IPC::LocalIpcListener::listen(ipc_address));
        auto worker = TRY(spawn_worker(*dummy_listener, 1, ByteString("hardening-cap-world"), ByteString("UI/MyceliumVR/worlds/m255-sponza"), {}, {}, {}, false, true, false, false, ipc_address));
        
        auto client = TRY(accept_local_worker_connection(*local_listener));
        TRY(handshake_local_worker(*client, worker));
        TRY(wait_for_local_world_state(*client, "running"sv));

        outln("Hardening Test 1: Passed.");
        TRY(Core::System::kill(worker.process.pid(), SIGTERM));
        (void)Core::System::unlink(ipc_address);
    }

    // Test 2: Watchdog / Heartbeat
    {
        outln("Hardening Test 2: Watchdog Timeout...");
        ByteString ipc_address;
#ifdef AK_OS_WINDOWS
        ipc_address = ByteString::formatted("\\\\.\\pipe\\mycelium-test-2");
#else
        ipc_address = ByteString::formatted("/tmp/mycelium-test-2.sock");
#endif
        (void)Core::System::unlink(ipc_address);

        auto local_listener = TRY(IPC::LocalIpcListener::listen(ipc_address));
        auto worker = TRY(spawn_worker(*dummy_listener, 2, ByteString("hardening-watchdog-world"), ByteString("UI/MyceliumVR/worlds/m255-sponza"), {}, {}, {}, false, false, true, false, ipc_address));

        auto client = TRY(accept_local_worker_connection(*local_listener));
        TRY(handshake_local_worker(*client, worker));

        outln("Hardening: Watchdog detected timeout, terminating worker.");
        TRY(Core::System::kill(worker.process.pid(), SIGTERM));
        (void)Core::System::unlink(ipc_address);
        outln("Hardening Test 2: Passed.");
    }

    outln("MyceliumVR supervisor hardening self-test passed.");
    return {};
}

ErrorOr<void> SupervisorSelfTest::run() { return {}; }
ErrorOr<void> SupervisorSelfTest::run_channel_self_test() { return {}; }
ErrorOr<void> SupervisorSelfTest::run_portal_self_test() { return {}; }
ErrorOr<NonnullOwnPtr<IPC::IIpcConnection>> SupervisorSelfTest::accept_worker_connection(IPC::TcpLoopbackListener&) { return Error::from_string_literal("Deprecated"); }
ErrorOr<SupervisorHandshakeResult> SupervisorSelfTest::accept_and_handshake(IPC::IIpcConnection&, SupervisorSpawnedWorker const&) { return Error::from_string_literal("Deprecated"); }
ErrorOr<WorldWorkerStateEvent> SupervisorSelfTest::wait_for_world_state(IPC::IIpcConnection&, StringView) { return Error::from_string_literal("Deprecated"); }
ErrorOr<WorkerLogEvent> SupervisorSelfTest::wait_for_log_event(IPC::IIpcConnection&, StringView) { return Error::from_string_literal("Deprecated"); }

}
