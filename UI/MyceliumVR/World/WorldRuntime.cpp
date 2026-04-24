/*
 * MyceliumVR - a social VR platform built on the Ladybird browser engine
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "WorldRuntime.h"
#include "WorldManagementSystem.h"
#include "RenderSnapshot.h"

#include "../Scripting/BridgeBackend.h"
#include "../Scripting/ScriptHost.h"
#include "../Scripting/ScriptRuntime.h"
#include "../Scripting/WasmRuntime.h"
#include "../Support/InputState.h"
#include "../Support/VirtualFileSystem.h"

#include <chrono>

namespace MyceliumVR {

static void make_trs_matrix(Transform const& t, float* m)
{
    float qx = t.rotation[0], qy = t.rotation[1], qz = t.rotation[2], qw = t.rotation[3];
    float sx = t.scale[0], sy = t.scale[1], sz = t.scale[2];
    float tx = t.position[0], ty = t.position[1], tz = t.position[2];
    float r00 = 1.0f - 2.0f * (qy * qy + qz * qz);
    float r10 = 2.0f * (qx * qy + qz * qw);
    float r20 = 2.0f * (qx * qz - qy * qw);
    float r01 = 2.0f * (qx * qy - qz * qw);
    float r11 = 1.0f - 2.0f * (qx * qx + qz * qz);
    float r21 = 2.0f * (qy * qz + qx * qw);
    float r02 = 2.0f * (qx * qz + qy * qw);
    float r12 = 2.0f * (qy * qz - qx * qw);
    float r22 = 1.0f - 2.0f * (qx * qx + qy * qy);
    m[0]  = sx * r00;  m[1]  = sx * r10;  m[2]  = sx * r20;  m[3]  = 0.0f;
    m[4]  = sy * r01;  m[5]  = sy * r11;  m[6]  = sy * r21;  m[7]  = 0.0f;
    m[8]  = sz * r02;  m[9]  = sz * r12;  m[10] = sz * r22;  m[11] = 0.0f;
    m[12] = tx;        m[13] = ty;        m[14] = tz;        m[15] = 1.0f;
}

WorldRuntime::WorldRuntime(WorldManagementSystem* manager, WorldId id, String name)
    : m_id(id)
    , m_name(move(name))
    , m_world_manager(manager)
    , m_world(make<World>())
    , m_control_bus(make<ControlBusServer>(manager))
{
    m_control_bus->set_runtime(this);
}

WorldRuntime::~WorldRuntime()
{
    shutdown();
}

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

void WorldRuntime::set_state(WorldRuntimeState state)
{
    m_state.store(state);
    if (m_runtime_host)
        m_runtime_host->set_runtime_state(static_cast<u32>(state));
    if (m_worker_started) {
        std::lock_guard lock(m_worker_mutex);
        m_worker_condition.notify_one();
    }
}

ErrorOr<void> WorldRuntime::initialize(ScriptHost& script_host, VirtualFileSystem* virtual_file_system, InputState* input_state, WorldRuntimeHost* runtime_host, bool use_worker_thread)
{
    m_script_host = &script_host;
    m_runtime_host = runtime_host;
    m_world_state = WorldState::Running;
    m_fault_reason = {};
    m_script_runtime = make<ScriptRuntime>(
        script_host,
        *m_world,
        m_world_manager ? &m_world_manager->network_service() : nullptr,
        virtual_file_system,
        input_state,
        runtime_host,
        this);
    TRY(m_script_runtime->initialize());
    TRY(m_control_bus->start(m_id));
    
    if (use_worker_thread) {
        std::lock_guard lock(m_worker_mutex);
        m_worker_should_stop = false;
        m_update_pending = false;
        m_pending_delta_time = 0.0;
        
        m_worker_thread = std::thread([this] {
            auto last_tick_time = std::chrono::steady_clock::now();
            for (;;) {
                double delta_time = 0.0;
                InputFrameState input_state;
                bool background_tick = false;

                {
                    std::unique_lock lock(m_worker_mutex);
                    if (m_state.load() == WorldRuntimeState::Foreground) {
                        m_worker_condition.wait(lock, [this] { return m_worker_should_stop || m_update_pending || m_state.load() != WorldRuntimeState::Foreground; });
                    } else {
                        auto tick_rate = m_background_tick_rate;
                        if (tick_rate == 0) tick_rate = 1;
                        auto interval = std::chrono::milliseconds(1000 / tick_rate);
                        m_worker_condition.wait_for(lock, interval, [this] { return m_worker_should_stop || m_update_pending || m_state.load() == WorldRuntimeState::Foreground; });
                        background_tick = !m_update_pending;
                    }

                    if (m_worker_should_stop)
                        break;
                    
                    if (m_update_pending) {
                        delta_time = m_pending_delta_time;
                        input_state = m_pending_input_state;
                        m_update_pending = false;
                        last_tick_time = std::chrono::steady_clock::now();
                    } else if (background_tick && m_state.load() == WorldRuntimeState::Background) {
                        auto now = std::chrono::steady_clock::now();
                        delta_time = std::chrono::duration<double>(now - last_tick_time).count();
                        last_tick_time = now;
                        // Use empty input for background ticks
                    } else {
                        continue;
                    }
                }

                tick(delta_time, move(input_state));
            }
        });
        m_worker_started = true;
    }

    m_phase = WorldLifecyclePhase::Start;
    m_state = WorldRuntimeState::Foreground;
    return {};
}

void WorldRuntime::tick(double delta_time, InputFrameState input_state)
{
    if (!m_script_runtime || is_faulted())
        return;

    auto tick_started_at = std::chrono::steady_clock::now();

    if (m_networking_wasm) {
        std::lock_guard world_lock(m_world_mutex);
        m_networking_wasm->update(delta_time);
        if (m_networking_wasm->is_faulted()) {
            mark_faulted(MUST(String::formatted("WASM runtime faulted: {}", m_networking_wasm->fault_reason())));
            return;
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
        return;
    }

    auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - tick_started_at).count();
    if (m_script_tick_budget_ms > 0 && elapsed_ms > m_script_tick_budget_ms)
        mark_faulted(MUST(String::formatted("Script tick exceeded budget ({}ms > {}ms)", elapsed_ms, m_script_tick_budget_ms)));
}

bool WorldRuntime::sync_scene_revision_for_static_publish()
{
    std::lock_guard world_lock(m_world_mutex);
    if (m_world->layout_dirty() && !m_static_scene_publish_pending) {
        ++m_scene_revision;
        m_static_scene_publish_pending = true;
    }
    return m_static_scene_publish_pending;
}

size_t WorldRuntime::required_static_scene_snapshot_bytes()
{
    std::lock_guard world_lock(m_world_mutex);

    if (!m_static_scene_publish_pending)
        return 0;

    size_t mesh_group_upper_bound = 0;
    auto mesh_view = m_world->registry().view<MeshRenderer const>(entt::exclude<Panel>);
    for (auto entity : mesh_view) {
        (void)entity;
        ++mesh_group_upper_bound;
    }

    size_t panel_count = 0;
    auto panel_view = m_world->registry().view<Panel const>();
    for (auto entity : panel_view) {
        (void)entity;
        ++panel_count;
    }

    return static_scene_slot_bytes(mesh_group_upper_bound, panel_count);
}

bool WorldRuntime::build_static_scene_snapshot(Span<u8> slot)
{
    std::lock_guard world_lock(m_world_mutex);

    if (!m_static_scene_publish_pending)
        return false;

    struct GroupKey {
        u32 mesh_handle;
        u32 material_handle;
        u8 alpha_mode;
        u8 cull_mode;
        u8 has_normal_map;

        bool operator==(GroupKey const& other) const
        {
            return mesh_handle == other.mesh_handle
                && material_handle == other.material_handle
                && alpha_mode == other.alpha_mode
                && cull_mode == other.cull_mode
                && has_normal_map == other.has_normal_map;
        }
    };

    Vector<StaticDrawGroup> draw_groups;
    HashMap<u32, u32> group_index_map;

    auto get_group_key_hash = [](GroupKey const& k) {
        return k.mesh_handle ^ (k.material_handle << 8) ^ (k.alpha_mode << 16) ^ (k.cull_mode << 20) ^ (k.has_normal_map << 24);
    };

    auto mesh_view = m_world->registry().view<MeshRenderer const>(entt::exclude<Panel>);
    for (auto entity : mesh_view) {
        auto const& mesh_renderer = mesh_view.get<MeshRenderer const>(entity);

        u32 mesh_handle = 0;
        u32 material_handle = 0;
        if (m_runtime_host) {
            mesh_handle = m_runtime_host->register_mesh(m_id, mesh_renderer.mesh.bytes_as_string_view());
            material_handle = m_runtime_host->register_material(m_id, mesh_renderer.material.bytes_as_string_view());
        }

        u8 alpha_mode = 0;
        if (m_world->registry().all_of<AlphaClip>(entity))
            alpha_mode = 1;
        else if (m_world->registry().all_of<AlphaBlend>(entity))
            alpha_mode = 2;
        else if (m_world->registry().all_of<AlphaHash>(entity))
            alpha_mode = 3;

        u8 cull_mode = 0;
        if (auto const* override_comp = m_world->registry().try_get<CullOverride>(entity); override_comp)
            cull_mode = static_cast<u8>(override_comp->mode);

        u8 has_normal_map = !mesh_renderer.normal_map.is_empty() ? 1 : 0;
        GroupKey key { mesh_handle, material_handle, alpha_mode, cull_mode, has_normal_map };
        u32 key_hash = get_group_key_hash(key);
        if (group_index_map.contains(key_hash))
            continue;

        StaticDrawGroup group {};
        group.draw_group_id = draw_groups.size();
        group.mesh_handle = mesh_handle;
        group.material_handle = material_handle;
        group.alpha_mode = alpha_mode;
        group.cull_mode = cull_mode;
        group.has_normal_map = has_normal_map;
        draw_groups.append(group);
        group_index_map.set(key_hash, group.draw_group_id);
    }

    Vector<StaticPanelEntry> panels;
    auto panel_view = m_world->registry().view<Panel const>();
    for (auto entity : panel_view) {
        auto const& panel = panel_view.get<Panel const>(entity);
        StaticPanelEntry entry {};
        if (m_runtime_host)
            entry.panel_handle = m_runtime_host->register_panel(m_id, static_cast<u32>(entity), panel.url.bytes_as_string_view(), panel.width, panel.height);
        entry.width = panel.width;
        entry.height = panel.height;
        panels.append(entry);
    }

    size_t total_bytes = static_scene_slot_bytes(draw_groups.size(), panels.size());
    VERIFY(slot.size() >= total_bytes);

    StaticSceneHeader header {};
    header.scene_revision = m_scene_revision;
    header.draw_group_count = draw_groups.size();
    header.panel_count = panels.size();
    header.slot_bytes = total_bytes;

    u8* ptr = slot.data();
    memcpy(ptr, &header, sizeof(header));
    ptr += sizeof(header);
    if (!draw_groups.is_empty()) {
        memcpy(ptr, draw_groups.data(), sizeof(StaticDrawGroup) * draw_groups.size());
        ptr += sizeof(StaticDrawGroup) * draw_groups.size();
    }
    if (!panels.is_empty())
        memcpy(ptr, panels.data(), sizeof(StaticPanelEntry) * panels.size());

    m_static_scene_publish_pending = false;
    return true;
}

void WorldRuntime::build_render_snapshot(Span<u8> slot)
{
    std::lock_guard world_lock(m_world_mutex);

    // 1. Gather counts and components
    Vector<SnapshotCamera> cameras;
    if (m_runtime_host) {
        auto camera_state = m_runtime_host->camera_state();
        bool camera_moved = camera_state.position[0] != m_last_snapshot_camera_state.position[0]
            || camera_state.position[1] != m_last_snapshot_camera_state.position[1]
            || camera_state.position[2] != m_last_snapshot_camera_state.position[2]
            || camera_state.yaw_degrees != m_last_snapshot_camera_state.yaw_degrees
            || camera_state.pitch_degrees != m_last_snapshot_camera_state.pitch_degrees;
        if (camera_moved && m_logged_snapshot_camera_motion) {
            // no-op after first report
        } else if (camera_moved) {
            m_logged_snapshot_camera_motion = true;
            outln("MyceliumWorld: writing moved camera into snapshot pos=({}, {}, {}) yaw={} pitch={}",
                camera_state.position[0], camera_state.position[1], camera_state.position[2],
                camera_state.yaw_degrees, camera_state.pitch_degrees);
        }
        m_last_snapshot_camera_state = camera_state;
        SnapshotCamera main_cam {};
        main_cam.entity_id = 0; // Host doesn't use this for the main camera yet
        memcpy(main_cam.position, camera_state.position, sizeof(float) * 3);
        main_cam.yaw_degrees = camera_state.yaw_degrees;
        main_cam.pitch_degrees = camera_state.pitch_degrees;
        main_cam.fov_degrees = 60.0f; // TODO: get from camera_state
        main_cam.near_plane = 0.1f;
        main_cam.far_plane = 100.0f;
        main_cam.render_target_panel_handle = 0;
        cameras.append(main_cam);
    }

    struct GroupKey {
        u32 mesh_handle;
        u32 material_handle;
        u8 alpha_mode;
        u8 cull_mode;
        u8 has_normal_map;

        bool operator==(GroupKey const& other) const {
            return mesh_handle == other.mesh_handle && material_handle == other.material_handle && alpha_mode == other.alpha_mode && cull_mode == other.cull_mode && has_normal_map == other.has_normal_map;
        }
    };

    HashMap<u32, Vector<EntityId>> group_entities;
    Vector<GroupKey> groups;
    HashMap<u32, u32> group_index_map; // Hash of GroupKey -> index in groups

    auto get_group_key_hash = [](GroupKey const& k) {
        return k.mesh_handle ^ (k.material_handle << 8) ^ (k.alpha_mode << 16) ^ (k.cull_mode << 20) ^ (k.has_normal_map << 24);
    };

    auto mesh_view = m_world->registry().view<Transform const, MeshRenderer const>(entt::exclude<Panel>);
    for (auto entity : mesh_view) {
        auto const& mesh_renderer = mesh_view.get<MeshRenderer const>(entity);
        
        u32 mesh_handle = 0;
        u32 material_handle = 0;
        if (m_runtime_host) {
            mesh_handle = m_runtime_host->register_mesh(m_id, mesh_renderer.mesh.bytes_as_string_view());
            material_handle = m_runtime_host->register_material(m_id, mesh_renderer.material.bytes_as_string_view());
        }

        u8 alpha_mode = 0;
        if (m_world->registry().all_of<AlphaClip>(entity)) alpha_mode = 1;
        else if (m_world->registry().all_of<AlphaBlend>(entity)) alpha_mode = 2;
        else if (m_world->registry().all_of<AlphaHash>(entity)) alpha_mode = 3;

        u8 cull_mode = 0; // Back
        if (auto const* override_comp = m_world->registry().try_get<CullOverride>(entity); override_comp) {
            cull_mode = (u8)override_comp->mode;
        }

        bool has_normal_map = !mesh_renderer.normal_map.is_empty();

        GroupKey key { mesh_handle, material_handle, alpha_mode, cull_mode, (u8)has_normal_map };
        u32 key_hash = get_group_key_hash(key);
        
        u32 group_idx = 0;
        if (auto it = group_index_map.find(key_hash); it != group_index_map.end()) {
            group_idx = it->value;
        } else {
            group_idx = groups.size();
            groups.append(key);
            group_index_map.set(key_hash, group_idx);
        }
        group_entities.ensure(group_idx).append(entity);
    }

    Vector<SnapshotPanelEntry> panels;
    auto panel_view = m_world->registry().view<Transform const, Panel const>();
    for (auto entity : panel_view) {
        auto const& transform = panel_view.get<Transform const>(entity);
        auto const& panel = panel_view.get<Panel const>(entity);

        SnapshotPanelEntry entry {};
        if (m_runtime_host)
            entry.panel_handle = m_runtime_host->register_panel(m_id, static_cast<u32>(entity), panel.url.bytes_as_string_view(), panel.width, panel.height);
        make_trs_matrix(transform, entry.transform);
        panels.append(entry);
    }

    Vector<SnapshotLightEntry> lights;
    SceneLightData scene_light {};
    if (m_runtime_host) {
        scene_light = m_runtime_host->scene_light();
        for (int i = 0; i < scene_light.point_light_count; ++i) {
            auto const& pl = scene_light.point_lights[i];
            SnapshotLightEntry entry {};
            memcpy(entry.position, pl.position, sizeof(float) * 3);
            entry.radius = pl.radius;
            memcpy(entry.color, pl.color, sizeof(float) * 3);
            entry.intensity = pl.intensity;
            lights.append(entry);
        }
    }

    // 2. Build flat arrays
    Vector<FrameDrawGroup> draw_groups;
    Vector<SnapshotInstance> instances;

    for (u32 i = 0; i < groups.size(); ++i) {
        auto const& entities = group_entities.get(i).value();

        FrameDrawGroup dg {};
        dg.draw_group_id = i;
        dg.instance_count = entities.size();
        dg.instance_offset = instances.size();
        draw_groups.append(dg);

        for (auto entity : entities) {
            auto const& transform = mesh_view.get<Transform const>(entity);
            SnapshotInstance inst {};
            make_trs_matrix(transform, inst.transform);
            instances.append(inst);
        }
    }

    // 3. Write to slot
    size_t total_bytes = frame_slot_bytes(cameras.size(), draw_groups.size(), instances.size(), panels.size(), lights.size());
    VERIFY(slot.size() >= total_bytes);

    FrameHeader header {};
    header.frame_id = 0; // TODO
    header.scene_revision = m_scene_revision;
    header.camera_count = cameras.size();
    header.primary_camera_idx = 0;
    header.draw_group_count = draw_groups.size();
    header.instance_count = instances.size();
    header.panel_count = panels.size();
    header.point_light_count = lights.size();
    header.slot_bytes = total_bytes;

    header.ambient_intensity = scene_light.ambient_intensity;
    memcpy(header.ambient_rgb, scene_light.ambient_rgb, sizeof(float) * 3);
    header.sun_intensity = scene_light.light_intensity;
    memcpy(header.sun_direction, scene_light.light_to_xyz, sizeof(float) * 3);
    memcpy(header.sun_rgb, scene_light.light_rgb, sizeof(float) * 3);

    u8* ptr = slot.data();
    memcpy(ptr, &header, sizeof(header)); ptr += sizeof(header);
    if (!cameras.is_empty()) { memcpy(ptr, cameras.data(), sizeof(SnapshotCamera) * cameras.size()); ptr += sizeof(SnapshotCamera) * cameras.size(); }
    if (!draw_groups.is_empty()) { memcpy(ptr, draw_groups.data(), sizeof(FrameDrawGroup) * draw_groups.size()); ptr += sizeof(FrameDrawGroup) * draw_groups.size(); }
    if (!instances.is_empty()) { memcpy(ptr, instances.data(), sizeof(SnapshotInstance) * instances.size()); ptr += sizeof(SnapshotInstance) * instances.size(); }
    if (!panels.is_empty()) { memcpy(ptr, panels.data(), sizeof(SnapshotPanelEntry) * panels.size()); ptr += sizeof(SnapshotPanelEntry) * panels.size(); }
    if (!lights.is_empty()) { memcpy(ptr, lights.data(), sizeof(SnapshotLightEntry) * lights.size()); ptr += sizeof(SnapshotLightEntry) * lights.size(); }
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

ErrorOr<String, String> WorldRuntime::run_script(StringView source, StringView filename)
{
    VERIFY(m_script_runtime);
    return m_script_runtime->run_script(source, filename);
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
    if (!m_script_runtime && !m_worker_started && m_state == WorldRuntimeState::Stopped)
        return;

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
    if (m_world_manager)
        m_world_manager->network_service().close_world_connections(m_id);
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
    if (m_fault_callback)
        m_fault_callback(m_fault_reason);
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
