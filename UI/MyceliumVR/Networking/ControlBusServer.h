/*
 * MyceliumVR - a social VR platform built on the Ladybird browser engine
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <AK/ByteString.h>
#include <AK/Error.h>
#include <AK/Function.h>
#include <AK/HashMap.h>
#include <AK/JsonObject.h>
#include <AK/JsonValue.h>
#include <AK/RefCounted.h>
#include <AK/String.h>
#include <AK/Vector.h>
#include <LibCore/Socket.h>
#include <LibCore/TCPServer.h>

namespace MyceliumVR {

class WorldManagementSystem;

class ControlBusServer {
public:
    explicit ControlBusServer(WorldManagementSystem&);
    ~ControlBusServer();

    ErrorOr<void> start(u32 world_id);
    void stop();

    u16 port() const { return m_port; }
    String const& capability_token() const { return m_capability_token; }

private:
    struct Client : public RefCounted<Client> {
        NonnullOwnPtr<Core::BufferedTCPSocket> socket;
        bool handshaked { false };
        Vector<String> granted_capabilities;

        explicit Client(NonnullOwnPtr<Core::BufferedTCPSocket> s) : socket(move(s)) {}
    };

    void on_ready_to_accept();
    void on_client_data(Client&);
    ErrorOr<void> handle_handshake(Client&, StringView request);
    ErrorOr<void> handle_message(Client&, StringView message);
    void send_response(Client&, StringView id, bool ok, JsonValue const& result = JsonValue(), JsonValue const& error = JsonValue());
    void send_event(StringView event, JsonValue const& data = JsonValue());

    WorldManagementSystem& m_world_manager;
    u32 m_world_id { 0 };
    u16 m_port { 0 };
    String m_capability_token;
    RefPtr<Core::TCPServer> m_server;
    HashTable<NonnullRefPtr<Client>> m_clients;
};

}
