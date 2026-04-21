/*
 * MyceliumVR - a social VR platform built on the Ladybird browser engine
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include "World.h"
#include "WorldRuntimeHost.h"

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

namespace MyceliumVR {

class BridgeBackend;
class InputState;
class ScriptHost;
class ScriptRuntime;
class VirtualFileSystem;
class WasmRuntime;

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

class WorldRuntime {
public:
    struct BootInfo {
        ByteString source_path;
        String package_name;
        String world_mount_root;
        String state_mount_root;
        String entry_script_path;
    };

    explicit WorldRuntime(WorldManagementSystem&, WorldId, String name);
    ~WorldRuntime();

    WorldId id() const { return m_id; }
    String const& name() const { return m_name; }

    World& world();
    World const& world() const;
    void set_boot_info(BootInfo);
    Optional<BootInfo> const& boot_info() const { return m_boot_info; }

    bool is_initialized() const { return m_script_runtime; }
    WorldLifecyclePhase phase() const { return m_phase; }
    WorldRuntimeState state() const { return m_state; }
    void set_state(WorldRuntimeState state) { m_state = state; }

    ErrorOr<void> initialize(ScriptHost&, VirtualFileSystem*, InputState*, WorldRuntimeHost* = nullptr);
    ErrorOr<void> load_script(ByteString const&);
    ErrorOr<void> load_script_source(ByteBuffer, StringView filename);
    ErrorOr<void> load_control_script(ByteString const&);
    void set_log_callback(Function<void(StringView, StringView, StringView)>);
    void update(double delta_time);
    void shutdown();

    ScriptRuntime& script_runtime();
    ScriptRuntime const& script_runtime() const;
    WasmRuntime* networking_wasm() { return m_networking_wasm.ptr(); }
    void set_networking_wasm(OwnPtr<WasmRuntime> wasm) { m_networking_wasm = move(wasm); }
    BridgeBackend& bridge_backend();
    BridgeBackend const& bridge_backend() const;

    ControlBusServer& control_bus() { return *m_control_bus; }
    ControlBusServer const& control_bus() const { return *m_control_bus; }

    HashMap<u32, NonnullOwnPtr<EntityScriptContext>> const& entity_script_contexts() const { return m_entity_script_contexts; }
    void add_mount_prefix(String);
    Vector<String> const& mount_prefixes() const { return m_mount_prefixes; }
    void clear_mount_prefixes();

private:
    WorldId m_id { 0 };
    String m_name;
    WorldLifecyclePhase m_phase { WorldLifecyclePhase::Boot };
    WorldRuntimeState m_state { WorldRuntimeState::Stopped };
    WorldManagementSystem& m_world_manager;
    OwnPtr<World> m_world;
    OwnPtr<ScriptRuntime> m_script_runtime;
    OwnPtr<WasmRuntime> m_networking_wasm;
    OwnPtr<ControlBusServer> m_control_bus;
    HashMap<u32, NonnullOwnPtr<EntityScriptContext>> m_entity_script_contexts;
    Vector<String> m_mount_prefixes;
    Optional<BootInfo> m_boot_info;
};

}
