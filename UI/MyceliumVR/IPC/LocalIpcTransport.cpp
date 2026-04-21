/*
 * MyceliumVR - a social VR platform built on the Ladybird browser engine
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "LocalIpcTransport.h"
#include <LibCore/LocalServer.h>

namespace MyceliumVR::IPC {

ErrorOr<NonnullOwnPtr<LocalIpcConnection>> LocalIpcConnection::connect(StringView address)
{
    auto socket = TRY(Core::LocalSocket::connect(address));
    auto buffered_socket = TRY(Core::BufferedLocalSocket::create(move(socket)));
    auto description = TRY(String::formatted("local://{}", address));
    return adopt_nonnull_own_or_enomem(new (nothrow) LocalIpcConnection(move(buffered_socket), move(description)));
}

ErrorOr<NonnullOwnPtr<LocalIpcConnection>> LocalIpcConnection::create_from_accepted_socket(NonnullOwnPtr<Core::LocalSocket> socket, String description)
{
    auto buffered_socket = TRY(Core::BufferedLocalSocket::create(move(socket)));
    return adopt_nonnull_own_or_enomem(new (nothrow) LocalIpcConnection(move(buffered_socket), move(description)));
}

LocalIpcConnection::LocalIpcConnection(NonnullOwnPtr<Core::BufferedLocalSocket> socket, String description)
    : m_socket(move(socket))
    , m_description(move(description))
{
}

ErrorOr<void> LocalIpcConnection::send_frame(Frame const& frame)
{
    return write_frame(*m_socket, frame);
}

ErrorOr<Frame> LocalIpcConnection::receive_frame()
{
    return read_frame(*m_socket);
}

ErrorOr<bool> LocalIpcConnection::can_read_without_blocking() const
{
    return m_socket->can_read_without_blocking();
}

bool LocalIpcConnection::is_open() const
{
    return m_socket->is_open();
}

void LocalIpcConnection::close()
{
    m_socket->close();
}

ErrorOr<NonnullOwnPtr<LocalIpcListener>> LocalIpcListener::listen(StringView address)
{
    auto server = Core::LocalServer::construct();
    if (!server->listen(address))
        return Error::from_string_literal("LocalIpcListener: failed to listen on address");
    auto description = TRY(String::formatted("local://{}", address));
    return adopt_nonnull_own_or_enomem(new (nothrow) LocalIpcListener(move(server), move(description)));
}

LocalIpcListener::LocalIpcListener(RefPtr<Core::LocalServer> server, String description)
    : m_server(move(server))
    , m_description(move(description))
{
}

ErrorOr<NonnullOwnPtr<IIpcConnection>> LocalIpcListener::accept()
{
    auto socket = TRY(accept_socket());
    auto description = TRY(String::formatted("{}#accepted", m_description));
    NonnullOwnPtr<IIpcConnection> connection = TRY(LocalIpcConnection::create_from_accepted_socket(move(socket), move(description)));
    return connection;
}

ErrorOr<NonnullOwnPtr<Core::LocalSocket>> LocalIpcListener::accept_socket()
{
    if (!m_server)
        return Error::from_string_literal("Local IPC listener is closed");
    return m_server->accept();
}

bool LocalIpcListener::is_open() const
{
    return m_server && m_server->is_listening();
}

void LocalIpcListener::close()
{
    m_server = nullptr;
}

}
