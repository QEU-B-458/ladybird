/*
 * MyceliumVR - a social VR platform built on the Ladybird browser engine
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "LinuxWakeSignal.h"

#include "LinuxSyscalls.h"

namespace MyceliumVR::ProcessTransport {

ErrorOr<NonnullOwnPtr<LinuxWakeSignal>> LinuxWakeSignal::create()
{
#ifdef AK_OS_WINDOWS
    return Error::from_string_literal("LinuxWakeSignal is not available on Windows");
#else
    int fd = TRY(LinuxSyscalls::create_event_fd());
    return adopt_nonnull_own_or_enomem(new (nothrow) LinuxWakeSignal(fd));
#endif
}

ErrorOr<NonnullOwnPtr<LinuxWakeSignal>> LinuxWakeSignal::create_from_fd(int fd)
{
#ifdef AK_OS_WINDOWS
    (void)fd;
    return Error::from_string_literal("LinuxWakeSignal is not available on Windows");
#else
    TRY(LinuxSyscalls::set_fd_nonblocking(fd));
    return adopt_nonnull_own_or_enomem(new (nothrow) LinuxWakeSignal(fd));
#endif
}

LinuxWakeSignal::LinuxWakeSignal(int fd)
    : m_fd(fd)
{
}

LinuxWakeSignal::~LinuxWakeSignal()
{
    if (m_fd >= 0)
        MUST(LinuxSyscalls::close_fd(m_fd));
}

ErrorOr<void> LinuxWakeSignal::signal()
{
#ifdef AK_OS_WINDOWS
    return Error::from_string_literal("LinuxWakeSignal is not available on Windows");
#else
    TRY(LinuxSyscalls::write_event_fd(m_fd));
    m_signals_sent.fetch_add(1, AK::MemoryOrder::memory_order_relaxed);
    return {};
#endif
}

ErrorOr<WakeWaitStatus> LinuxWakeSignal::wait(u32 timeout_ms)
{
#ifdef AK_OS_WINDOWS
    (void)timeout_ms;
    return Error::from_string_literal("LinuxWakeSignal is not available on Windows");
#else
    m_wait_calls.fetch_add(1, AK::MemoryOrder::memory_order_relaxed);
    auto readable = TRY(LinuxSyscalls::poll_readable(m_fd, timeout_ms));
    if (!readable) {
        m_wait_timeouts.fetch_add(1, AK::MemoryOrder::memory_order_relaxed);
        return WakeWaitStatus::Timeout;
    }
    return WakeWaitStatus::Signaled;
#endif
}

ErrorOr<void> LinuxWakeSignal::drain()
{
#ifdef AK_OS_WINDOWS
    return Error::from_string_literal("LinuxWakeSignal is not available on Windows");
#else
    m_drain_calls.fetch_add(1, AK::MemoryOrder::memory_order_relaxed);
    return LinuxSyscalls::drain_event_fd(m_fd);
#endif
}

ErrorOr<int> LinuxWakeSignal::duplicate_fd() const
{
    const_cast<LinuxWakeSignal*>(this)->m_duplicate_fd_calls.fetch_add(1, AK::MemoryOrder::memory_order_relaxed);
    return LinuxSyscalls::duplicate_fd(m_fd);
}

ErrorOr<int> LinuxWakeSignal::release_fd()
{
    m_duplicate_fd_calls.fetch_add(1, AK::MemoryOrder::memory_order_relaxed);
    int fd = m_fd;
    m_fd = -1;
    return fd;
}

TransportStatsSnapshot LinuxWakeSignal::snapshot_stats() const
{
    TransportStatsSnapshot snapshot;
    snapshot.component_name = "linux.wake"sv.to_byte_string();
    snapshot.signals_sent = m_signals_sent.load(AK::MemoryOrder::memory_order_relaxed);
    snapshot.wait_calls = m_wait_calls.load(AK::MemoryOrder::memory_order_relaxed);
    snapshot.wait_timeouts = m_wait_timeouts.load(AK::MemoryOrder::memory_order_relaxed);
    snapshot.drain_calls = m_drain_calls.load(AK::MemoryOrder::memory_order_relaxed);
    snapshot.duplicate_fd_calls = m_duplicate_fd_calls.load(AK::MemoryOrder::memory_order_relaxed);
    snapshot.close_calls = m_close_calls.load(AK::MemoryOrder::memory_order_relaxed);
    return snapshot;
}

}
