/*
 * MyceliumVR - a social VR platform built on the Ladybird browser engine
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "NetworkService.h"
#include "WebSocketTransport.h"

namespace MyceliumVR {

NetworkService::NetworkService() = default;
NetworkService::~NetworkService() = default;

ErrorOr<u32> NetworkService::connect(StringView kind, StringView address)
{
    if (m_connections.size() >= m_limits.max_connections)
        return Error::from_string_literal("NetworkService: max connections reached");

    u32 id = allocate_connection_id();
    OwnPtr<ITransport> transport;

    if (kind == "websocket"sv) {
        transport = make<WebSocketTransport>(id);
    } else {
        return Error::from_string_literal("NetworkService: transport kind not supported");
    }

    TRY(transport->connect(address));
    m_connections.set(id, transport.release_nonnull());
    return id;
}

ErrorOr<void> NetworkService::send(u32 connection_id, ReadonlyBytes payload, u32 flags)
{
    if (payload.size() > m_limits.max_message_size)
        return Error::from_string_literal("NetworkService: message size exceeds limit");

    auto it = m_connections.find(connection_id);
    if (it == m_connections.end())
        return Error::from_string_literal("NetworkService: invalid connection id");

    TRY(it->value->send(payload, flags));
    return {};
}

void NetworkService::close(u32 connection_id)
{
    auto it = m_connections.find(connection_id);
    if (it == m_connections.end())
        return;

    it->value->close();
    m_connections.remove(it);
}

Vector<NetworkEvent> NetworkService::poll_events()
{
    Vector<NetworkEvent> all_events;
    for (auto& entry : m_connections) {
        auto events = entry.value->poll_events();
        all_events.extend(move(events));
    }
    return all_events;
}

u32 NetworkService::allocate_connection_id()
{
    return m_next_connection_id++;
}

}
