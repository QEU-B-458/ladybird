/*
 * MyceliumVR - a social VR platform built on the Ladybird browser engine
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "LinuxSharedMemoryRegion.h"

#include "LinuxSyscalls.h"

namespace MyceliumVR::ProcessTransport {

ErrorOr<NonnullOwnPtr<LinuxSharedMemoryRegion>> LinuxSharedMemoryRegion::create(size_t size)
{
    auto buffer = TRY(Core::AnonymousBuffer::create_with_size(size));
    auto fd = TRY(LinuxSyscalls::duplicate_fd(buffer.fd()));
    return adopt_nonnull_own_or_enomem(new (nothrow) LinuxSharedMemoryRegion(fd, size, move(buffer)));
}

ErrorOr<NonnullOwnPtr<LinuxSharedMemoryRegion>> LinuxSharedMemoryRegion::create_from_fd(int fd, size_t size)
{
    auto buffer = TRY(Core::AnonymousBuffer::create_from_anon_fd(fd, size));
    auto owned_fd = TRY(LinuxSyscalls::duplicate_fd(buffer.fd()));
    return adopt_nonnull_own_or_enomem(new (nothrow) LinuxSharedMemoryRegion(owned_fd, size, move(buffer)));
}

LinuxSharedMemoryRegion::~LinuxSharedMemoryRegion()
{
    unmap();
    if (m_fd >= 0)
        MUST(LinuxSyscalls::close_fd(m_fd));
}

ErrorOr<void> LinuxSharedMemoryRegion::map()
{
    if (m_buffer.is_valid())
        return {};
    m_map_calls.fetch_add(1, AK::MemoryOrder::memory_order_relaxed);
    m_buffer = TRY(Core::AnonymousBuffer::create_from_anon_fd(TRY(LinuxSyscalls::duplicate_fd(m_fd)), m_size));
    return {};
}

void LinuxSharedMemoryRegion::unmap()
{
    m_buffer = {};
}

Bytes LinuxSharedMemoryRegion::bytes()
{
    if (!m_buffer.is_valid())
        return {};
    return { m_buffer.data<void>(), m_buffer.size() };
}

ReadonlyBytes LinuxSharedMemoryRegion::bytes() const
{
    return m_buffer.bytes();
}

ErrorOr<int> LinuxSharedMemoryRegion::duplicate_fd() const
{
    const_cast<LinuxSharedMemoryRegion*>(this)->m_duplicate_fd_calls.fetch_add(1, AK::MemoryOrder::memory_order_relaxed);
    return LinuxSyscalls::duplicate_fd(m_fd);
}

ErrorOr<int> LinuxSharedMemoryRegion::release_fd()
{
    m_duplicate_fd_calls.fetch_add(1, AK::MemoryOrder::memory_order_relaxed);
    int fd = m_fd;
    m_fd = -1;
    return fd;
}

TransportStatsSnapshot LinuxSharedMemoryRegion::snapshot_stats() const
{
    TransportStatsSnapshot snapshot;
    snapshot.component_name = "linux.shm"sv.to_byte_string();
    snapshot.map_calls = m_map_calls.load(AK::MemoryOrder::memory_order_relaxed);
    snapshot.duplicate_fd_calls = m_duplicate_fd_calls.load(AK::MemoryOrder::memory_order_relaxed);
    snapshot.close_calls = m_close_calls.load(AK::MemoryOrder::memory_order_relaxed);
    return snapshot;
}

}
