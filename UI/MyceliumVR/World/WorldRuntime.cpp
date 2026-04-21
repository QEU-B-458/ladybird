/*
 * MyceliumVR - a social VR platform built on the Ladybird browser engine
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "WorldRuntime.h"
#include "WorldManagementSystem.h"

#include "../Scripting/BridgeBackend.h"
#include "../Scripting/ScriptHost.h"
#include "../Scripting/ScriptRuntime.h"
#include "../Scripting/WasmRuntime.h"
#include "../Support/InputState.h"
#include "../Support/VirtualFileSystem.h"

namespace MyceliumVR {

WorldRuntime::WorldRuntime(WorldManagementSystem& manager, WorldId id, String name)
    : m_id(id)
    , m_name(move(name))
    , m_world_manager(manager)
    , m_world(make<World>())
    , m_control_bus(make<ControlBusServer>(manager))
{
}

WorldRuntime::~WorldRuntime() = default;

World& WorldRuntime::world()
{
    return *m_world;
}

World const& WorldRuntime::world() const
{
    return *m_world;
}

void WorldRuntime::set_boot_info(BootInfo boot_info)
{
    m_boot_info = move(boot_info);
}

ErrorOr<void> WorldRuntime::initialize(ScriptHost& script_host, VirtualFileSystem* virtual_file_system, InputState* input_state, WorldRuntimeHost* runtime_host)
{
    m_script_runtime = make<ScriptRuntime>(
        script_host,
        *m_world,
        m_world_manager.network_service(),
        virtual_file_system,
        input_state,
        runtime_host);
    TRY(m_script_runtime->initialize());
    TRY(m_control_bus->start(m_id));
    m_phase = WorldLifecyclePhase::Start;
    m_state = WorldRuntimeState::Foreground;
    return {};
}

ErrorOr<void> WorldRuntime::load_script(ByteString const& path)
{
    VERIFY(m_script_runtime);
    return m_script_runtime->load_script(path);
}

ErrorOr<void> WorldRuntime::load_script_source(ByteBuffer source, StringView filename)
{
    VERIFY(m_script_runtime);
    return m_script_runtime->load_script_source(move(source), filename);
}

ErrorOr<void> WorldRuntime::load_control_script(ByteString const& path)
{
    VERIFY(m_script_runtime);
    return m_script_runtime->load_control_script(path);
}

void WorldRuntime::set_log_callback(Function<void(StringView, StringView, StringView)> callback)
{
    VERIFY(m_script_runtime);
    m_script_runtime->set_log_callback(move(callback));
}

void WorldRuntime::update(double delta_time)
{
    if (!m_script_runtime)
        return;
    m_phase = WorldLifecyclePhase::Update;
    if (m_networking_wasm)
        m_networking_wasm->update(delta_time);
    m_script_runtime->update(delta_time);
    m_phase = WorldLifecyclePhase::EventDispatch;
}

void WorldRuntime::shutdown()
{
    m_phase = WorldLifecyclePhase::Shutdown;
    m_state = WorldRuntimeState::Stopped;
    m_control_bus->stop();
    m_entity_script_contexts.clear();
    m_script_runtime = nullptr;
    m_boot_info.clear();
}

void WorldRuntime::add_mount_prefix(String prefix)
{
    m_mount_prefixes.append(move(prefix));
}

void WorldRuntime::clear_mount_prefixes()
{
    m_mount_prefixes.clear();
}

ScriptRuntime& WorldRuntime::script_runtime()
{
    VERIFY(m_script_runtime);
    return *m_script_runtime;
}

ScriptRuntime const& WorldRuntime::script_runtime() const
{
    VERIFY(m_script_runtime);
    return *m_script_runtime;
}

BridgeBackend& WorldRuntime::bridge_backend()
{
    return script_runtime().bridge_backend();
}

BridgeBackend const& WorldRuntime::bridge_backend() const
{
    return script_runtime().bridge_backend();
}

}
