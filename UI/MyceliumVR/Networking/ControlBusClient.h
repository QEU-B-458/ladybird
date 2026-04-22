/*
 * MyceliumVR - a social VR platform built on the Ladybird browser engine
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <AK/ByteString.h>
#include <AK/Error.h>
#include <AK/Function.h>
#include <AK/JsonObject.h>
#include <AK/JsonValue.h>
#include <AK/String.h>
#include <LibCore/Socket.h>

namespace MyceliumVR {

class ControlBusClient {
public:
    ControlBusClient();
    ~ControlBusClient();

    ErrorOr<void> connect(u16 port, StringView token);
    void disconnect();

    ErrorOr<void> send_request(StringView method, JsonObject const& params = {});
    void set_message_callback(Function<void(JsonObject const&)> callback) { m_message_callback = move(callback); }

    void poll();

    bool is_connected() const { return m_handshaked; }

private:
    ErrorOr<void> handle_handshake_response();
    ErrorOr<void> on_data();

    OwnPtr<Core::BufferedTCPSocket> m_socket;
    bool m_connected { false };
    bool m_handshaked { false };
    String m_token;
    String m_expected_accept_key;

    Function<void(JsonObject const&)> m_message_callback;
};

}
