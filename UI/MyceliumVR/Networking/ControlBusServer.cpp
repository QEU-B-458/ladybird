/*
 * MyceliumVR - a social VR platform built on the Ladybird browser engine
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "ControlBusServer.h"
#include "../World/WorldManagementSystem.h"

#include <AK/Base64.h>
#include <AK/JsonObject.h>
#include <AK/JsonValue.h>
#include <AK/Random.h>
#include <AK/StringBuilder.h>
#include <LibCrypto/Hash/SHA1.h>

namespace MyceliumVR {

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

    // Listen on local loopback only, random port.
    TRY(m_server->listen(IPv4Address { 127, 0, 0, 1 }, 0));
    m_port = m_server->local_port().value();

    // Generate a random capability token.
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
        // Simple handshake parsing (hacky but enough for local MVP).
        u8 buffer[4096];
        auto bytes_read_or_error = client.socket->read_some({ buffer, sizeof(buffer) });
        if (bytes_read_or_error.is_error()) {
            m_clients.remove(client);
            return;
        }

        auto bytes = bytes_read_or_error.value();
        StringView request { (char const*)bytes.data(), bytes.size() };
        if (auto result = handle_handshake(client, request); result.is_error()) {
            m_clients.remove(client);
        }
        return;
    }

    // After handshake, we expect WebSocket frames.
    // For MVP, we'll just support tiny non-fragmented text frames.
    // TODO: Full WebSocket framing implementation.
    // For now, let's just close if it's too complex.
    m_clients.remove(client);
}

ErrorOr<void> ControlBusServer::handle_handshake(Client& client, StringView request)
{
    // Check for Sec-WebSocket-Key and our token (as a query param or header).
    // This is a very simplified WebSocket handshake.
    if (!request.contains("Upgrade: websocket"sv))
        return Error::from_string_literal("Not a WebSocket upgrade request");

    // Extract Sec-WebSocket-Key.
    auto key_start = request.find("Sec-WebSocket-Key: "sv);
    if (!key_start.has_value())
        return Error::from_string_literal("Missing Sec-WebSocket-Key");
    
    auto key_line = request.substring_view(*key_start + 19);
    auto key_end = key_line.find("\r\n"sv);
    if (!key_end.has_value())
        return Error::from_string_literal("Malformed Sec-WebSocket-Key");
    auto key = key_line.substring_view(0, *key_end);

    // Validate token (HACK: checking if it's in the GET line).
    if (!request.contains(m_capability_token))
        return Error::from_string_literal("Invalid capability token");

    // Compute Accept key.
    StringBuilder accept_base;
    accept_base.append(key);
    accept_base.append("258EAFA5-E914-47DA-95CA-C5AB0DC85B11"sv);
    
    auto sha1 = Crypto::Hash::SHA1::create();
    sha1->update(accept_base.string_view().bytes());
    auto digest = sha1->digest();
    auto accept_key = TRY(encode_base64(digest.bytes()));

    StringBuilder response;
    response.append("HTTP/1.1 101 Switching Protocols\r\n"sv);
    response.append("Upgrade: websocket\r\n"sv);
    response.append("Connection: Upgrade\r\n"sv);
    response.appendff("Sec-WebSocket-Accept: {}\r\n\r\n", accept_key);

    TRY(client.socket->write_until_depleted(response.string_view().bytes()));
    client.handshaked = true;
    client.granted_capabilities.append("admin"_string); // Default for trusted panels for now.

    outln("ControlBus: Client connected and handshaked.");
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

    // TODO: Send as WebSocket text frame.
    (void)client;
}

void ControlBusServer::send_event(StringView event, JsonValue const& data)
{
    JsonObject msg;
    msg.set("type"sv, "event"sv);
    msg.set("event"sv, event);
    msg.set("data"sv, data);

    // TODO: Broadcast to all handshaked clients.
}

}
