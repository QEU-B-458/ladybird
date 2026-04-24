/*
 * MyceliumVR - a social VR platform built on the Ladybird browser engine
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "LinuxBootstrapChannel.h"

#include "LinuxSyscalls.h"

#include <AK/ByteBuffer.h>
#include <AK/Stream.h>
#include <LibCore/Socket.h>

namespace MyceliumVR::ProcessTransport {

struct BootstrapWireHeader {
    u32 kind { 0 };
    u32 payload_size { 0 };
};

ErrorOr<NonnullOwnPtr<LinuxBootstrapChannel>> LinuxBootstrapChannel::create_from_fd(int fd)
{
    auto socket = TRY(Core::LocalSocket::adopt_fd(fd));
    return adopt_nonnull_own_or_enomem(new (nothrow) LinuxBootstrapChannel(move(socket)));
}

LinuxBootstrapChannel::LinuxBootstrapChannel(NonnullOwnPtr<Core::LocalSocket> socket)
    : m_socket(move(socket))
{
}

LinuxBootstrapChannel::~LinuxBootstrapChannel()
{
    close();
}

ErrorOr<void> LinuxBootstrapChannel::send(BootstrapMessage const& message)
{
    BootstrapWireHeader header {
        .kind = static_cast<u32>(message.kind),
        .payload_size = static_cast<u32>(message.payload.size()),
    };
    TRY(m_socket->write_until_depleted({ &header, sizeof(header) }));
    if (!message.payload.is_empty())
        TRY(m_socket->write_until_depleted(message.payload.bytes()));
    m_messages_sent.fetch_add(1, AK::MemoryOrder::memory_order_relaxed);
    m_bytes_sent.fetch_add(sizeof(header) + message.payload.size(), AK::MemoryOrder::memory_order_relaxed);
    return {};
}

ErrorOr<void> LinuxBootstrapChannel::send_with_fds(BootstrapMessage const& message, Vector<int, 1> const& fds)
{
    TRY(send(message));
    for (auto fd : fds)
        TRY(m_socket->send_fd(fd));
    return {};
}

ErrorOr<Optional<BootstrapMessage>> LinuxBootstrapChannel::receive()
{
    auto can_read = TRY(m_socket->can_read_without_blocking());
    if (!can_read)
        return Optional<BootstrapMessage> {};

    BootstrapWireHeader header {};
    TRY(m_socket->read_until_filled({ &header, sizeof(header) }));

    BootstrapMessage message;
    message.kind = static_cast<BootstrapMessageKind>(header.kind);
    if (header.payload_size > 0) {
        message.payload = TRY(ByteBuffer::create_uninitialized(header.payload_size));
        TRY(m_socket->read_until_filled(message.payload.bytes()));
    }
    m_messages_received.fetch_add(1, AK::MemoryOrder::memory_order_relaxed);
    m_bytes_received.fetch_add(sizeof(header) + header.payload_size, AK::MemoryOrder::memory_order_relaxed);
    return message;
}

ErrorOr<Optional<LinuxBootstrapChannel::MessageWithFileDescriptors>> LinuxBootstrapChannel::receive_with_fds()
{
    if (!m_pending_fd_message.has_value()) {
        auto message = TRY(receive());
        if (!message.has_value())
            return Optional<MessageWithFileDescriptors> {};
        m_pending_fd_message = move(message);
    }

    auto can_read = TRY(m_socket->can_read_without_blocking());
    if (!can_read)
        return Optional<MessageWithFileDescriptors> {};

    MessageWithFileDescriptors message_with_fds;
    message_with_fds.message = move(*m_pending_fd_message);
    m_pending_fd_message.clear();
    message_with_fds.fds.append(TRY(m_socket->receive_fd(0)));
    return message_with_fds;
}

ErrorOr<BootstrapMessage> LinuxBootstrapChannel::receive_blocking()
{
    BootstrapWireHeader header {};
    TRY(m_socket->read_until_filled({ &header, sizeof(header) }));

    BootstrapMessage message;
    message.kind = static_cast<BootstrapMessageKind>(header.kind);
    if (header.payload_size > 0) {
        message.payload = TRY(ByteBuffer::create_uninitialized(header.payload_size));
        TRY(m_socket->read_until_filled(message.payload.bytes()));
    }
    m_messages_received.fetch_add(1, AK::MemoryOrder::memory_order_relaxed);
    m_bytes_received.fetch_add(sizeof(header) + header.payload_size, AK::MemoryOrder::memory_order_relaxed);
    return message;
}

ErrorOr<LinuxBootstrapChannel::MessageWithFileDescriptors> LinuxBootstrapChannel::receive_blocking_with_fds()
{
    MessageWithFileDescriptors message_with_fds;
    if (m_pending_fd_message.has_value()) {
        message_with_fds.message = move(*m_pending_fd_message);
        m_pending_fd_message.clear();
    } else {
        message_with_fds.message = TRY(receive_blocking());
    }
    message_with_fds.fds.append(TRY(m_socket->receive_fd(0)));
    return message_with_fds;
}

void LinuxBootstrapChannel::close()
{
    if (m_socket->is_open()) {
        m_close_calls.fetch_add(1, AK::MemoryOrder::memory_order_relaxed);
        m_socket->close();
    }
}

ErrorOr<int> LinuxBootstrapChannel::release_fd()
{
    m_duplicate_fd_calls.fetch_add(1, AK::MemoryOrder::memory_order_relaxed);
    return m_socket->release_fd();
}

TransportStatsSnapshot LinuxBootstrapChannel::snapshot_stats() const
{
    TransportStatsSnapshot snapshot;
    snapshot.component_name = "linux.bootstrap"sv.to_byte_string();
    snapshot.messages_sent = m_messages_sent.load(AK::MemoryOrder::memory_order_relaxed);
    snapshot.messages_received = m_messages_received.load(AK::MemoryOrder::memory_order_relaxed);
    snapshot.bytes_sent = m_bytes_sent.load(AK::MemoryOrder::memory_order_relaxed);
    snapshot.bytes_received = m_bytes_received.load(AK::MemoryOrder::memory_order_relaxed);
    snapshot.duplicate_fd_calls = m_duplicate_fd_calls.load(AK::MemoryOrder::memory_order_relaxed);
    snapshot.close_calls = m_close_calls.load(AK::MemoryOrder::memory_order_relaxed);
    return snapshot;
}

}
