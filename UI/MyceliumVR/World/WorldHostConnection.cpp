/*
 * MyceliumVR - a social VR platform built on the Ladybird browser engine
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "WorldHostConnection.h"
#include "WorldManagementSystem.h"
#include "RenderSnapshot.h"
#include "../ProcessTransport/Linux/LinuxBootstrapChannel.h"
#include "../ProcessTransport/Linux/LinuxSharedMemoryRegion.h"
#include "../ProcessTransport/Linux/LinuxSyscalls.h"
#include "../ProcessTransport/Linux/LinuxTransportFactory.h"
#include "../Rendering/Renderer.h"

namespace MyceliumVR {

void WorldHostConnection::sync_transport_static_scene_region() const
{
    if (!m_world_ready)
        return;

    auto* transport = m_world_manager.world_process_transport(m_world_id);
    if (!transport || !transport->is_configured())
        return;

    auto latest_static_scene = transport->frame_mailbox()->latest_static_scene();
    if (!latest_static_scene.has_value())
        return;
    if (m_last_transport_static_scene_info.has_value() && m_last_transport_static_scene_info->scene_revision == latest_static_scene->scene_revision)
        return;

    auto bytes = transport->static_scene_region()->bytes();
    if (bytes.size() < sizeof(StaticSceneHeader))
        return;

    auto const& header = *reinterpret_cast<StaticSceneHeader const*>(bytes.data());
    auto bounded_slot_bytes = min(static_cast<size_t>(latest_static_scene->slot_bytes), bytes.size());
    if (header.slot_bytes >= sizeof(StaticSceneHeader) && header.slot_bytes <= bytes.size())
        bounded_slot_bytes = min(static_cast<size_t>(header.slot_bytes), bytes.size());

    m_last_static_scene_revision = latest_static_scene->scene_revision;
    m_last_static_scene_bytes = bounded_slot_bytes;
    m_last_transport_static_scene_info = latest_static_scene;
}

void WorldHostConnection::sync_transport_frame_mailbox() const
{
    if (!m_world_ready)
        return;

    auto* transport = m_world_manager.world_process_transport(m_world_id);
    if (!transport || !transport->is_configured())
        return;

    if (auto wake_status = transport->world_to_host_wake()->wait(0); !wake_status.is_error() && wake_status.value() == ProcessTransport::WakeWaitStatus::Signaled)
        (void)transport->world_to_host_wake()->drain();

    auto latest_frame = transport->frame_mailbox()->latest_frame_view();
    if (!latest_frame.has_value())
        return;
    if (m_last_transport_frame_view.has_value() && m_last_transport_frame_view->info.frame_sequence == latest_frame->info.frame_sequence)
        return;

    auto slot = latest_frame->bytes;
    if (slot.size() < sizeof(FrameHeader))
        return;

    auto const& header = *reinterpret_cast<FrameHeader const*>(slot.data());
    auto bounded_slot_bytes = min(static_cast<size_t>(latest_frame->info.slot_bytes), slot.size());
    if (header.slot_bytes >= sizeof(FrameHeader) && header.slot_bytes <= slot.size())
        bounded_slot_bytes = min(static_cast<size_t>(header.slot_bytes), slot.size());

    if (header.camera_count > 0 && bounded_slot_bytes >= sizeof(FrameHeader) + sizeof(SnapshotCamera) * header.camera_count) {
        auto const* cameras = reinterpret_cast<SnapshotCamera const*>(slot.data() + sizeof(FrameHeader));
        if (header.primary_camera_idx < header.camera_count) {
            auto const& camera = cameras[header.primary_camera_idx];
            bool camera_moved = camera.position[0] != m_last_snapshot_camera_state.position[0]
                || camera.position[1] != m_last_snapshot_camera_state.position[1]
                || camera.position[2] != m_last_snapshot_camera_state.position[2]
                || camera.yaw_degrees != m_last_snapshot_camera_state.yaw_degrees
                || camera.pitch_degrees != m_last_snapshot_camera_state.pitch_degrees;
            if (camera_moved && !m_logged_snapshot_camera_motion) {
                m_logged_snapshot_camera_motion = true;
                outln("WorldHostConnection: observed moved camera from snapshot pos=({}, {}, {}) yaw={} pitch={}",
                    camera.position[0], camera.position[1], camera.position[2],
                    camera.yaw_degrees, camera.pitch_degrees);
            }
            memcpy(m_last_snapshot_camera_state.position, camera.position, sizeof(float) * 3);
            m_last_snapshot_camera_state.yaw_degrees = camera.yaw_degrees;
            m_last_snapshot_camera_state.pitch_degrees = camera.pitch_degrees;
        }
    }

    m_last_ready_frame_id = static_cast<u32>(latest_frame->info.frame_sequence);
    m_last_ready_scene_revision = latest_frame->info.scene_revision;
    m_last_ready_slot_bytes = bounded_slot_bytes;
    latest_frame->bytes = latest_frame->bytes.slice(0, bounded_slot_bytes);
    m_last_transport_frame_view = latest_frame;
}

WorldHostConnection::WorldHostConnection(NonnullOwnPtr<IPC::Transport> transport, int client_id, WorldManagementSystem& world_manager, u32 world_id)
    : IPC::ConnectionFromClient<WorldClientEndpoint, WorldHostEndpoint>(*this, move(transport), client_id)
    , m_world_manager(world_manager)
    , m_world_id(world_id)
{
}

WorldHostConnection::~WorldHostConnection() = default;

void WorldHostConnection::die()
{
}

void WorldHostConnection::pump_control_bus()
{
    if (m_control_bus_client) {
        m_control_bus_client->poll();

        if (m_control_bus_client->is_connected() && !m_control_bus_initialized) {
            m_control_bus_initialized = true;
            (void)m_control_bus_client->send_request("system.info"sv);
            (void)m_control_bus_client->send_request("resources.list"sv);

            JsonObject params;
            JsonArray topics;
            (void)topics.append("*"_string);
            params.set("topics"sv, move(topics));
            (void)m_control_bus_client->send_request("events.subscribe"sv, params);
        }
    }
}

void WorldHostConnection::pump_transport_control()
{
    auto* transport = m_world_manager.world_process_transport(m_world_id);
    if (!transport || !transport->is_configured())
        return;

    auto* bootstrap_channel = dynamic_cast<ProcessTransport::LinuxBootstrapChannel*>(transport->bootstrap_channel());
    if (!bootstrap_channel)
        return;

    for (;;) {
        auto resize_message = ProcessTransport::LinuxTransportFactory::receive_static_scene_resize(*bootstrap_channel);
        if (resize_message.is_error()) {
            warnln("WorldHostConnection: failed to receive transport control message for world {}: {}", m_world_id, resize_message.error());
            return;
        }
        if (!resize_message.value().has_value())
            return;
        auto info = resize_message.release_value().release_value();
        remap_static_scene_region_from_fd(info.region_fd, info.region_bytes);
    }
}

void WorldHostConnection::configure_control_bus(u16 port, ByteString token)
{
    m_control_bus_client = make<ControlBusClient>();
    m_control_bus_initialized = false;
    m_control_bus_client->set_message_callback([this](JsonObject const& message) {
        handle_control_bus_message(message);
    });

    auto result = m_control_bus_client->connect(port, token);
    if (result.is_error()) {
        warnln("WorldHostConnection: failed to connect internal control bus client for world {}: {}", m_world_id, result.error());
        m_control_bus_client = nullptr;
        return;
    }
}

void WorldHostConnection::remap_static_scene_region_from_fd(int fd, size_t region_bytes)
{
    auto* transport = m_world_manager.world_process_transport(m_world_id);
    if (!transport || !transport->is_configured()) {
        warnln("WorldHostConnection: received static scene resize without configured transport");
        MUST(ProcessTransport::LinuxSyscalls::close_fd(fd));
        return;
    }

    auto region = ProcessTransport::LinuxSharedMemoryRegion::create_from_fd(fd, region_bytes);
    if (region.is_error()) {
        MUST(ProcessTransport::LinuxSyscalls::close_fd(fd));
        warnln("WorldHostConnection: failed to remap transport static scene region: {}", region.error());
        return;
    }

    transport->replace_static_scene_region(region.release_value());
    m_last_transport_static_scene_info.clear();
    m_last_static_scene_bytes = 0;
    outln("WorldHostConnection: resized transport static scene region ({} bytes)", region_bytes);
}

void WorldHostConnection::request_shutdown()
{
    if (m_control_bus_client && m_control_bus_client->is_connected()) {
        auto result = m_control_bus_client->send_request("world.shutdown"sv);
        if (!result.is_error())
            return;
        warnln("WorldHostConnection: failed to send control-bus shutdown request for world {}: {}", m_world_id, result.error());
    }
}

void WorldHostConnection::handle_control_bus_message(JsonObject const& message)
{
    auto type = message.get_string("type"sv).value_or(""_string);
    if (type == "event"sv) {
        auto event = message.get_string("event"sv).value_or(""_string);
        auto data = message.get_object("data"sv);

        if (event == "world.ready"sv) {
            if (!m_world_ready) {
                m_world_ready = true;
                m_world_manager.on_world_process_ready(m_world_id);
                outln("WorldHostConnection: world is ready");
            }
            return;
        }

        if (event == "world.faulted"sv && data.has_value()) {
            auto reason = data->get_string("reason"sv).value_or(""_string);
            warnln("WorldHostConnection: world faulted: {}", reason);
            m_world_manager.notify_world_faulted(m_world_id, reason.to_byte_string());
            return;
        }

        if (event == "log.entry"sv && data.has_value()) {
            auto level = data->get_string("level"sv).value_or(""_string);
            auto source = data->get_string("source"sv).value_or(""_string);
            auto log_message = data->get_string("message"sv).value_or(""_string);
            outln("[{}][{}] {}", level, source, log_message);
            return;
        }

        if (event == "mesh.registered"sv && data.has_value()) {
            auto handle = data->get_u32("handle"sv).value_or(0);
            auto virtual_path = data->get_string("virtual_path"sv).value_or(""_string);
            if (handle && !virtual_path.is_empty()) {
                m_meshes.set(virtual_path.to_byte_string(), handle);
                if (auto* renderer = m_world_manager.renderer()) {
                    renderer->register_mesh(handle, virtual_path.to_byte_string());
                    renderer->force_rebuild_scene();
                }
                m_last_ready_scene_revision = 0; // Force rebuild
            }
            return;
        }

        if (event == "material.registered"sv && data.has_value()) {
            auto handle = data->get_u32("handle"sv).value_or(0);
            auto virtual_path = data->get_string("virtual_path"sv).value_or(""_string);
            if (handle && !virtual_path.is_empty()) {
                m_materials.set(virtual_path.to_byte_string(), handle);
                if (auto* renderer = m_world_manager.renderer()) {
                    renderer->register_material(handle, virtual_path.to_byte_string());
                    renderer->force_rebuild_scene();
                }
                m_last_ready_scene_revision = 0; // Force rebuild
            }
            return;
        }

        if (event == "panel.registered"sv && data.has_value()) {
            auto handle = data->get_u32("handle"sv).value_or(0);
            auto entity_id = data->get_u32("entity_id"sv).value_or(0);
            auto url = data->get_string("url"sv).value_or(""_string);
            auto width = data->get_double_with_precision_loss("width"sv).value_or(0.0);
            auto height = data->get_double_with_precision_loss("height"sv).value_or(0.0);
            if (handle) {
                m_panels.set(entity_id, handle);
                if (auto* renderer = m_world_manager.renderer()) {
                    renderer->register_panel(handle, entity_id, url.to_byte_string(), static_cast<float>(width), static_cast<float>(height));
                    renderer->force_rebuild_scene();
                }
                m_last_ready_scene_revision = 0; // Force rebuild
            }
            return;
        }
        return;
    }

    if (type == "response"sv) {
        auto ok = message.get_bool("ok"sv).value_or(false);
        if (!ok)
            return;
        auto result = message.get_object("result"sv);
        if (!result.has_value())
            return;
        if (result->has("world_id"sv) && result->get_bool("ready"sv).value_or(false)) {
            if (!m_world_ready) {
                m_world_ready = true;
                m_world_manager.on_world_process_ready(m_world_id);
                outln("WorldHostConnection: world is ready");
            }
        }

        if (result->has("meshes"sv)) {
            auto meshes = result->get_object("meshes"sv);
            size_t count = 0;
            meshes->for_each_member([&](auto const& virtual_path, auto const& handle_value) {
                auto handle = handle_value.template as_integer<u32>();
                if (!m_meshes.contains(virtual_path.to_byte_string())) {
                    m_meshes.set(virtual_path.to_byte_string(), handle);
                    if (auto* renderer = m_world_manager.renderer()) {
                        renderer->register_mesh(handle, virtual_path.to_byte_string());
                        renderer->force_rebuild_scene();
                    }
                    count++;
                }
            });
            if (count > 0)
                m_last_ready_scene_revision = 0;
        }

        if (result->has("materials"sv)) {
            auto materials = result->get_object("materials"sv);
            size_t count = 0;
            materials->for_each_member([&](auto const& virtual_path, auto const& handle_value) {
                auto handle = handle_value.template as_integer<u32>();
                if (!m_materials.contains(virtual_path.to_byte_string())) {
                    m_materials.set(virtual_path.to_byte_string(), handle);
                    if (auto* renderer = m_world_manager.renderer()) {
                        renderer->register_material(handle, virtual_path.to_byte_string());
                        renderer->force_rebuild_scene();
                    }
                    count++;
                }
            });
            if (count > 0)
                m_last_ready_scene_revision = 0;
        }

        if (result->has("panels"sv)) {
            auto panels = result->get_array("panels"sv);
            for (auto const& panel_value : panels->values()) {
                auto const& panel = panel_value.as_object();
                auto handle = panel.get_u32("handle"sv).value_or(0);
                auto entity_id = panel.get_u32("entity_id"sv).value_or(0);
                auto url = panel.get_string("url"sv).value_or(""_string);
                auto width = panel.get_double_with_precision_loss("width"sv).value_or(0.0);
                auto height = panel.get_double_with_precision_loss("height"sv).value_or(0.0);
                if (handle && !m_panels.contains(entity_id)) {
                    m_panels.set(entity_id, handle);
                    if (auto* renderer = m_world_manager.renderer())
                        renderer->register_panel(handle, entity_id, url.to_byte_string(), static_cast<float>(width), static_cast<float>(height));
                }
            }
        }
    }
}

bool WorldHostConnection::has_snapshot() const
{
    sync_transport_frame_mailbox();
    return m_last_transport_frame_view.has_value() && m_last_ready_slot_bytes >= sizeof(FrameHeader);
}

bool WorldHostConnection::has_static_scene() const
{
    sync_transport_static_scene_region();
    return m_last_transport_static_scene_info.has_value() && m_last_static_scene_bytes >= sizeof(StaticSceneHeader);
}

ReadonlyBytes WorldHostConnection::latest_snapshot() const
{
    if (!has_snapshot())
        return {};
    return m_last_transport_frame_view->bytes;
}

ReadonlyBytes WorldHostConnection::latest_static_scene() const
{
    if (!has_static_scene())
        return {};
    auto* transport = m_world_manager.world_process_transport(m_world_id);
    if (!transport || !transport->is_configured())
        return {};
    return transport->static_scene_region()->bytes().slice(0, m_last_static_scene_bytes);
}

}
