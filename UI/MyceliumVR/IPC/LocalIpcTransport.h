/*
 * MyceliumVR - a social VR platform built on the Ladybird browser engine
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include "IpcTransport.h"

#include <AK/OwnPtr.h>
#include <AK/String.h>
#include <LibCore/Socket.h>
#include <LibCore/LocalServer.h>

namespace MyceliumVR::IPC {

// Platform-agnostic local IPC connection using Unix Sockets (POSIX) or Named Pipes (Windows).
class LocalIpcConnection final : public IIpcConnection {
public:
    static ErrorOr<NonnullOwnPtr<LocalIpcConnection>> connect(StringView address);
    static ErrorOr<NonnullOwnPtr<LocalIpcConnection>> create_from_accepted_socket(NonnullOwnPtr<Core::LocalSocket>, String description);

    virtual ~LocalIpcConnection() override = default;

    virtual ErrorOr<void> send_frame(Frame const&) override;
    virtual ErrorOr<Frame> receive_frame() override;
    virtual ErrorOr<bool> can_read_without_blocking() const override;
    virtual bool is_open() const override;
    virtual void close() override;
    virtual StringView description() const override { return m_description; }

private:
    LocalIpcConnection(NonnullOwnPtr<Core::BufferedLocalSocket>, String description);

    NonnullOwnPtr<Core::BufferedLocalSocket> m_socket;
    String m_description;
};

class LocalIpcListener final : public IIpcListener {
public:
    static ErrorOr<NonnullOwnPtr<LocalIpcListener>> listen(StringView address);

    virtual ~LocalIpcListener() override = default;

    virtual ErrorOr<NonnullOwnPtr<IIpcConnection>> accept() override;
    
    // Low-level accept for LibIPC integration.
    ErrorOr<NonnullOwnPtr<Core::LocalSocket>> accept_socket();

    virtual bool is_open() const override;
    virtual void close() override;
    virtual StringView description() const override { return m_description; }

private:
    LocalIpcListener(RefPtr<Core::LocalServer>, String description);

    RefPtr<Core::LocalServer> m_server;
    String m_description;
};

}
