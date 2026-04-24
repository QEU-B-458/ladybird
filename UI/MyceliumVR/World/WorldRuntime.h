/*
 * MyceliumVR - a social VR platform built on the Ladybird browser engine
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include "World.h"
#include "WorldManifest.h"
#include "WorldRuntimeHost.h"

#include "../Networking/NetworkService.h"
#include "../Support/InputState.h"
#include "../Networking/ControlBusServer.h"
#include "../Scripting/WasmRuntime.h"

#include <AK/ByteBuffer.h>
#include <AK/ByteString.h>
#include <AK/Error.h>
#include <AK/Function.h>
#include <AK/HashMap.h>
#include <AK/NonnullOwnPtr.h>
#include <AK/Optional.h>
#include <AK/OwnPtr.h>
#include <AK/String.h>
#include <AK/Vector.h>
#include <condition_variable>
#include <atomic>
#include <mutex>
#include <thread>

namespace MyceliumVR {

class BridgeBackend;
class InputState;
class ScriptHost;
class ScriptRuntime;
class VirtualFileSystem;
class WasmRuntime;
class WorldManagementSystem;

using WorldId = u32;

enum class WorldLifecyclePhase {
    Boot,
    Start,
    Update,
    FixedUpdate,
    EventDispatch,
    Shutdown,
};

enum class WorldRuntimeState {
    Foreground,
    Background,
    Suspended,
    Stopped,
};

enum class WorldState {
    Running,
    Faulted,
    Unloading,
};

class EntityScriptContext {
public:
    explicit EntityScriptContext(EntityId entity_id, ScriptComponent component)
        : m_entity_id(entity_id)
        , m_component(move(component))
    {
    }

    EntityId entity_id() const { return m_entity_id; }
    ScriptComponent const& component() const { return m_component; }

private:
    EntityId m_entity_id { 0 };
    ScriptComponent m_component;
};

class WorldRuntime : public RefCounted<WorldRuntime> {
public:
    struct BootInfo {
        ByteString source_path;
        String package_name;
        String world_mount_root;
        String state_mount_root;
        String entry_script_path;
    };

    explicit WorldRuntime(WorldManagementSystem*, WorldId, String name);
    ~WorldRuntime();

    WorldId id() const { return m_id; }
    String const& name() const { return m_name; }

    World& world();
    World const& world() const;
    void set_boot_info(BootInfo);
    Optional<BootInfo> const& boot_info() const { return m_boot_info; }
    void set_bootstrap_info(BootstrapInfo);
    Optional<BootstrapInfo> const& bootstrap_info() const { return m_bootstrap_info; }
    void set_script_tick_budget_ms(u32 value) { m_script_tick_budget_ms = value; }
    u32 script_tick_budget_ms() const { return m_script_tick_budget_ms; }
    void set_background_tick_rate(u32 value) { m_background_tick_rate = value; }
    u32 background_tick_rate() const { return m_background_tick_rate; }

    bool is_initialized() const { return m_script_runtime; }
    WorldLifecyclePhase phase() const { return m_phase.load(); }
    WorldRuntimeState state() const { return m_state.load(); }
    void set_state(WorldRuntimeState state);
    WorldState world_state() const { return m_world_state.load(); }
    bool is_faulted() const { return m_world_state.load() == WorldState::Faulted; }
    String const& fault_reason() const { return m_fault_reason; }

    ErrorOr<void> initialize(ScriptHost&, VirtualFileSystem*, InputState*, WorldRuntimeHost* = nullptr, bool use_worker_thread = true);
    bool sync_scene_revision_for_static_publish();
    size_t required_static_scene_snapshot_bytes();
    bool build_static_scene_snapshot(Span<u8> slot);
    void build_render_snapshot(Span<u8> slot);
    ErrorOr<void> load_script(ByteString const&);
    ErrorOr<void> load_script_source(ByteBuffer, StringView filename);
    ErrorOr<void> load_control_script(ByteString const&);
    ErrorOr<String, String> run_script(StringView source, StringView filename = "eval"sv);
    void set_log_callback(Function<void(StringView, StringView, StringView)>);
    void set_fault_callback(Function<void(StringView)> callback) { m_fault_callback = move(callback); }
    void update(double delta_time, InputFrameState);
    void tick(double delta_time, InputFrameState);
    void shutdown();
    void enqueue_network_events(Vector<NetworkEvent>);
    Optional<NetworkEvent> dequeue_network_event();
    Optional<String> dequeue_connection_payload_as_utf8(u32 connection_id);
    void queue_update(double delta_time, InputFrameState);
    void mark_faulted(String reason);

    template<typename Callback>
    decltype(auto) with_world_lock(Callback&& callback)
    {
        std::lock_guard lock(m_world_mutex);
        return callback(*m_world);
    }

    template<typename Callback>
    bool try_with_world_lock(Callback&& callback)
    {
        std::unique_lock lock(m_world_mutex, std::try_to_lock);
        if (!lock.owns_lock())
            return false;
        callback(*m_world);
        return true;
    }

    ScriptRuntime& script_runtime();
    ScriptRuntime const& script_runtime() const;
    WasmRuntime* networking_wasm() { return m_networking_wasm.ptr(); }
    void set_networking_wasm(OwnPtr<WasmRuntime> wasm) { m_networking_wasm = move(wasm); }
    BridgeBackend& bridge_backend();
    BridgeBackend const& bridge_backend() const;

    WorldRuntimeHost* runtime_host() { return m_runtime_host; }

    ControlBusServer& control_bus() { return *m_control_bus; }
    ControlBusServer const& control_bus() const { return *m_control_bus; }

    HashMap<u32, NonnullOwnPtr<EntityScriptContext>> const& entity_script_contexts() const { return m_entity_script_contexts; }
    void add_mount_prefix(String);
    Vector<String> const& mount_prefixes() const { return m_mount_prefixes; }
    void clear_mount_prefixes();

private:
    WorldId m_id { 0 };
    String m_name;
    std::atomic<WorldLifecyclePhase> m_phase { WorldLifecyclePhase::Boot };
    std::atomic<WorldRuntimeState> m_state { WorldRuntimeState::Stopped };
    std::atomic<WorldState> m_world_state { WorldState::Running };
    String m_fault_reason;
    WorldManagementSystem* m_world_manager { nullptr };
    ScriptHost* m_script_host { nullptr };
    WorldRuntimeHost* m_runtime_host { nullptr };
    OwnPtr<World> m_world;
    OwnPtr<ScriptRuntime> m_script_runtime;
    OwnPtr<WasmRuntime> m_networking_wasm;
    OwnPtr<ControlBusServer> m_control_bus;
    HashMap<u32, NonnullOwnPtr<EntityScriptContext>> m_entity_script_contexts;
    Vector<NetworkEvent> m_pending_network_events;
    HashMap<u32, Vector<ByteBuffer>> m_connection_payloads;
    Vector<String> m_mount_prefixes;
    Optional<BootInfo> m_boot_info;
    Optional<BootstrapInfo> m_bootstrap_info;
    u32 m_script_tick_budget_ms { 0 };
    u32 m_background_tick_rate { 10 };
    std::mutex m_world_mutex;
    std::mutex m_worker_mutex;
    std::condition_variable m_worker_condition;
    std::thread m_worker_thread;
    bool m_worker_should_stop { false };
    bool m_update_pending { false };
    double m_pending_delta_time { 0.0 };
    InputFrameState m_pending_input_state;
    bool m_worker_started { false };
    bool m_logged_snapshot_camera_motion { false };
    CameraState m_last_snapshot_camera_state;
    u32 m_scene_revision { 1 };
    bool m_static_scene_publish_pending { true };
    Function<void(StringView)> m_fault_callback;
};

}
