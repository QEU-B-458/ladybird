/*
 * MyceliumVR - a social VR platform built on the Ladybird browser engine
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "WorldManagementSystem.h"
#include "SubprocessWorldRuntimeHost.h"
#include "WorldHostConnection.h"

#include "../ProcessTransport/Linux/LinuxTransportFactory.h"
#include <LibCore/AnonymousBuffer.h>
#include <LibCore/Process.h>
#include <LibCore/System.h>
#include <LibIPC/ConnectionToServer.h>
#include "../Networking/ControlBusServer.h"
#include "../Rendering/Renderer.h"
#include "../Rendering/Web/OverlayManager.h"
#include "../Scripting/BridgeBackend.h"
#include "../Support/InputState.h"
#include "../Support/VirtualFileSystem.h"

#include <LibFileSystem/FileSystem.h>
#include <AK/LexicalPath.h>
#include <AK/JsonObject.h>
#include <AK/Time.h>
#include <LibCore/Directory.h>
#include <stdlib.h>
#include <sys/wait.h>

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

static bool transport_bootstrap_self_test_requested_by_environment()
{
    auto* value = getenv("MYCELIUM_TRANSPORT_BOOTSTRAP_SELF_TEST");
    return value && value[0] != '\0' && value[0] != '0';
}

static bool transport_input_path_self_test_requested_by_environment()
{
    auto* value = getenv("MYCELIUM_TRANSPORT_INPUT_PATH_SELF_TEST");
    return value && value[0] != '\0' && value[0] != '0';
}

static Optional<size_t> transport_static_scene_bytes_override_from_environment()
{
    auto* value = getenv("MYCELIUM_TRANSPORT_STATIC_SCENE_BYTES");
    if (!value || value[0] == '\0')
        return {};

    char* endptr = nullptr;
    auto parsed_value = strtoull(value, &endptr, 10);
    if (endptr == value || (endptr && *endptr != '\0') || parsed_value == 0)
        return {};

    return static_cast<size_t>(parsed_value);
}

WorldManagementSystem::WorldManagementSystem(VirtualFileSystem* virtual_file_system, InputState* input_state)
    : m_virtual_file_system(virtual_file_system)
    , m_input_state(input_state)
    , m_overlay_manager(make<OverlayManager>())
{
    m_process_monitor_timer = Core::Timer::create_repeating(100, [this] {
        Vector<WorldId> dead_worlds;
        for (auto const& it : m_world_process_pids) {
            pid_t pid = it.value;
            int status = 0;
            auto result = waitpid(pid, &status, WNOHANG);
            if (result > 0) {
                dead_worlds.append(it.key);
            }
        }
        for (auto world_id : dead_worlds) {
            on_world_process_exit(world_id, 0); // TODO: status
        }
    });
    m_process_monitor_timer->start();
}

WorldManagementSystem::~WorldManagementSystem()
{
    for (auto& entry : m_world_process_connections)
        entry.value->request_shutdown();
    m_world_process_connections.clear();
    m_world_process_transports.clear();
    m_transport_input_sequences.clear();
    m_transport_host_frame_ids.clear();
    m_transport_dropped_input_counts.clear();
    for (auto& entry : m_world_runtimes)
        entry.value->shutdown();
    m_world_runtimes.clear();
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
Vector<WorldManagementSystem::DiscoverableWorld> WorldManagementSystem::list_available_worlds() const
{
    Vector<DiscoverableWorld> worlds;
    auto worlds_dir_relative = "UI/MyceliumVR/worlds"sv;
    auto worlds_dir_or_error = FileSystem::real_path(worlds_dir_relative);

    if (worlds_dir_or_error.is_error()) {
        warnln("WorldManagementSystem: Could not find worlds directory at {}: {}", worlds_dir_relative, worlds_dir_or_error.error());
        return worlds;
    }

    auto worlds_dir = worlds_dir_or_error.value();

    auto result = Core::Directory::for_each_entry(worlds_dir, Core::DirIterator::SkipDots, [&](auto& entry, auto&) -> ErrorOr<IterationDecision> {
        auto world_path = LexicalPath::join(worlds_dir, entry.name.view()).string();
        auto manifest_path = LexicalPath::join(world_path, "world.json"sv).string();

        if (FileSystem::exists(manifest_path)) {
            // Try to load manifest to get the name.
            VirtualFileSystem temp_vfs;
            (void)temp_vfs.mount_directory("/"sv, world_path);
            auto manifest_or_error = load_world_manifest(temp_vfs, "world.json"sv);

            DiscoverableWorld dw;
            dw.path = world_path;
            if (!manifest_or_error.is_error())
                dw.name = manifest_or_error.value().name;
            else
                dw.name = MUST(String::from_utf8(entry.name.view()));

            if (auto* runtime = find_runtime_by_source_path(world_path); runtime) {
                if (m_in_process_mode) {
                    dw.is_running = runtime->is_initialized() && runtime->state() != WorldRuntimeState::Stopped;
                } else {
                    auto world_id = runtime->id();
                    dw.is_running = m_world_process_connections.contains(world_id)
                        || m_world_process_pids.contains(world_id);
                }
                dw.is_active = runtime->id() == m_session.active_world_id();
            }

            worlds.append(move(dw));
        }
        return IterationDecision::Continue;
    });

    if (result.is_error()) {
        warnln("WorldManagementSystem: Error iterating worlds directory: {}", result.error());
    }

    return worlds;
}

ErrorOr<void> WorldManagementSystem::initialize()
{
    auto& runtime = ensure_runtime(m_session.active_world_id(), m_session.active_world_name());
    runtime.set_state(WorldRuntimeState::Foreground);
    if (!m_in_process_mode)
        return {};
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

        if (request.load_in_background) {
            existing_runtime->set_state(WorldRuntimeState::Background);
            return {};
        }

        m_session.set_loading_world(existing_runtime->id(), existing_runtime->name());
        
        if (auto* renderer = this->renderer())
            renderer->unload_world_resources(previous_active_runtime.id());

        previous_active_runtime.set_state(WorldRuntimeState::Background);
        existing_runtime->set_state(WorldRuntimeState::Foreground);
        VERIFY(m_session.activate_loading_world());
        bind_runtime_callbacks(*existing_runtime);

        if (m_overlay_manager) {
            auto const& bus = existing_runtime->control_bus();
            if (bus.is_running())
                m_overlay_manager->connect_to_world(bus.port(), bus.capability_token());
        }

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
    loading_runtime.set_background_tick_rate(prepared.manifest.background_tick_rate);

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

    if (request.load_in_background) {
        loading_runtime.set_state(WorldRuntimeState::Background);
        m_session.cancel_loading_world();
        bind_runtime_callbacks(loading_runtime);
    } else {
        if (auto* renderer = this->renderer())
            renderer->unload_world_resources(previous_active_runtime.id());

        previous_active_runtime.set_state(WorldRuntimeState::Background);
        loading_runtime.set_state(WorldRuntimeState::Foreground);
        VERIFY(m_session.activate_loading_world());
        bind_runtime_callbacks(loading_runtime);

        if (m_overlay_manager) {
            auto const& bus = loading_runtime.control_bus();
            if (bus.is_running())
                m_overlay_manager->connect_to_world(bus.port(), bus.capability_token());
        }
    }

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
    for (auto& entry : m_world_process_connections)
        entry.value->pump_transport_control();

    for (auto& entry : m_world_process_connections)
        entry.value->pump_control_bus();

    auto input_frame = m_input_state ? m_input_state->snapshot() : InputFrameState {};
    auto input_path_self_test_enabled = transport_input_path_self_test_requested_by_environment();

    if (!m_in_process_mode) {
        if (auto* connection = foreground_world_process_connection()) {
            auto world_id = m_session.active_world_id();
            if (connection->is_world_ready()) {
                if (auto* transport = foreground_world_process_transport(); transport && transport->is_configured()) {
                    u64 sequence = 1;
                    if (auto it = m_transport_input_sequences.find(world_id); it != m_transport_input_sequences.end()) {
                        sequence = it->value + 1;
                        it->value = sequence;
                    } else {
                        m_transport_input_sequences.set(world_id, sequence);
                    }

                    u64 host_frame_id = 1;
                    if (auto it = m_transport_host_frame_ids.find(world_id); it != m_transport_host_frame_ids.end()) {
                        host_frame_id = it->value + 1;
                        it->value = host_frame_id;
                    } else {
                        m_transport_host_frame_ids.set(world_id, host_frame_id);
                    }

                    ProcessTransport::InputRecord record;
                    record.sequence = sequence;
                    record.tick.host_frame_id = host_frame_id;
                    record.tick.delta_time_seconds = delta_time;
                    record.tick.host_timestamp_ns = MonotonicTime::now().nanoseconds();
                    record.input = input_frame;

                    auto push_result = transport->input_queue()->push_input_record(record);
                    auto dropped_count = transport->input_queue()->dropped_record_count();
                    auto last_dropped_count = m_transport_dropped_input_counts.get(world_id).value_or(0);
                    if (dropped_count > last_dropped_count && input_path_self_test_enabled && !m_logged_transport_input_overwrite) {
                        m_logged_transport_input_overwrite = true;
                        outln("WorldManagementSystem: input path self-test observed latest-wins overwrite for world {}", world_id);
                    }
                    m_transport_dropped_input_counts.set(world_id, dropped_count);

                    if (push_result.is_error()) {
                        warnln("WorldManagementSystem: failed to push shared transport input for world {}: {}", world_id, push_result.error());
                    } else {
                        auto signal_result = transport->host_to_world_wake()->signal();
                        if (signal_result.is_error()) {
                            warnln("WorldManagementSystem: failed to signal shared transport wake for world {}: {}", world_id, signal_result.error());
                        } else {
                            if (input_path_self_test_enabled && !m_logged_transport_input_shared_path) {
                                m_logged_transport_input_shared_path = true;
                                outln("WorldManagementSystem: input path self-test confirmed shared transport publish for world {}", world_id);
                            }
                        }
                    }
                }
            }

        }
    }

    for (auto& entry : m_world_runtimes) {
        auto& runtime = *entry.value;
        if (!runtime.is_initialized())
            continue;
        if (runtime.state() == WorldRuntimeState::Suspended || runtime.state() == WorldRuntimeState::Stopped)
            continue;
        if (runtime.is_faulted())
            continue;

        runtime.enqueue_network_events(m_network_service.poll_events(runtime.id()));
        runtime.update(delta_time, input_frame);
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

WorldHostConnection* WorldManagementSystem::foreground_world_process_connection()
{
    auto it = m_world_process_connections.find(m_session.active_world_id());
    if (it == m_world_process_connections.end())
        return nullptr;
    return it->value.ptr();
}

WorldHostConnection const* WorldManagementSystem::foreground_world_process_connection() const
{
    auto it = m_world_process_connections.find(m_session.active_world_id());
    if (it == m_world_process_connections.end())
        return nullptr;
    return it->value.ptr();
}

ProcessTransport::WorldProcessTransportHost* WorldManagementSystem::foreground_world_process_transport()
{
    return world_process_transport(m_session.active_world_id());
}

ProcessTransport::WorldProcessTransportHost const* WorldManagementSystem::foreground_world_process_transport() const
{
    return world_process_transport(m_session.active_world_id());
}

ProcessTransport::WorldProcessTransportHost* WorldManagementSystem::world_process_transport(WorldId world_id)
{
    auto it = m_world_process_transports.find(world_id);
    if (it == m_world_process_transports.end())
        return nullptr;
    return it->value.ptr();
}

ProcessTransport::WorldProcessTransportHost const* WorldManagementSystem::world_process_transport(WorldId world_id) const
{
    auto it = m_world_process_transports.find(world_id);
    if (it == m_world_process_transports.end())
        return nullptr;
    return it->value.ptr();
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

    auto runtime = MUST(adopt_nonnull_ref_or_enomem(new (nothrow) WorldRuntime(this, world_id, MUST(String::from_utf8(world_name.bytes())))));
    auto* runtime_ptr = runtime.ptr();
    m_world_runtimes.set(world_id, move(runtime));
    return *runtime_ptr;
}

WorldRuntime* WorldManagementSystem::find_runtime_by_source_path(StringView source_path) const
{
    for (auto& entry : m_world_runtimes) {
        auto& runtime = *entry.value;
        if (!runtime.boot_info().has_value())
            continue;
        if (runtime.boot_info()->source_path == source_path)
            return entry.value;
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
    if (m_in_process_mode) {
        if (prepared.has_value()) {
            runtime.set_bootstrap_info(build_bootstrap_info(runtime.id(), prepared->manifest));
            runtime.set_script_tick_budget_ms(prepared->manifest.script_tick_budget_ms);
            runtime.set_background_tick_rate(prepared->manifest.background_tick_rate);
        }

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
    } else {
        // Subprocess mode
        ProcessTransport::LinuxTransportFactory::Options transport_options;
        if (auto static_scene_override = transport_static_scene_bytes_override_from_environment(); static_scene_override.has_value()) {
            transport_options.static_scene_capacity_bytes = *static_scene_override;
            outln("WorldManagementSystem: overriding initial transport static scene region to {} bytes", *static_scene_override);
        }
        auto transport_bundle = TRY(ProcessTransport::LinuxTransportFactory::create_spawn_bundle(transport_options));
        auto should_run_transport_bootstrap_self_test = false;
        if (!m_requested_transport_bootstrap_self_test && transport_bootstrap_self_test_requested_by_environment()) {
            should_run_transport_bootstrap_self_test = true;
            m_requested_transport_bootstrap_self_test = true;
        }
        ProcessTransport::LinuxTransportFactory::log_bootstrap_validation("host"sv, transport_bundle.spawn_info, transport_bundle.bootstrap_config);

        int fds[2];
        TRY(Core::System::socketpair(AF_LOCAL, SOCK_STREAM, 0, fds));
        
        auto world_process_binary = LexicalPath::join(LexicalPath::dirname(TRY(Core::System::current_executable_path())), "MyceliumWorld"sv).string();
        
        Vector<ByteString> arguments;
        arguments.append("--world-id");
        arguments.append(ByteString::number(runtime.id()));
        arguments.append("--world-path");
        arguments.append(request.world_path);
        arguments.append("--ipc-fd");
        arguments.append(ByteString::number(fds[1]));
        arguments.append("--transport-bootstrap-fd");
        arguments.append(ByteString::number(transport_bundle.spawn_info.bootstrap_fd));
        arguments.append("--transport-host-to-world-wake-fd");
        arguments.append(ByteString::number(transport_bundle.spawn_info.host_to_world_wake_fd));
        arguments.append("--transport-world-to-host-wake-fd");
        arguments.append(ByteString::number(transport_bundle.spawn_info.world_to_host_wake_fd));
        arguments.append("--transport-input-queue-fd");
        arguments.append(ByteString::number(transport_bundle.spawn_info.input_queue_fd));
        arguments.append("--transport-frame-mailbox-fd");
        arguments.append(ByteString::number(transport_bundle.spawn_info.frame_mailbox_fd));
        arguments.append("--transport-static-scene-fd");
        arguments.append(ByteString::number(transport_bundle.spawn_info.static_scene_region_fd));
        if (should_run_transport_bootstrap_self_test)
            arguments.append("--transport-bootstrap-self-test");
        
        Core::ProcessSpawnOptions options {
            .name = "MyceliumWorld"sv,
            .executable = world_process_binary,
            .arguments = arguments,
            .file_actions = {
                Core::FileAction::DupFd { .write_fd = fds[1], .fd = fds[1] },
                Core::FileAction::DupFd { .write_fd = transport_bundle.spawn_info.bootstrap_fd, .fd = transport_bundle.spawn_info.bootstrap_fd },
                Core::FileAction::DupFd { .write_fd = transport_bundle.spawn_info.host_to_world_wake_fd, .fd = transport_bundle.spawn_info.host_to_world_wake_fd },
                Core::FileAction::DupFd { .write_fd = transport_bundle.spawn_info.world_to_host_wake_fd, .fd = transport_bundle.spawn_info.world_to_host_wake_fd },
                Core::FileAction::DupFd { .write_fd = transport_bundle.spawn_info.input_queue_fd, .fd = transport_bundle.spawn_info.input_queue_fd },
                Core::FileAction::DupFd { .write_fd = transport_bundle.spawn_info.frame_mailbox_fd, .fd = transport_bundle.spawn_info.frame_mailbox_fd },
                Core::FileAction::DupFd { .write_fd = transport_bundle.spawn_info.static_scene_region_fd, .fd = transport_bundle.spawn_info.static_scene_region_fd }
            }
        };
        
        auto process = TRY(Core::Process::spawn(options));
        TRY(Core::System::close(fds[1]));
        
        auto socket = TRY(Core::LocalSocket::adopt_fd(fds[0]));
        auto transport = TRY(IPC::TransportSocket::from_socket(move(socket)));
        auto connection = WorldHostConnection::construct(move(transport), process.pid(), *this, runtime.id());
        m_world_process_connections.set(runtime.id(), connection);
        TRY(ProcessTransport::LinuxTransportFactory::send_bootstrap_config(*transport_bundle.host.bootstrap_channel(), transport_bundle.bootstrap_config));
        if (should_run_transport_bootstrap_self_test)
            TRY(ProcessTransport::LinuxTransportFactory::run_bootstrap_self_test_host(transport_bundle.host, transport_bundle.spawn_info, transport_bundle.bootstrap_config));
        auto* bootstrap_channel = dynamic_cast<ProcessTransport::LinuxBootstrapChannel*>(transport_bundle.host.bootstrap_channel());
        VERIFY(bootstrap_channel);
        auto control_bus_discovery = TRY(ProcessTransport::LinuxTransportFactory::receive_control_bus_discovery(*bootstrap_channel));
        connection->configure_control_bus(control_bus_discovery.port, control_bus_discovery.token);
        notify_control_bus_ready(runtime.id(), control_bus_discovery.port, control_bus_discovery.token);
        m_world_process_transports.set(runtime.id(), make<ProcessTransport::WorldProcessTransportHost>(move(transport_bundle.host)));
        m_world_process_pids.set(runtime.id(), process.pid());
        
        outln("Spawned MyceliumWorld process (pid={}) for world '{}'", process.pid(), runtime.id());
    }
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
    if (m_overlay_manager && runtime.control_bus().is_running())
        m_overlay_manager->connect_to_world(runtime.control_bus().port(), runtime.control_bus().capability_token());

    if (m_in_process_mode) {
        runtime.set_log_callback([this, &runtime](StringView level, StringView source, StringView message) {
            if (m_runtime_log_callback)
                m_runtime_log_callback(level, source, message);

            JsonObject data;
            data.set("level"sv, level);
            data.set("source"sv, source);
            data.set("message"sv, message);
            runtime.control_bus().send_event("log.entry"sv, data);
        });
    }

    if (m_in_process_mode) {
        runtime.bridge_backend().set_selection_changed_callback([&runtime](EntityId entity) {
            JsonObject data;
            data.set("entity_id"sv, static_cast<u32>(entity));
            runtime.control_bus().send_event("entity.selected"sv, data);
        });
    }
}

void WorldManagementSystem::rebind_foreground_runtime_callbacks()
{
    auto* runtime = foreground_runtime();
    if (!runtime || !runtime->is_initialized())
        return;
    bind_runtime_callbacks(*runtime);
}

void WorldManagementSystem::on_world_process_ready(WorldId world_id)
{
    if (auto it = m_world_process_transports.find(world_id); it != m_world_process_transports.end()) {
        auto& transport = *it->value;
        if (transport.is_configured()) {
            transport.input_queue()->reset();
            if (auto result = transport.host_to_world_wake()->drain(); result.is_error())
                warnln("WorldManagementSystem: failed to drain host-to-world wake during world-ready handoff for world {}: {}", world_id, result.error());
        }
    }
    m_transport_input_sequences.set(world_id, 0);
    m_transport_host_frame_ids.set(world_id, 0);
    m_transport_dropped_input_counts.set(world_id, 0);
    m_logged_transport_input_shared_path = false;
    m_logged_transport_input_overwrite = false;
}

void WorldManagementSystem::reload_world(WorldId world_id)
{
    auto it = m_world_runtimes.find(world_id);
    if (it == m_world_runtimes.end())
        return;
    
    auto& runtime = *it->value;
    auto boot_info = runtime.boot_info();
    if (!boot_info.has_value())
        return;

    outln("Reloading world '{}'...", world_id);
    
    // Stop and unmount
    unmount_runtime(runtime);
    runtime.shutdown();
    
    // Cleanup process tracking
    m_world_process_connections.remove(world_id);
    m_world_process_transports.remove(world_id);
    m_world_process_pids.remove(world_id);

    // Boot again
    (void)boot({
        .world_path = boot_info->source_path,
        .script_path = {}, // Uses manifest entry script
        .control_script_path = "UI/MyceliumVR/scripts/controls.js",
        .has_script_path_override = false,
    });
}

void WorldManagementSystem::on_world_process_exit(WorldId world_id, i32 exit_code)
{
    (void)exit_code;
    if (!m_world_process_pids.contains(world_id))
        return;
    
    m_world_process_pids.remove(world_id);
    m_world_process_connections.remove(world_id);
    m_world_process_transports.remove(world_id);

    notify_world_faulted(world_id, "Process exited unexpectedly");
}

void WorldManagementSystem::notify_world_faulted(WorldId world_id, ByteString reason)
{
    auto it = m_world_runtimes.find(world_id);
    if (it == m_world_runtimes.end())
        return;

    auto& runtime = *it->value;
    runtime.mark_faulted(MUST(String::from_utf8(reason.view())));

    if (m_overlay_manager)
        m_overlay_manager->notify_world_faulted(world_id, reason);
}

void WorldManagementSystem::notify_control_bus_ready(WorldId world_id, u16 port, ByteString token)
{
    if (m_overlay_manager && (world_id == m_session.active_world_id() || world_id == m_session.loading_world_id()))
        m_overlay_manager->connect_to_world(port, token);
}

}
