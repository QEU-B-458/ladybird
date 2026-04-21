/*
 * MyceliumVR - a social VR platform built on the Ladybird browser engine
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <AK/ByteBuffer.h>
#include <AK/Error.h>
#include <AK/HashMap.h>
#include <AK/NonnullOwnPtr.h>
#include <AK/String.h>
#include <AK/Vector.h>

namespace MyceliumVR {

struct NetworkEvent {
    enum class Type {
        Connected,
        Disconnected,
        Data,
        Error,
    } type;
    u32 connection_id { 0 };
    ByteBuffer payload;
    String error_message;
};

class ITransport {
public:
    virtual ~ITransport() = default;

    virtual ErrorOr<void> connect(StringView address) = 0;
    virtual ErrorOr<size_t> send(ReadonlyBytes payload, u32 flags) = 0;
    virtual Vector<NetworkEvent> poll_events() = 0;
    virtual void close() = 0;
};

class NetworkService {
public:
    NetworkService();
    ~NetworkService();

    struct Limits {
        u32 max_connections { 16 };
        u32 max_peers_per_world { 32 };
        size_t max_message_size { 256 * 1024 };
    };

    void set_limits(Limits const& limits) { m_limits = limits; }
    Limits const& limits() const { return m_limits; }

    ErrorOr<u32> connect(StringView kind, StringView address);
    ErrorOr<void> send(u32 connection_id, ReadonlyBytes payload, u32 flags = 0);
    void close(u32 connection_id);

    Vector<NetworkEvent> poll_events();

private:
    u32 allocate_connection_id();

    Limits m_limits;
    u32 m_next_connection_id { 1 };
    HashMap<u32, NonnullOwnPtr<ITransport>> m_connections;
};

}
