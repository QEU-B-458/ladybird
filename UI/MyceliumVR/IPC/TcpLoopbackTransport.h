/*
 * MyceliumVR - a social VR platform built on the Ladybird browser engine
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include "IpcTransport.h"

#include <AK/IPv4Address.h>
#include <AK/OwnPtr.h>
#include <AK/RefPtr.h>
#include <AK/String.h>
#include <LibCore/Socket.h>
#include <LibCore/TCPServer.h>

namespace MyceliumVR::IPC {

struct TcpLoopbackEndpoint {
    IPv4Address address { 127, 0, 0, 1 };
    u16 port { 0 };
};

class TcpLoopbackConnection final : public IIpcConnection {
public:
    static ErrorOr<NonnullOwnPtr<TcpLoopbackConnection>> connect(TcpLoopbackEndpoint const&);
    static ErrorOr<NonnullOwnPtr<TcpLoopbackConnection>> create_from_accepted_socket(NonnullOwnPtr<Core::TCPSocket>, String description);

    virtual ~TcpLoopbackConnection() override = default;

    virtual ErrorOr<void> send_frame(Frame const&) override;
    virtual ErrorOr<Frame> receive_frame() override;
    virtual ErrorOr<bool> can_read_without_blocking() const override;
    virtual bool is_open() const override;
    virtual void close() override;
    virtual StringView description() const override { return m_description; }

private:
    TcpLoopbackConnection(NonnullOwnPtr<Core::BufferedTCPSocket>, String description);

    NonnullOwnPtr<Core::BufferedTCPSocket> m_socket;
    String m_description;
};

class TcpLoopbackListener final : public IIpcListener {
public:
    static ErrorOr<NonnullOwnPtr<TcpLoopbackListener>> listen(TcpLoopbackEndpoint const&);

    virtual ~TcpLoopbackListener() override = default;

    virtual ErrorOr<NonnullOwnPtr<IIpcConnection>> accept() override;
    virtual bool is_open() const override;
    virtual void close() override;
    virtual StringView description() const override { return m_description; }

    Optional<u16> local_port() const;

private:
    TcpLoopbackListener(RefPtr<Core::TCPServer>, String description);

    RefPtr<Core::TCPServer> m_server;
    String m_description;
};

}
