/*
 * MyceliumVR - a social VR platform built on the Ladybird browser engine
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include "../IWorldBootstrapChannel.h"
#include "../ITransportStatsProvider.h"

#include <AK/Atomic.h>
#include <AK/NonnullOwnPtr.h>
#include <AK/OwnPtr.h>
#include <AK/Vector.h>

namespace Core {
class LocalSocket;
}

namespace MyceliumVR::ProcessTransport {

class LinuxBootstrapChannel final
    : public IWorldBootstrapChannel
    , public ITransportStatsProvider {
public:
    struct MessageWithFileDescriptors {
        BootstrapMessage message;
        Vector<int, 1> fds;
    };

    static ErrorOr<NonnullOwnPtr<LinuxBootstrapChannel>> create_from_fd(int fd);

    virtual ~LinuxBootstrapChannel() override;

    virtual ErrorOr<void> send(BootstrapMessage const&) override;
    ErrorOr<void> send_with_fds(BootstrapMessage const&, Vector<int, 1> const&);
    virtual ErrorOr<Optional<BootstrapMessage>> receive() override;
    ErrorOr<Optional<MessageWithFileDescriptors>> receive_with_fds();
    ErrorOr<BootstrapMessage> receive_blocking();
    ErrorOr<MessageWithFileDescriptors> receive_blocking_with_fds();
    virtual void close() override;
    virtual TransportStatsSnapshot snapshot_stats() const override;

    ErrorOr<int> release_fd();

private:
    explicit LinuxBootstrapChannel(NonnullOwnPtr<Core::LocalSocket>);

    NonnullOwnPtr<Core::LocalSocket> m_socket;
    Atomic<u64> m_messages_sent { 0 };
    Atomic<u64> m_messages_received { 0 };
    Atomic<u64> m_bytes_sent { 0 };
    Atomic<u64> m_bytes_received { 0 };
    Optional<BootstrapMessage> m_pending_fd_message;
    Atomic<u64> m_close_calls { 0 };
    Atomic<u64> m_duplicate_fd_calls { 0 };
};

}
