/*
 * MyceliumVR - a social VR platform built on the Ladybird browser engine
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "ControlBusClient.h"

#include <AK/Base64.h>
#include <AK/Random.h>
#include <AK/StringBuilder.h>
#include <LibCrypto/Hash/SHA1.h>

namespace MyceliumVR {

namespace {

static ErrorOr<void> send_ws_text_frame(Core::BufferedTCPSocket& socket, StringView payload)
{
    // RFC 6455 text frame, client→server (MUST be masked)
    u8 header[14]; // 2 (basic) + 8 (extended length) + 4 (mask)
    size_t header_length = 0;
    header[header_length++] = 0x81; // FIN + Opcode Text

    auto payload_length = payload.length();
    if (payload_length < 126) {
        header[header_length++] = static_cast<u8>(payload_length | 0x80); // Masked
    } else if (payload_length < 65536) {
        header[header_length++] = 126 | 0x80;
        header[header_length++] = static_cast<u8>((payload_length >> 8) & 0xff);
        header[header_length++] = static_cast<u8>(payload_length & 0xff);
    } else {
        header[header_length++] = 127 | 0x80;
        for (int shift = 7; shift >= 0; --shift)
            header[header_length++] = static_cast<u8>((payload_length >> (shift * 8)) & 0xff);
    }

    u8 mask[4];
    fill_with_random({ mask, sizeof(mask) });
    for (int i = 0; i < 4; ++i)
        header[header_length++] = mask[i];

    TRY(socket.write_until_depleted({ header, header_length }));

    auto masked_payload = TRY(ByteBuffer::create_uninitialized(payload_length));
    for (size_t i = 0; i < payload_length; ++i)
        masked_payload[i] = static_cast<u8>(payload[i] ^ mask[i % 4]);

    TRY(socket.write_until_depleted(masked_payload));
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
    // Server to client frames are NOT masked.
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

}

ControlBusClient::ControlBusClient() = default;
ControlBusClient::~ControlBusClient() = default;

ErrorOr<void> ControlBusClient::connect(u16 port, StringView token)
{
    m_token = TRY(String::from_utf8(token));
    auto socket = TRY(Core::TCPSocket::connect("127.0.0.1", port));
    m_socket = TRY(Core::BufferedTCPSocket::create(move(socket)));
    m_connected = true;

    u8 raw_key[16];
    fill_with_random({ raw_key, sizeof(raw_key) });
    auto key = TRY(encode_base64({ raw_key, sizeof(raw_key) }));

    StringBuilder handshake;
    handshake.appendff("GET /?token={} HTTP/1.1\r\n", m_token);
    handshake.append("Host: 127.0.0.1\r\n"sv);
    handshake.append("Upgrade: websocket\r\n"sv);
    handshake.append("Connection: Upgrade\r\n"sv);
    handshake.appendff("Sec-WebSocket-Key: {}\r\n", key);
    handshake.append("Sec-WebSocket-Version: 13\r\n\r\n"sv);

    TRY(m_socket->write_until_depleted(handshake.string_view().bytes()));

    StringBuilder accept_base;
    accept_base.append(key);
    accept_base.append("258EAFA5-E914-47DA-95CA-C5AB0DC85B11"sv);
    auto sha1 = Crypto::Hash::SHA1::create();
    sha1->update(accept_base.string_view().bytes());
    m_expected_accept_key = TRY(encode_base64(sha1->digest().bytes()));

    return {};
}

void ControlBusClient::disconnect()
{
    m_socket = nullptr;
    m_connected = false;
    m_handshaked = false;
}

ErrorOr<void> ControlBusClient::send_request(StringView method, JsonObject const& params)
{
    if (!m_handshaked)
        return Error::from_string_literal("ControlBusClient not handshaked");

    JsonObject request;
    request.set("type"sv, "request"sv);
    request.set("method"sv, method);
    request.set("params"sv, params);
    request.set("id"sv, MUST(String::from_byte_string(ByteString::formatted("{}", get_random<u32>()))));

    return send_ws_text_frame(*m_socket, request.serialized());
}

void ControlBusClient::poll()
{
    if (!m_connected || !m_socket)
        return;

    auto can_read_or_error = m_socket->can_read_without_blocking();
    if (can_read_or_error.is_error() || !can_read_or_error.value())
        return;

    auto result = on_data();
    if (result.is_error()) {
        warnln("ControlBusClient: Error on data: {}", result.error());
        disconnect();
    }
}

ErrorOr<void> ControlBusClient::on_data()
{
    if (!m_handshaked)
        return handle_handshake_response();

    auto frame = TRY(read_ws_frame(*m_socket));
    if (frame.opcode == 0x8) {
        disconnect();
        return {};
    }

    if (frame.opcode == 0x1) {
        auto json_text = StringView { frame.payload };
        auto json_or_error = JsonValue::from_string(json_text);
        if (json_or_error.is_error())
            return Error::from_string_literal("ControlBusClient: Received invalid JSON");

        auto json = json_or_error.release_value();
        if (!json.is_object())
            return Error::from_string_literal("ControlBusClient: Received non-object JSON");

        if (m_message_callback)
            m_message_callback(json.as_object());
    }

    return {};
}

ErrorOr<void> ControlBusClient::handle_handshake_response()
{
    u8 buffer[4096];
    auto line = TRY(m_socket->read_line(buffer));
    if (!line.contains("101 Switching Protocols"sv))
        return Error::from_string_literal("ControlBusClient: Handshake failed");

    bool has_upgrade = false;
    bool has_connection = false;
    bool has_accept = false;

    while (true) {
        auto header_line = TRY(m_socket->read_line(buffer));
        if (header_line.is_empty() || header_line == "\r"sv)
            break;

        if (header_line.starts_with("Upgrade: websocket"sv, CaseSensitivity::CaseInsensitive))
            has_upgrade = true;
        else if (header_line.starts_with("Connection: Upgrade"sv, CaseSensitivity::CaseInsensitive))
            has_connection = true;
        else if (header_line.starts_with("Sec-WebSocket-Accept: "sv, CaseSensitivity::CaseInsensitive)) {
            auto accept = header_line.substring_view(22).trim_whitespace();
            if (accept == m_expected_accept_key)
                has_accept = true;
        }
    }

    if (!has_upgrade || !has_connection || !has_accept)
        return Error::from_string_literal("ControlBusClient: WebSocket handshake missing headers or invalid accept key");

    m_handshaked = true;
    outln("ControlBusClient: Handshake successful.");
    return {};
}

}
