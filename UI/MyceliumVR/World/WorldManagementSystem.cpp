/*
 * MyceliumVR - a social VR platform built on the Ladybird browser engine
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "WorldManagementSystem.h"

#include "../Networking/ControlBusServer.h"
#include "../Rendering/Renderer.h"
#include "../Rendering/Web/UIOverlayBroker.h"
#include "../Scripting/BridgeBackend.h"
#include "../Support/InputState.h"
#include "../Support/VirtualFileSystem.h"

#include <AK/LexicalPath.h>
#include <AK/JsonObject.h>
#include <LibCore/Directory.h>

namespace MyceliumVR {

static BootstrapInfo build_bootstrap_info(WorldId world_id, WorldManifest const& manifest)
{
    return BootstrapInfo {
        .world_id = world_id,
        .package_name = manifest.package_name,
        .world_name = manifest.name,
        .mode = manifest.networking.bootstrap.mode.is_empty() ? "none"_string : manifest.networking.bootstrap.mode,
        .bootstrap_urls = manifest.networking.bootstrap.urls,
        .transports = manifest.networking.transports,
        .protocol_version = manifest.networking.protocol_version,
        .storage_allowed = manifest.permissions.storage,
        .network_allowed = manifest.permissions.network,
        .wasm_allowed = manifest.permissions.wasm,
    };
}

static ErrorOr<void> validate_networking_manifest(WorldManifest const& manifest)
{
    if (manifest.networking.entry_wasm.is_empty())
        return {};
    if (!manifest.permissions.network)
        return Error::from_string_literal("World manifest declares networking.entry_wasm without permissions.network");
    if (!manifest.permissions.wasm)
        return Error::from_string_literal("World manifest declares networking.entry_wasm without permissions.wasm");
    if (manifest.networking.transports.is_empty())
        return Error::from_string_literal("World manifest declares networking.entry_wasm without any transports");
    return {};
}

WorldManagementSystem::WorldManagementSystem(VirtualFileSystem* virtual_file_system, InputState* input_state)
    : m_virtual_file_system(virtual_file_system)
    , m_input_state(input_state)
{
}

void WorldManagementSystem::set_environment(VirtualFileSystem* virtual_file_system, InputState* input_state)
{
    m_virtual_file_system = virtual_file_system;
    m_input_state = input_state;
}

void WorldManagementSystem::set_renderer(Renderer* renderer)
{
    m_renderer = renderer;
    if (renderer)
        m_runtime_host = make<ProcessLocalWorldRuntimeHost>(*renderer);
    else
        m_runtime_host = nullptr;
}

void WorldManagementSystem::set_runtime_log_callback(Function<void(StringView, StringView, StringView)> callback)
{
    m_runtime_log_callback = move(callback);
    for (auto& entry : m_world_runtimes) {
        auto& runtime = *entry.value;
        if (!runtime.is_initialized())
            continue;
        bind_runtime_callbacks(runtime);
    }
}

ErrorOr<void> WorldManagementSystem::initialize()
{
    auto& runtime = ensure_runtime(m_session.active_world_id(), m_session.active_world_name());
    runtime.set_state(WorldRuntimeState::Foreground);
    return initialize_runtime(runtime);
}

ErrorOr<void> WorldManagementSystem::boot(BootRequest const& request)
{
    if (!request.world_path.is_empty())
        return switch_to_world(request);

    auto& runtime = runtime_by_id(m_session.active_world_id());
    if (!runtime.is_initialized())
        TRY(initialize_runtime(runtime));
    return boot_runtime(runtime, request, {});
}

ErrorOr<void> WorldManagementSystem::switch_to_world(BootRequest const& request)
{
    VERIFY(!request.world_path.is_empty());
    cancel_loading_world();

    auto* existing_runtime = find_runtime_by_source_path(request.world_path);
    auto& previous_active_runtime = runtime_by_id(m_session.active_world_id());
    if (existing_runtime) {
        if (existing_runtime->id() == previous_active_runtime.id())
            return {};

        m_session.set_loading_world(existing_runtime->id(), existing_runtime->name());
        previous_active_runtime.set_state(WorldRuntimeState::Background);
        existing_runtime->set_state(WorldRuntimeState::Foreground);
        VERIFY(m_session.activate_loading_world());
        bind_runtime_callbacks(*existing_runtime);
        return {};
    }

    auto prepared = TRY(prepare_world_load(request));
    auto loading_world_id = m_session.begin_loading_world(prepared.manifest.name);
    auto& loading_runtime = ensure_runtime(loading_world_id, prepared.manifest.name);
    loading_runtime.set_boot_info({
        .source_path = request.world_path,
        .package_name = prepared.manifest.package_name,
        .world_mount_root = prepared.world_mount_root,
        .state_mount_root = prepared.state_mount_root,
        .entry_script_path = prepared.entry_script_path,
    });
    loading_runtime.set_bootstrap_info(build_bootstrap_info(loading_world_id, prepared.manifest));
    loading_runtime.set_script_tick_budget_ms(prepared.manifest.script_tick_budget_ms);

    auto rollback_loading_world = [&]() {
        unmount_runtime(loading_runtime);
        loading_runtime.shutdown();
        m_world_runtimes.remove(loading_runtime.id());
        m_session.cancel_loading_world();
    };

    auto mount_result = mount_world_runtime(loading_runtime, request, prepared);
    if (mount_result.is_error()) {
        rollback_loading_world();
        return mount_result.release_error();
    }

    auto boot_result = boot_runtime(loading_runtime, request, prepared);
    if (boot_result.is_error()) {
        rollback_loading_world();
        return boot_result.release_error();
    }

    previous_active_runtime.set_state(WorldRuntimeState::Background);
    loading_runtime.set_state(WorldRuntimeState::Foreground);
    VERIFY(m_session.activate_loading_world());
    bind_runtime_callbacks(loading_runtime);

    outln("Mounted world '{}' from '{}' as {}.", prepared.manifest.name, request.world_path, prepared.world_mount_root);
    outln("Mounted writable world state as {}.", prepared.state_mount_root);
    return {};
}

void WorldManagementSystem::cancel_loading_world()
{
    if (!m_session.has_loading_world())
        return;
    auto world_id = m_session.loading_world_id();
    auto it = m_world_runtimes.find(world_id);
    if (it == m_world_runtimes.end()) {
        m_session.cancel_loading_world();
        return;
    }
    auto& loading_runtime = *it->value;
    unmount_runtime(loading_runtime);
    loading_runtime.shutdown();
    if (world_id != m_session.active_world_id())
        m_world_runtimes.remove(world_id);
    m_session.cancel_loading_world();
}

void WorldManagementSystem::update(double delta_time)
{
    for (auto& entry : m_world_runtimes) {
        auto& runtime = *entry.value;
        if (!runtime.is_initialized())
            continue;
        if (runtime.state() == WorldRuntimeState::Suspended || runtime.state() == WorldRuntimeState::Stopped)
            continue;
        if (runtime.is_faulted())
            continue;

        runtime.enqueue_network_events(m_network_service.poll_events(runtime.id()));

        auto input_frame = m_input_state ? m_input_state->snapshot() : InputFrameState {};
        runtime.update(delta_time, move(input_frame));
    }
}

WorldRuntime* WorldManagementSystem::find_runtime(WorldId world_id)
{
    auto it = m_world_runtimes.find(world_id);
    if (it == m_world_runtimes.end())
        return nullptr;
    return it->value.ptr();
}

WorldRuntime const* WorldManagementSystem::find_runtime(WorldId world_id) const
{
    auto it = m_world_runtimes.find(world_id);
    if (it == m_world_runtimes.end())
        return nullptr;
    return it->value.ptr();
}

WorldRuntime* WorldManagementSystem::foreground_runtime()
{
    return find_runtime(m_session.active_world_id());
}

WorldRuntime const* WorldManagementSystem::foreground_runtime() const
{
    return find_runtime(m_session.active_world_id());
}

WorldRuntime& WorldManagementSystem::runtime_by_id(WorldId world_id)
{
    auto it = m_world_runtimes.find(world_id);
    VERIFY(it != m_world_runtimes.end());
    return *it->value;
}

WorldRuntime const& WorldManagementSystem::runtime_by_id(WorldId world_id) const
{
    auto it = m_world_runtimes.find(world_id);
    VERIFY(it != m_world_runtimes.end());
    return *it->value;
}

WorldRuntime& WorldManagementSystem::ensure_runtime(WorldId world_id, String const& world_name)
{
    auto it = m_world_runtimes.find(world_id);
    if (it != m_world_runtimes.end())
        return *it->value;

    auto runtime = make<WorldRuntime>(*this, world_id, MUST(String::from_utf8(world_name.bytes())));
    auto* runtime_ptr = runtime.ptr();
    m_world_runtimes.set(world_id, move(runtime));
    return *runtime_ptr;
}

WorldRuntime* WorldManagementSystem::find_runtime_by_source_path(StringView source_path)
{
    for (auto& entry : m_world_runtimes) {
        auto& runtime = *entry.value;
        if (!runtime.boot_info().has_value())
            continue;
        if (runtime.boot_info()->source_path == source_path)
            return &runtime;
    }
    return nullptr;
}

ErrorOr<WorldManagementSystem::PreparedWorld> WorldManagementSystem::prepare_world_load(BootRequest const& request) const
{
    VERIFY(m_virtual_file_system);

    auto const boot_mount = "world://_boot/"sv;
    TRY(m_virtual_file_system->mount_directory(boot_mount, request.world_path));

    auto manifest_result = load_world_manifest(*m_virtual_file_system, "world://_boot/world.json"sv);
    auto unmount_result = m_virtual_file_system->unmount(boot_mount);
    if (manifest_result.is_error())
        return manifest_result.release_error();
    TRY(unmount_result);
    auto manifest = manifest_result.release_value();
    TRY(validate_networking_manifest(manifest));

    auto world_mount_root = TRY(package_mount_root(manifest.package_name));
    auto state_mount_root = MUST(String::formatted("state://{}/", manifest.package_name));
    auto entry_script_path = TRY(resolve_world_relative_path(world_mount_root, manifest.entry_script));

    return PreparedWorld {
        .manifest = move(manifest),
        .world_mount_root = move(world_mount_root),
        .state_mount_root = move(state_mount_root),
        .entry_script_path = move(entry_script_path),
    };
}

ErrorOr<void> WorldManagementSystem::mount_world_runtime(WorldRuntime& runtime, BootRequest const& request, PreparedWorld const& prepared)
{
    VERIFY(m_virtual_file_system);

    runtime.clear_mount_prefixes();

    TRY(m_virtual_file_system->mount_directory(prepared.world_mount_root, request.world_path));
    runtime.add_mount_prefix(prepared.world_mount_root);

    auto state_path = LexicalPath::join(request.world_path, "state"sv);
    TRY(Core::Directory::create(state_path, Core::Directory::CreateDirectories::Yes));
    TRY(m_virtual_file_system->mount_directory(prepared.state_mount_root, state_path.string(), MountPermissions::ReadWrite));
    runtime.add_mount_prefix(prepared.state_mount_root);

    return {};
}

ErrorOr<void> WorldManagementSystem::boot_runtime(WorldRuntime& runtime, BootRequest const& request, Optional<PreparedWorld> const& prepared)
{
    if (!runtime.is_initialized())
        TRY(initialize_runtime(runtime));

    if (request.has_script_path_override) {
        TRY(runtime.load_script(request.script_path));
    } else if (prepared.has_value()) {
        auto source = TRY(m_virtual_file_system->read_file(prepared->entry_script_path));
        outln("Booting world script {}.", prepared->entry_script_path);
        TRY(runtime.load_script_source(move(source), prepared->entry_script_path));
    } else {
        TRY(runtime.load_script(request.script_path));
    }

    if (prepared.has_value() && !prepared->manifest.networking.entry_wasm.is_empty()) {
        auto wasm_path = TRY(resolve_world_relative_path(prepared->world_mount_root, prepared->manifest.networking.entry_wasm));
        auto source = TRY(m_virtual_file_system->read_file(wasm_path));
        outln("Booting world networking WASM {}.", wasm_path);
        
        // TODO: This should use a proper wasm runtime loader.
        // For now, we'll just instantiate the member if it doesn't exist.
        if (!runtime.networking_wasm()) {
            runtime.set_networking_wasm(make<WasmRuntime>(runtime.script_runtime()));
            TRY(runtime.networking_wasm()->initialize());
        }
        TRY(runtime.networking_wasm()->load_module(move(source)));
    }

    TRY(runtime.load_control_script(request.control_script_path));
    return {};
}

void WorldManagementSystem::unmount_runtime(WorldRuntime& runtime)
{
    if (!m_virtual_file_system)
        return;

    for (auto const& mount_prefix : runtime.mount_prefixes())
        (void)m_virtual_file_system->unmount(mount_prefix);
    runtime.clear_mount_prefixes();
}

ErrorOr<void> WorldManagementSystem::initialize_runtime(WorldRuntime& runtime)
{
    runtime.shutdown();
    if (!m_script_host.is_initialized())
        TRY(m_script_host.initialize());
    TRY(runtime.initialize(m_script_host, m_virtual_file_system, m_input_state, m_runtime_host.ptr()));
    bind_runtime_callbacks(runtime);
    return {};
}

void WorldManagementSystem::bind_runtime_callbacks(WorldRuntime& runtime)
{
    if (m_overlay_broker)
        m_overlay_broker->connect_to_world(runtime.control_bus().port(), runtime.control_bus().capability_token());

    runtime.set_log_callback([this, &runtime](StringView level, StringView source, StringView message) {
        if (m_runtime_log_callback)
            m_runtime_log_callback(level, source, message);

        JsonObject data;
        data.set("level"sv, level);
        data.set("source"sv, source);
        data.set("message"sv, message);
        runtime.control_bus().send_event("log.entry"sv, data);
    });

    runtime.bridge_backend().set_selection_changed_callback([&runtime](EntityId entity) {
        JsonObject data;
        data.set("entity_id"sv, static_cast<u32>(entity));
        runtime.control_bus().send_event("entity.selected"sv, data);
    });
}

}
