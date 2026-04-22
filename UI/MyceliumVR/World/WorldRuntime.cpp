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

#include <chrono>

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

void WorldRuntime::set_bootstrap_info(BootstrapInfo bootstrap_info)
{
    m_bootstrap_info = move(bootstrap_info);
}

ErrorOr<void> WorldRuntime::initialize(ScriptHost& script_host, VirtualFileSystem* virtual_file_system, InputState* input_state, WorldRuntimeHost* runtime_host)
{
    m_script_host = &script_host;
    m_world_state = WorldState::Running;
    m_fault_reason = {};
    m_script_runtime = make<ScriptRuntime>(
        script_host,
        *m_world,
        m_world_manager.network_service(),
        virtual_file_system,
        input_state,
        runtime_host,
        this);
    TRY(m_script_runtime->initialize());
    TRY(m_control_bus->start(m_id));
    {
        std::lock_guard lock(m_worker_mutex);
        m_worker_should_stop = false;
        m_update_pending = false;
        m_pending_delta_time = 0.0;
    }
    m_worker_thread = std::thread([this] {
        for (;;) {
            double delta_time = 0.0;
            InputFrameState input_state;
            {
                std::unique_lock lock(m_worker_mutex);
                m_worker_condition.wait(lock, [this] { return m_worker_should_stop || m_update_pending; });
                if (m_worker_should_stop)
                    break;
                delta_time = m_pending_delta_time;
                input_state = m_pending_input_state;
                m_update_pending = false;
            }

            if (!m_script_runtime || is_faulted())
                continue;

            auto tick_started_at = std::chrono::steady_clock::now();

            if (m_networking_wasm) {
                std::lock_guard world_lock(m_world_mutex);
                m_networking_wasm->update(delta_time);
                if (m_networking_wasm->is_faulted()) {
                    mark_faulted(MUST(String::formatted("WASM runtime faulted: {}", m_networking_wasm->fault_reason())));
                    continue;
                }
            }

            bool script_ok = true;
            {
                std::lock_guard vm_lock(m_script_host->vm_mutex());
                std::lock_guard world_lock(m_world_mutex);
                m_phase = WorldLifecyclePhase::Update;
                m_script_runtime->set_input_frame_state(move(input_state));
                script_ok = m_script_runtime->update(delta_time);
                m_phase = WorldLifecyclePhase::EventDispatch;
            }

            if (!script_ok) {
                mark_faulted(MUST(String::formatted("Script exception: {}", m_script_runtime->last_exception_message())));
                continue;
            }

            auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - tick_started_at).count();
            if (m_script_tick_budget_ms > 0 && elapsed_ms > m_script_tick_budget_ms)
                mark_faulted(MUST(String::formatted("Script tick exceeded budget ({}ms > {}ms)", elapsed_ms, m_script_tick_budget_ms)));
        }
    });
    m_worker_started = true;
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

void WorldRuntime::update(double delta_time, InputFrameState input_state)
{
    if (!m_script_runtime || is_faulted())
        return;
    queue_update(delta_time, move(input_state));
}

void WorldRuntime::shutdown()
{
    m_phase = WorldLifecyclePhase::Shutdown;
    m_world_state = WorldState::Unloading;
    m_state = WorldRuntimeState::Stopped;
    {
        std::lock_guard lock(m_worker_mutex);
        m_worker_should_stop = true;
        m_update_pending = false;
    }
    m_worker_condition.notify_all();
    if (m_worker_thread.joinable())
        m_worker_thread.join();
    m_worker_started = false;
    m_world_manager.network_service().close_world_connections(m_id);
    m_control_bus->stop();
    m_entity_script_contexts.clear();
    m_pending_network_events.clear();
    m_connection_payloads.clear();
    m_networking_wasm = nullptr;
    m_script_runtime = nullptr;
    m_script_host = nullptr;
    m_boot_info.clear();
    m_bootstrap_info.clear();
}

void WorldRuntime::enqueue_network_events(Vector<NetworkEvent> events)
{
    for (auto& event : events) {
        if (event.type == NetworkEvent::Type::Data)
            m_connection_payloads.ensure(event.connection_id).append(ByteBuffer::copy(event.payload.bytes()).release_value_but_fixme_should_propagate_errors());
        m_pending_network_events.append(move(event));
    }
}

Optional<NetworkEvent> WorldRuntime::dequeue_network_event()
{
    if (m_pending_network_events.is_empty())
        return {};
    return m_pending_network_events.take_first();
}

Optional<String> WorldRuntime::dequeue_connection_payload_as_utf8(u32 connection_id)
{
    auto it = m_connection_payloads.find(connection_id);
    if (it == m_connection_payloads.end() || it->value.is_empty())
        return {};

    auto payload = it->value.take_first();
    if (it->value.is_empty())
        m_connection_payloads.remove(it);

    auto text = String::from_utf8(StringView { payload.bytes() });
    if (text.is_error())
        return {};
    return text.release_value();
}

void WorldRuntime::queue_update(double delta_time, InputFrameState input_state)
{
    std::lock_guard lock(m_worker_mutex);
    if (m_worker_should_stop)
        return;
    m_pending_delta_time = delta_time;
    m_pending_input_state = move(input_state);
    m_update_pending = true;
    m_worker_condition.notify_one();
}

void WorldRuntime::mark_faulted(String reason)
{
    if (m_world_state == WorldState::Faulted)
        return;

    m_world_state = WorldState::Faulted;
    m_fault_reason = move(reason);
    m_phase = WorldLifecyclePhase::Shutdown;
    warnln("World '{}' faulted: {}", m_name, m_fault_reason);
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
