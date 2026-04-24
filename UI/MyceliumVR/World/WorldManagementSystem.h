/*
 * MyceliumVR - a social VR platform built on the Ladybird browser engine
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include "Session.h"
#include "WorldManifest.h"
#include "ProcessLocalWorldRuntimeHost.h"
#include "WorldRuntime.h"
#include "WorldHostConnection.h"

#include "../Networking/NetworkService.h"
#include "../ProcessTransport/WorldProcessTransportHost.h"
#include "../Scripting/ScriptHost.h"

#include <AK/ByteString.h>
#include <AK/HashMap.h>
#include <AK/OwnPtr.h>
#include <LibCore/Timer.h>

namespace MyceliumVR {

class BridgeBackend;
class InputState;
class Renderer;
class VirtualFileSystem;
class OverlayManager;

class WorldManagementSystem {
public:
    struct BootRequest {
        ByteString world_path;
        ByteString script_path;
        ByteString control_script_path;
        bool has_script_path_override { false };
        bool load_in_background { false };
    };

    struct DiscoverableWorld {
        ByteString path;
        String name;
        bool is_running { false };
        bool is_active { false };
    };
    Vector<DiscoverableWorld> list_available_worlds() const;

    explicit WorldManagementSystem(VirtualFileSystem* = nullptr, InputState* = nullptr);
    ~WorldManagementSystem();

    void set_environment(VirtualFileSystem*, InputState*);
    void set_renderer(Renderer*);
    void set_runtime_log_callback(Function<void(StringView, StringView, StringView)>);
    ErrorOr<void> initialize();

    OverlayManager& overlay_manager() { return *m_overlay_manager; }
    OverlayManager const& overlay_manager() const { return *m_overlay_manager; }

    ErrorOr<void> boot(BootRequest const&);
    ErrorOr<void> switch_to_world(BootRequest const&);
    void cancel_loading_world();
    void update(double delta_time);
    void reload_world(WorldId);
    void notify_control_bus_ready(WorldId, u16 port, ByteString token);
    void notify_world_faulted(WorldId, ByteString reason);
    void rebind_foreground_runtime_callbacks();
    void on_world_process_ready(WorldId);

    bool is_in_process_mode() const { return m_in_process_mode; }
    void set_in_process_mode(bool mode) { m_in_process_mode = mode; }

    Session& session() { return m_session; }
    Session const& session() const { return m_session; }

    NetworkService& network_service() { return m_network_service; }
    NetworkService const& network_service() const { return m_network_service; }
    VirtualFileSystem* virtual_file_system() { return m_virtual_file_system; }
    VirtualFileSystem const* virtual_file_system() const { return m_virtual_file_system; }
    Renderer* renderer() { return m_renderer; }

    WorldRuntime* find_runtime(WorldId);
    WorldRuntime const* find_runtime(WorldId) const;
    WorldRuntime* foreground_runtime();
    WorldRuntime const* foreground_runtime() const;
    WorldHostConnection* foreground_world_process_connection();
    WorldHostConnection const* foreground_world_process_connection() const;
    ProcessTransport::WorldProcessTransportHost* foreground_world_process_transport();
    ProcessTransport::WorldProcessTransportHost const* foreground_world_process_transport() const;
    ProcessTransport::WorldProcessTransportHost* world_process_transport(WorldId);
    ProcessTransport::WorldProcessTransportHost const* world_process_transport(WorldId) const;

    template<typename Callback>
    bool try_with_foreground_world_lock(Callback&& callback)
    {
        if (auto* runtime = foreground_runtime())
            return runtime->try_with_world_lock(callback);
        return false;
    }

private:
    struct PreparedWorld {
        WorldManifest manifest;
        String world_mount_root;
        String state_mount_root;
        String entry_script_path;
    };

    void bind_runtime_callbacks(WorldRuntime&);
    WorldRuntime& runtime_by_id(WorldId);
    WorldRuntime const& runtime_by_id(WorldId) const;
    WorldRuntime& ensure_runtime(WorldId, String const&);
    WorldRuntime* find_runtime_by_source_path(StringView) const;
    ErrorOr<PreparedWorld> prepare_world_load(BootRequest const&) const;
    ErrorOr<void> mount_world_runtime(WorldRuntime&, BootRequest const&, PreparedWorld const&);
    ErrorOr<void> boot_runtime(WorldRuntime&, BootRequest const&, Optional<PreparedWorld> const&);
    void unmount_runtime(WorldRuntime&);
    ErrorOr<void> initialize_runtime(WorldRuntime&);

    void on_world_process_exit(WorldId, i32 exit_code);
    NetworkService m_network_service;
    ScriptHost m_script_host;
    Session m_session;
    HashMap<WorldId, NonnullRefPtr<WorldRuntime>> m_world_runtimes;
    HashMap<WorldId, RefPtr<WorldHostConnection>> m_world_process_connections;
    HashMap<WorldId, OwnPtr<ProcessTransport::WorldProcessTransportHost>> m_world_process_transports;
    HashMap<WorldId, pid_t> m_world_process_pids;
    HashMap<WorldId, u64> m_transport_input_sequences;
    HashMap<WorldId, u64> m_transport_host_frame_ids;
    HashMap<WorldId, u64> m_transport_dropped_input_counts;
    RefPtr<Core::Timer> m_process_monitor_timer;
    VirtualFileSystem* m_virtual_file_system { nullptr };
    InputState* m_input_state { nullptr };
    Renderer* m_renderer { nullptr };
    OwnPtr<WorldRuntimeHost> m_runtime_host;
    OwnPtr<OverlayManager> m_overlay_manager;
    Function<void(StringView, StringView, StringView)> m_runtime_log_callback;
    bool m_in_process_mode { true };
    bool m_requested_transport_bootstrap_self_test { false };
    bool m_logged_transport_input_shared_path { false };
    bool m_logged_transport_input_overwrite { false };
};

}
