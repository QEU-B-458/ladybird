/*
 * MyceliumVR - a social VR platform built on the Ladybird browser engine
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "TcpLoopbackTransport.h"

namespace MyceliumVR::IPC {

ErrorOr<NonnullOwnPtr<TcpLoopbackConnection>> TcpLoopbackConnection::connect(TcpLoopbackEndpoint const& endpoint)
{
    auto socket = TRY(Core::TCPSocket::connect(endpoint.address.to_byte_string(), endpoint.port));
    auto buffered_socket = TRY(Core::BufferedTCPSocket::create(move(socket)));
    auto description = TRY(String::formatted("tcp://{}:{}", endpoint.address.to_byte_string(), endpoint.port));
    return adopt_nonnull_own_or_enomem(new (nothrow) TcpLoopbackConnection(move(buffered_socket), move(description)));
}

ErrorOr<NonnullOwnPtr<TcpLoopbackConnection>> TcpLoopbackConnection::create_from_accepted_socket(NonnullOwnPtr<Core::TCPSocket> socket, String description)
{
    auto buffered_socket = TRY(Core::BufferedTCPSocket::create(move(socket)));
    return adopt_nonnull_own_or_enomem(new (nothrow) TcpLoopbackConnection(move(buffered_socket), move(description)));
}

TcpLoopbackConnection::TcpLoopbackConnection(NonnullOwnPtr<Core::BufferedTCPSocket> socket, String description)
    : m_socket(move(socket))
    , m_description(move(description))
{
}

ErrorOr<void> TcpLoopbackConnection::send_frame(Frame const& frame)
{
    return write_frame(*m_socket, frame);
}

ErrorOr<Frame> TcpLoopbackConnection::receive_frame()
{
    return read_frame(*m_socket);
}

ErrorOr<bool> TcpLoopbackConnection::can_read_without_blocking() const
{
    return m_socket->can_read_without_blocking();
}

bool TcpLoopbackConnection::is_open() const
{
    return m_socket->is_open();
}

void TcpLoopbackConnection::close()
{
    m_socket->close();
}

ErrorOr<NonnullOwnPtr<TcpLoopbackListener>> TcpLoopbackListener::listen(TcpLoopbackEndpoint const& endpoint)
{
    auto server = TRY(Core::TCPServer::try_create());
    TRY(server->listen(endpoint.address, endpoint.port, Core::TCPServer::AllowAddressReuse::Yes));
    auto description = TRY(String::formatted("tcp://{}:{}", endpoint.address.to_byte_string(), endpoint.port));
    return adopt_nonnull_own_or_enomem(new (nothrow) TcpLoopbackListener(move(server), move(description)));
}

TcpLoopbackListener::TcpLoopbackListener(RefPtr<Core::TCPServer> server, String description)
    : m_server(move(server))
    , m_description(move(description))
{
}

ErrorOr<NonnullOwnPtr<IIpcConnection>> TcpLoopbackListener::accept()
{
    if (!m_server)
        return Error::from_string_literal("TCP loopback listener is closed");

    auto socket = TRY(m_server->accept());
    auto description = TRY(String::formatted("{}#accepted", m_description));
    NonnullOwnPtr<IIpcConnection> connection = TRY(TcpLoopbackConnection::create_from_accepted_socket(move(socket), move(description)));
    return connection;
}

bool TcpLoopbackListener::is_open() const
{
    return m_server && m_server->is_listening();
}

void TcpLoopbackListener::close()
{
    m_server = nullptr;
}

Optional<u16> TcpLoopbackListener::local_port() const
{
    if (!m_server)
        return {};
    return m_server->local_port();
}

}
