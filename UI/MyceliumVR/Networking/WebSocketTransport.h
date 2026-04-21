/*
 * MyceliumVR - a social VR platform built on the Ladybird browser engine
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include "NetworkService.h"

#include <AK/HashMap.h>
#include <AK/OwnPtr.h>
#include <AK/RefPtr.h>
#include <LibWebSocket/WebSocket.h>

namespace MyceliumVR {

class WebSocketTransport final : public ITransport {
public:
    explicit WebSocketTransport(u32 id);
    virtual ~WebSocketTransport() override;

    virtual ErrorOr<void> connect(StringView address) override;
    virtual ErrorOr<size_t> send(ReadonlyBytes payload, u32 flags) override;
    virtual Vector<NetworkEvent> poll_events() override;
    virtual void close() override;

private:
    u32 m_id { 0 };
    RefPtr<WebSocket::WebSocket> m_socket;
    Vector<NetworkEvent> m_pending_events;
};

}
