/*
 * MyceliumVR - a social VR platform built on the Ladybird browser engine
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include "../IWakeSignal.h"
#include "../ITransportStatsProvider.h"

#include <AK/Atomic.h>
#include <AK/NonnullOwnPtr.h>

namespace MyceliumVR::ProcessTransport {

class LinuxWakeSignal final
    : public IWakeSignal
    , public ITransportStatsProvider {
public:
    static ErrorOr<NonnullOwnPtr<LinuxWakeSignal>> create();
    static ErrorOr<NonnullOwnPtr<LinuxWakeSignal>> create_from_fd(int fd);

    virtual ~LinuxWakeSignal() override;

    virtual ErrorOr<void> signal() override;
    virtual ErrorOr<WakeWaitStatus> wait(u32 timeout_ms) override;
    virtual ErrorOr<void> drain() override;
    virtual TransportStatsSnapshot snapshot_stats() const override;

    ErrorOr<int> duplicate_fd() const;
    ErrorOr<int> release_fd();

private:
    explicit LinuxWakeSignal(int fd);

    int m_fd { -1 };
    Atomic<u64> m_signals_sent { 0 };
    Atomic<u64> m_wait_calls { 0 };
    Atomic<u64> m_wait_timeouts { 0 };
    Atomic<u64> m_drain_calls { 0 };
    Atomic<u64> m_close_calls { 0 };
    Atomic<u64> m_duplicate_fd_calls { 0 };
};

}
