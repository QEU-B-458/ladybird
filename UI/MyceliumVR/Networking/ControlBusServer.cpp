/*
 * MyceliumVR - a social VR platform built on the Ladybird browser engine
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "ControlBusServer.h"
#include "../Scripting/BridgeBackend.h"
#include "../Support/VirtualFileSystem.h"
#include "../World/WorldManagementSystem.h"

#include <AK/Base64.h>
#include <AK/JsonArray.h>
#include <AK/JsonObject.h>
#include <AK/JsonValue.h>
#include <AK/Random.h>
#include <AK/StringBuilder.h>
#include <LibCrypto/Hash/SHA1.h>

namespace MyceliumVR {

namespace {

static ErrorOr<void> send_ws_text_frame(Core::BufferedTCPSocket& socket, StringView payload)
{
    u8 header[10];
    size_t header_length = 0;
    header[header_length++] = 0x81;

    auto payload_length = payload.length();
    if (payload_length < 126) {
        header[header_length++] = static_cast<u8>(payload_length);
    } else if (payload_length < 65536) {
        header[header_length++] = 126;
        header[header_length++] = static_cast<u8>((payload_length >> 8) & 0xff);
        header[header_length++] = static_cast<u8>(payload_length & 0xff);
    } else {
        header[header_length++] = 127;
        for (int shift = 7; shift >= 0; --shift)
            header[header_length++] = static_cast<u8>((payload_length >> (shift * 8)) & 0xff);
    }

    TRY(socket.write_until_depleted({ header, header_length }));
    TRY(socket.write_until_depleted(payload.bytes()));
    return {};
}

struct WebSocketFrame {
    u8 opcode { 0 };
    ByteBuffer payload;
};

static ErrorOr<WebSocketFrame> read_ws_frame(Core::BufferedTCPSocket& socket)
{
    u8 header[2];
    TRY(socket.read_until_filled({ header, sizeof(header) }));

    auto opcode = header[0] & 0x0f;
    bool masked = (header[1] & 0x80) != 0;
    u64 payload_length = header[1] & 0x7f;

    if (payload_length == 126) {
        u8 ext[2];
        TRY(socket.read_until_filled({ ext, sizeof(ext) }));
        payload_length = (static_cast<u64>(ext[0]) << 8) | ext[1];
    } else if (payload_length == 127) {
        u8 ext[8];
        TRY(socket.read_until_filled({ ext, sizeof(ext) }));
        payload_length = 0;
        for (auto byte : ext)
            payload_length = (payload_length << 8) | byte;
    }

    if (payload_length > NumericLimits<size_t>::max())
        return Error::from_string_literal("WebSocket frame too large");

    u8 mask[4] {};
    if (masked)
        TRY(socket.read_until_filled({ mask, sizeof(mask) }));

    auto payload = TRY(ByteBuffer::create_zeroed(static_cast<size_t>(payload_length)));
    if (payload_length > 0)
        TRY(socket.read_until_filled(payload));

    if (masked) {
        for (size_t i = 0; i < payload.size(); ++i)
            payload[i] ^= mask[i % 4];
    }

    return WebSocketFrame {
        .opcode = static_cast<u8>(opcode),
        .payload = move(payload),
    };
}

static ErrorOr<String> read_ws_text_frame(Core::BufferedTCPSocket& socket)
{
    auto frame = TRY(read_ws_frame(socket));
    if (frame.opcode == 0x8)
        return Error::from_string_literal("WebSocket peer closed");
    if (frame.opcode != 0x1)
        return Error::from_string_literal("Unsupported WebSocket opcode");
    return String::from_utf8(StringView { frame.payload });
}

static bool topic_matches(StringView subscription, StringView event)
{
    if (subscription == "*"sv)
        return true;
    if (subscription == event)
        return true;
    if (subscription.ends_with('*')) {
        auto prefix = subscription.substring_view(0, subscription.length() - 1);
        return event.starts_with(prefix);
    }
    return false;
}

static StringView world_state_name(WorldState state)
{
    switch (state) {
    case WorldState::Running:
        return "running"sv;
    case WorldState::Faulted:
        return "faulted"sv;
    case WorldState::Unloading:
        return "unloading"sv;
    }
    VERIFY_NOT_REACHED();
}

static StringView runtime_state_name(WorldRuntimeState state)
{
    switch (state) {
    case WorldRuntimeState::Foreground:
        return "foreground"sv;
    case WorldRuntimeState::Background:
        return "background"sv;
    case WorldRuntimeState::Suspended:
        return "suspended"sv;
    case WorldRuntimeState::Stopped:
        return "stopped"sv;
    }
    VERIFY_NOT_REACHED();
}

}

ControlBusServer::ControlBusServer(WorldManagementSystem& manager)
    : m_world_manager(manager)
{
}

ControlBusServer::~ControlBusServer()
{
    stop();
}

ErrorOr<void> ControlBusServer::start(u32 world_id)
{
    m_world_id = world_id;
    m_server = TRY(Core::TCPServer::try_create());
    m_server->on_ready_to_accept = [this] { on_ready_to_accept(); };

    TRY(m_server->listen(IPv4Address { 127, 0, 0, 1 }, 0));
    m_port = m_server->local_port().value();

    u8 token_bytes[16];
    fill_with_random({ token_bytes, sizeof(token_bytes) });
    m_capability_token = TRY(encode_base64({ token_bytes, sizeof(token_bytes) }));

    outln("ControlBus: World {} listening on ws://127.0.0.1:{}/ (token: {})", m_world_id, m_port, m_capability_token);
    return {};
}

void ControlBusServer::stop()
{
    if (m_server)
        m_server->unref();
    m_server = nullptr;
    m_clients.clear();
}

void ControlBusServer::on_ready_to_accept()
{
    auto socket_or_error = m_server->accept();
    if (socket_or_error.is_error())
        return;

    auto buffered_socket_or_error = Core::BufferedTCPSocket::create(socket_or_error.release_value());
    if (buffered_socket_or_error.is_error())
        return;

    auto client = adopt_ref(*new Client(buffered_socket_or_error.release_value()));
    client->socket->on_ready_to_read = [this, client] { on_client_data(client); };
    m_clients.set(client);
}

void ControlBusServer::on_client_data(Client& client)
{
    if (!client.handshaked) {
        u8 buffer[4096];
        auto bytes_read_or_error = client.socket->read_some({ buffer, sizeof(buffer) });
        if (bytes_read_or_error.is_error()) {
            m_clients.remove(client);
            return;
        }

        auto bytes = bytes_read_or_error.value();
        auto request = StringView { reinterpret_cast<char const*>(bytes.data()), bytes.size() };
        if (auto result = handle_handshake(client, request); result.is_error())
            m_clients.remove(client);
        return;
    }

    auto text_or_error = read_ws_text_frame(*client.socket);
    if (text_or_error.is_error()) {
        m_clients.remove(client);
        return;
    }

    if (auto result = handle_message(client, text_or_error.value()); result.is_error())
        warnln("ControlBus: message error: {}", result.error());
}

ErrorOr<void> ControlBusServer::handle_handshake(Client& client, StringView request)
{
    if (!request.contains("Upgrade: websocket"sv))
        return Error::from_string_literal("Not a WebSocket upgrade request");

    auto key_start = request.find("Sec-WebSocket-Key: "sv);
    if (!key_start.has_value())
        return Error::from_string_literal("Missing Sec-WebSocket-Key");

    auto key_line = request.substring_view(*key_start + 19);
    auto key_end = key_line.find("\r\n"sv);
    if (!key_end.has_value())
        return Error::from_string_literal("Malformed Sec-WebSocket-Key");
    auto key = key_line.substring_view(0, *key_end);

    if (!request.contains(m_capability_token))
        return Error::from_string_literal("Invalid capability token");

    StringBuilder accept_base;
    accept_base.append(key);
    accept_base.append("258EAFA5-E914-47DA-95CA-C5AB0DC85B11"sv);

    auto sha1 = Crypto::Hash::SHA1::create();
    sha1->update(accept_base.string_view().bytes());
    auto accept_key = TRY(encode_base64(sha1->digest().bytes()));

    StringBuilder response;
    response.append("HTTP/1.1 101 Switching Protocols\r\n"sv);
    response.append("Upgrade: websocket\r\n"sv);
    response.append("Connection: Upgrade\r\n"sv);
    response.appendff("Sec-WebSocket-Accept: {}\r\n\r\n", accept_key);

    TRY(client.socket->write_until_depleted(response.string_view().bytes()));
    client.handshaked = true;
    client.granted_capabilities.append("admin"_string);

    outln("ControlBus: client connected and handshaked.");
    return {};
}

ErrorOr<void> ControlBusServer::handle_message(Client& client, StringView message)
{
    auto json = TRY(JsonValue::from_string(message));
    if (!json.is_object())
        return Error::from_string_literal("Expected JSON object");

    auto const& object = json.as_object();
    auto id = object.get_string("id"sv).value_or("?"_string);
    auto method = object.get_string("method"sv).value_or(""_string);

    if (method == "system.info"sv)
        return handle_system_info(client, id);
    if (method == "entities.list"sv)
        return handle_entities_list(client, id);
    if (method == "entities.inspect"sv) {
        auto params = object.get_object("params"sv);
        auto entity_id = params.has_value() ? params->get_u32("entity_id"sv).value_or(0) : 0;
        return handle_entities_inspect(client, id, entity_id);
    }
    if (method == "entities.select"sv) {
        auto params = object.get_object("params"sv);
        auto entity_id = params.has_value() ? params->get_u32("entity_id"sv).value_or(0) : 0;
        return handle_entities_select(client, id, entity_id);
    }
    if (method == "contexts.list"sv)
        return handle_contexts_list(client, id);
    if (method == "contexts.restart"sv)
        return handle_contexts_restart(client, id, object);
    if (method == "filesystem.list_dir"sv) {
        auto params = object.get_object("params"sv);
        auto path = params.has_value() ? params->get_string("path"sv).value_or("/"_string) : "/"_string;
        return handle_filesystem_list_dir(client, id, path);
    }
    if (method == "filesystem.read_text"sv) {
        auto params = object.get_object("params"sv);
        auto path = params.has_value() ? params->get_string("path"sv).value_or(""_string) : ""_string;
        return handle_filesystem_read_text(client, id, path);
    }
    if (method == "metrics.snapshot"sv)
        return handle_metrics_snapshot(client, id);
    if (method == "events.subscribe"sv)
        return handle_events_subscribe(client, id, object);

    JsonObject error;
    error.set("code"sv, "method_not_found"sv);
    error.set("message"sv, MUST(String::from_byte_string(ByteString::formatted("Unknown method: {}", method))));
    send_response(client, id, false, {}, error);
    return {};
}

ErrorOr<void> ControlBusServer::handle_system_info(Client& client, StringView id)
{
    auto* runtime = m_world_manager.find_runtime(m_world_id);
    if (!runtime) {
        JsonObject error;
        error.set("code"sv, "world_not_found"sv);
        error.set("message"sv, "World runtime is not available"sv);
        send_response(client, id, false, {}, error);
        return {};
    }

    JsonObject result;
    result.set("world_id"sv, runtime->id());
    result.set("name"sv, runtime->name());
    result.set("world_state"sv, world_state_name(runtime->world_state()));
    result.set("runtime_state"sv, runtime_state_name(runtime->state()));
    result.set("fault_reason"sv, runtime->fault_reason());
    send_response(client, id, true, result);
    return {};
}

ErrorOr<void> ControlBusServer::handle_entities_list(Client& client, StringView id)
{
    auto* runtime = m_world_manager.find_runtime(m_world_id);
    if (!runtime)
        return Error::from_string_literal("World runtime is not available");

    JsonArray entries;
    runtime->with_world_lock([&](World&) {
        auto hierarchy = runtime->bridge_backend().get_entity_hierarchy();
        for (auto const& entry : hierarchy) {
            JsonObject object;
            object.set("entity_id"sv, static_cast<u32>(entry.id));
            object.set("name"sv, entry.name);
            object.set("kind"sv, entry.kind);
            object.set("alpha_mode"sv, entry.alpha_mode);
            object.set("is_static"sv, entry.is_static);
            object.set("depth"sv, entry.depth);
            if (entry.parent_id != entt::null)
                object.set("parent_id"sv, static_cast<u32>(entry.parent_id));
            (void)entries.append(move(object));
        }
    });

    JsonObject result;
    result.set("entities"sv, move(entries));
    send_response(client, id, true, result);
    return {};
}

ErrorOr<void> ControlBusServer::handle_entities_inspect(Client& client, StringView id, u32 entity_id)
{
    auto* runtime = m_world_manager.find_runtime(m_world_id);
    if (!runtime)
        return Error::from_string_literal("World runtime is not available");

    Optional<EntityComponentSnapshot> snapshot;
    runtime->with_world_lock([&](World&) {
        snapshot = runtime->bridge_backend().get_entity_components(static_cast<EntityId>(entity_id));
    });

    if (!snapshot.has_value()) {
        JsonObject error;
        error.set("code"sv, "entity_not_found"sv);
        error.set("message"sv, MUST(String::from_byte_string(ByteString::formatted("Entity {} is not available", entity_id))));
        send_response(client, id, false, {}, error);
        return {};
    }

    auto const& snap = *snapshot;
    JsonObject transform;
    transform.set("px"sv, snap.position[0]);
    transform.set("py"sv, snap.position[1]);
    transform.set("pz"sv, snap.position[2]);
    transform.set("qx"sv, snap.rotation[0]);
    transform.set("qy"sv, snap.rotation[1]);
    transform.set("qz"sv, snap.rotation[2]);
    transform.set("qw"sv, snap.rotation[3]);
    transform.set("sx"sv, snap.scale[0]);
    transform.set("sy"sv, snap.scale[1]);
    transform.set("sz"sv, snap.scale[2]);

    JsonArray components;
    for (auto const& component : snap.attached_components)
        (void)components.append(component);

    JsonObject result;
    result.set("id"sv, static_cast<u32>(snap.id));
    result.set("name"sv, snap.name);
    result.set("attached_components"sv, move(components));
    result.set("transform"sv, move(transform));
    result.set("has_mesh_renderer"sv, snap.has_mesh_renderer);
    result.set("mesh"sv, snap.mesh);
    result.set("material"sv, snap.material);
    result.set("normal_map"sv, snap.normal_map);
    result.set("has_panel"sv, snap.has_panel);
    result.set("panel_url"sv, snap.panel_url);
    result.set("panel_width"sv, snap.panel_width);
    result.set("panel_height"sv, snap.panel_height);
    result.set("has_cull_override"sv, snap.has_cull_override);
    result.set("cull_mode"sv, static_cast<u32>(snap.cull_mode));
    result.set("is_static"sv, snap.is_static);
    result.set("alpha_blend"sv, snap.alpha_blend);
    result.set("alpha_clip"sv, snap.alpha_clip);
    result.set("alpha_hash"sv, snap.alpha_hash);
    send_response(client, id, true, result);
    return {};
}

ErrorOr<void> ControlBusServer::handle_entities_select(Client& client, StringView id, u32 entity_id)
{
    auto* runtime = m_world_manager.find_runtime(m_world_id);
    if (!runtime)
        return Error::from_string_literal("World runtime is not available");

    bool selected = false;
    runtime->with_world_lock([&](World&) {
        runtime->bridge_backend().set_selected_entity(static_cast<EntityId>(entity_id));
        selected = true;
    });

    JsonObject result;
    result.set("entity_id"sv, entity_id);
    result.set("selected"sv, selected);
    send_response(client, id, true, result);
    return {};
}

ErrorOr<void> ControlBusServer::handle_contexts_list(Client& client, StringView id)
{
    auto* runtime = m_world_manager.find_runtime(m_world_id);
    if (!runtime)
        return Error::from_string_literal("World runtime is not available");

    JsonArray contexts;
    for (auto const& it : runtime->entity_script_contexts()) {
        JsonObject entry;
        entry.set("context_id"sv, it.key);
        entry.set("entity_id"sv, static_cast<u32>(it.value->entity_id()));
        entry.set("module_path"sv, it.value->component().module_path);
        entry.set("enabled"sv, it.value->component().enabled);
        if (it.value->component().entrypoint.has_value())
            entry.set("entrypoint"sv, it.value->component().entrypoint.value());
        (void)contexts.append(move(entry));
    }

    JsonObject result;
    result.set("contexts"sv, move(contexts));
    send_response(client, id, true, result);
    return {};
}

ErrorOr<void> ControlBusServer::handle_contexts_restart(Client& client, StringView id, JsonObject const&)
{
    JsonObject error;
    error.set("code"sv, "not_supported"sv);
    error.set("message"sv, "contexts.restart is not implemented yet"sv);
    send_response(client, id, false, {}, error);
    return {};
}

ErrorOr<void> ControlBusServer::handle_filesystem_list_dir(Client& client, StringView id, StringView path)
{
    auto* virtual_file_system = m_world_manager.virtual_file_system();
    if (!virtual_file_system)
        return Error::from_string_literal("Virtual filesystem is not available");

    JsonArray entries;
    if (path == "/"sv || path.is_empty()) {
        for (auto const& prefix : virtual_file_system->mount_prefixes()) {
            JsonObject entry;
            entry.set("name"sv, prefix);
            entry.set("path"sv, prefix);
            entry.set("type"sv, "directory"sv);
            (void)entries.append(move(entry));
        }
    } else {
        auto directory_entries = TRY(virtual_file_system->list_directory(path));
        for (auto const& entry : directory_entries) {
            JsonObject object;
            object.set("name"sv, MUST(String::from_byte_string(entry.name)));
            object.set("path"sv, MUST(String::from_byte_string(ByteString::formatted("{}{}", path, entry.name))));
            object.set("type"sv, entry.type == Core::DirectoryEntry::Type::Directory ? "directory"sv : "file"sv);
            (void)entries.append(move(object));
        }
    }

    JsonObject result;
    result.set("path"sv, path);
    result.set("entries"sv, move(entries));
    send_response(client, id, true, result);
    return {};
}

ErrorOr<void> ControlBusServer::handle_filesystem_read_text(Client& client, StringView id, StringView path)
{
    auto* virtual_file_system = m_world_manager.virtual_file_system();
    if (!virtual_file_system)
        return Error::from_string_literal("Virtual filesystem is not available");

    auto bytes = TRY(virtual_file_system->read_file(path));
    auto text = TRY(String::from_utf8(StringView { bytes }));

    JsonObject result;
    result.set("path"sv, path);
    result.set("text"sv, text);
    send_response(client, id, true, result);
    return {};
}

ErrorOr<void> ControlBusServer::handle_metrics_snapshot(Client& client, StringView id)
{
    auto* runtime = m_world_manager.find_runtime(m_world_id);
    if (!runtime)
        return Error::from_string_literal("World runtime is not available");

    size_t entity_count = 0;
    bool layout_dirty = false;
    bool transform_dirty = false;
    runtime->with_world_lock([&](World& world) {
        entity_count = world.alive_entity_count();
        layout_dirty = world.layout_dirty();
        transform_dirty = world.transform_dirty();
    });

    JsonObject result;
    result.set("entity_count"sv, static_cast<u32>(entity_count));
    result.set("context_count"sv, static_cast<u32>(runtime->entity_script_contexts().size()));
    result.set("layout_dirty"sv, layout_dirty);
    result.set("transform_dirty"sv, transform_dirty);
    result.set("faulted"sv, runtime->is_faulted());
    result.set("world_state"sv, world_state_name(runtime->world_state()));
    result.set("runtime_state"sv, runtime_state_name(runtime->state()));
    result.set("fault_reason"sv, runtime->fault_reason());
    send_response(client, id, true, result);
    return {};
}

ErrorOr<void> ControlBusServer::handle_events_subscribe(Client& client, StringView id, JsonObject const& object)
{
    JsonArray subscribed;

    if (auto params = object.get_object("params"sv); params.has_value()) {
        if (auto topics = params->get_array("topics"sv); topics.has_value()) {
            for (auto const& topic : topics->values()) {
                if (!topic.is_string())
                    continue;
                auto subscribed_topic = topic.as_string();
                client.subscribed_topics.set(subscribed_topic);
                (void)subscribed.append(topic);
            }
        }
    }

    JsonObject result;
    result.set("subscribed"sv, move(subscribed));
    send_response(client, id, true, result);
    return {};
}

void ControlBusServer::send_response(Client& client, StringView id, bool ok, JsonValue const& result, JsonValue const& error)
{
    JsonObject response;
    response.set("id"sv, id);
    response.set("type"sv, "response"sv);
    response.set("ok"sv, ok);
    if (ok)
        response.set("result"sv, result);
    else
        response.set("error"sv, error);

    auto text = response.serialized();
    if (auto result_or_error = send_ws_text_frame(*client.socket, text); result_or_error.is_error())
        warnln("ControlBus: send error: {}", result_or_error.error());
}

void ControlBusServer::send_event(StringView event, JsonValue const& data)
{
    JsonObject message;
    message.set("type"sv, "event"sv);
    message.set("event"sv, event);
    message.set("data"sv, data);
    auto text = message.serialized();

    for (auto& client : m_clients) {
        if (!client->handshaked)
            continue;
        if (!client->subscribed_topics.is_empty()) {
            bool matched = false;
            for (auto const& topic : client->subscribed_topics) {
                if (topic_matches(topic, event)) {
                    matched = true;
                    break;
                }
            }
            if (!matched)
                continue;
        }
        if (auto result = send_ws_text_frame(*client->socket, text); result.is_error())
            warnln("ControlBus: event send error: {}", result.error());
    }
}

}
