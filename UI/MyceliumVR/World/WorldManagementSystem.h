/*
 * MyceliumVR - a social VR platform built on the Ladybird browser engine
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include "Session.h"
#include "WorldManifest.h"
#include "ProcessLocalWorldRuntimeHost.h"
#include "WorldRuntime.h"

#include "../Networking/NetworkService.h"
#include "../Scripting/ScriptHost.h"

#include <AK/ByteString.h>
#include <AK/HashMap.h>
#include <AK/OwnPtr.h>

namespace MyceliumVR {

class BridgeBackend;
class InputState;
class Renderer;
class VirtualFileSystem;
class UIOverlayBroker;

class WorldManagementSystem {
public:
    struct BootRequest {
        ByteString world_path;
        ByteString script_path;
        ByteString control_script_path;
        bool has_script_path_override { false };
    };

    explicit WorldManagementSystem(VirtualFileSystem* = nullptr, InputState* = nullptr);

    void set_environment(VirtualFileSystem*, InputState*);
    void set_renderer(Renderer*);
    void set_overlay_broker(UIOverlayBroker* broker) { m_overlay_broker = broker; }
    void set_runtime_log_callback(Function<void(StringView, StringView, StringView)>);
    ErrorOr<void> initialize();
    ErrorOr<void> boot(BootRequest const&);
    ErrorOr<void> switch_to_world(BootRequest const&);
    void cancel_loading_world();
    void update(double delta_time);

    Session& session() { return m_session; }
    Session const& session() const { return m_session; }

    NetworkService& network_service() { return m_network_service; }
    NetworkService const& network_service() const { return m_network_service; }
    VirtualFileSystem* virtual_file_system() { return m_virtual_file_system; }
    VirtualFileSystem const* virtual_file_system() const { return m_virtual_file_system; }

    WorldRuntime* find_runtime(WorldId);
    WorldRuntime const* find_runtime(WorldId) const;
    WorldRuntime* foreground_runtime();
    WorldRuntime const* foreground_runtime() const;

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
    WorldRuntime* find_runtime_by_source_path(StringView);
    ErrorOr<PreparedWorld> prepare_world_load(BootRequest const&) const;
    ErrorOr<void> mount_world_runtime(WorldRuntime&, BootRequest const&, PreparedWorld const&);
    ErrorOr<void> boot_runtime(WorldRuntime&, BootRequest const&, Optional<PreparedWorld> const&);
    void unmount_runtime(WorldRuntime&);
    ErrorOr<void> initialize_runtime(WorldRuntime&);

    NetworkService m_network_service;
    ScriptHost m_script_host;
    Session m_session;
    HashMap<WorldId, OwnPtr<WorldRuntime>> m_world_runtimes;
    VirtualFileSystem* m_virtual_file_system { nullptr };
    InputState* m_input_state { nullptr };
    Renderer* m_renderer { nullptr };
    OwnPtr<WorldRuntimeHost> m_runtime_host;
    UIOverlayBroker* m_overlay_broker { nullptr };
    Function<void(StringView, StringView, StringView)> m_runtime_log_callback;
};

}
