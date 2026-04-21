/*
 * MyceliumVR - a social VR platform built on the Ladybird browser engine
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "WebSocketTransport.h"

#include <LibURL/Parser.h>
#include <LibURL/URL.h>

namespace MyceliumVR {

WebSocketTransport::WebSocketTransport(u32 id)
    : m_id(id)
{
}

WebSocketTransport::~WebSocketTransport() = default;

ErrorOr<void> WebSocketTransport::connect(StringView address)
{
    auto url = URL::Parser::basic_parse(address);
    if (!url.has_value())
        return Error::from_string_literal("WebSocketTransport: invalid URL");

    WebSocket::ConnectionInfo info(url.value());
    m_socket = WebSocket::WebSocket::create(move(info));
    
    m_socket->on_open = [this] {
        m_pending_events.append({ NetworkEvent::Type::Connected, m_id, {}, {} });
    };

    m_socket->on_message = [this](WebSocket::Message message) {
        m_pending_events.append({ NetworkEvent::Type::Data, m_id, ByteBuffer::copy(message.data()).release_value_but_fixme_should_propagate_errors(), {} });
    };

    m_socket->on_close = [this](u16 code, ByteString reason, bool clean) {
        (void)code;
        (void)reason;
        (void)clean;
        m_pending_events.append({ NetworkEvent::Type::Disconnected, m_id, {}, {} });
    };

    m_socket->on_error = [this](WebSocket::WebSocket::Error error) {
        m_pending_events.append({ NetworkEvent::Type::Error, m_id, {}, String::formatted("WebSocket error: {}", (int)error).release_value_but_fixme_should_propagate_errors() });
    };

    m_socket->start();
    return {};
}

ErrorOr<size_t> WebSocketTransport::send(ReadonlyBytes payload, u32)
{
    if (!m_socket || m_socket->ready_state() != WebSocket::ReadyState::Open)
        return Error::from_string_literal("WebSocketTransport: socket not open");

    m_socket->send(WebSocket::Message(ByteBuffer::copy(payload).release_value_but_fixme_should_propagate_errors(), false));
    return payload.size();
}

Vector<NetworkEvent> WebSocketTransport::poll_events()
{
    return move(m_pending_events);
}

void WebSocketTransport::close()
{
    if (m_socket)
        m_socket->close();
}

}
