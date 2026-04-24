/*
 * MyceliumVR - a social VR platform built on the Ladybird browser engine
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "LinuxInputQueue.h"

#include <AK/Memory.h>
#include <LibCore/System.h>

namespace MyceliumVR::ProcessTransport {

static constexpr size_t input_queue_buffer_size = sizeof(LinuxInputQueue::SharedState);

ErrorOr<NonnullOwnPtr<LinuxInputQueue>> LinuxInputQueue::create(size_t capacity)
{
    if (capacity == 0 || capacity > MaxCapacity)
        return Error::from_string_literal("LinuxInputQueue capacity is out of range");

    auto buffer = TRY(Core::AnonymousBuffer::create_with_size(input_queue_buffer_size));
    auto* shared = new (buffer.data<void>()) SharedState;
    shared->capacity = capacity;
    auto fd = TRY(Core::System::dup(buffer.fd()));
    return adopt_nonnull_own_or_enomem(new (nothrow) LinuxInputQueue(fd, capacity, move(buffer)));
}

ErrorOr<NonnullOwnPtr<LinuxInputQueue>> LinuxInputQueue::create_from_fd(int fd, size_t capacity)
{
    if (capacity == 0 || capacity > MaxCapacity)
        return Error::from_string_literal("LinuxInputQueue capacity is out of range");

    auto buffer = TRY(Core::AnonymousBuffer::create_from_anon_fd(fd, input_queue_buffer_size));
    auto fd_copy = TRY(Core::System::dup(buffer.fd()));
    auto* shared = reinterpret_cast<SharedState*>(buffer.data<void>());
    if (shared->capacity == 0)
        shared->capacity = capacity;
    return adopt_nonnull_own_or_enomem(new (nothrow) LinuxInputQueue(fd_copy, shared->capacity, move(buffer)));
}

LinuxInputQueue::~LinuxInputQueue()
{
    if (m_fd >= 0)
        MUST(Core::System::close(m_fd));
}

ErrorOr<void> LinuxInputQueue::push_input_record(InputRecord const& record)
{
    auto* shared = state();
    auto published_sequence = shared->published_sequence.load(AK::MemoryOrder::memory_order_relaxed);
    auto consumed_sequence = shared->consumed_sequence.load(AK::MemoryOrder::memory_order_acquire);

    if (published_sequence > consumed_sequence)
        shared->dropped_count.fetch_add(1, AK::MemoryOrder::memory_order_relaxed);

    shared->latest_record = record;
    shared->published_sequence.store(published_sequence + 1, AK::MemoryOrder::memory_order_release);
    m_records_pushed.fetch_add(1, AK::MemoryOrder::memory_order_relaxed);
    return {};
}

ErrorOr<bool> LinuxInputQueue::pop_input_record(InputRecord& out_record)
{
    auto* shared = state();
    auto consumed_sequence = shared->consumed_sequence.load(AK::MemoryOrder::memory_order_relaxed);
    auto published_sequence = shared->published_sequence.load(AK::MemoryOrder::memory_order_acquire);

    if (consumed_sequence >= published_sequence)
        return false;

    out_record = shared->latest_record;
    shared->consumed_sequence.store(published_sequence, AK::MemoryOrder::memory_order_release);
    m_records_popped.fetch_add(1, AK::MemoryOrder::memory_order_relaxed);
    return true;
}

size_t LinuxInputQueue::capacity() const
{
    return m_capacity;
}

u64 LinuxInputQueue::dropped_record_count() const
{
    return state()->dropped_count.load(AK::MemoryOrder::memory_order_relaxed);
}

void LinuxInputQueue::reset()
{
    auto* shared = state();
    shared->published_sequence.store(0, AK::MemoryOrder::memory_order_relaxed);
    shared->consumed_sequence.store(0, AK::MemoryOrder::memory_order_relaxed);
    shared->dropped_count.store(0, AK::MemoryOrder::memory_order_relaxed);
}

ErrorOr<int> LinuxInputQueue::duplicate_fd() const
{
    const_cast<LinuxInputQueue*>(this)->m_duplicate_fd_calls.fetch_add(1, AK::MemoryOrder::memory_order_relaxed);
    return Core::System::dup(m_fd);
}

ErrorOr<int> LinuxInputQueue::release_fd()
{
    m_duplicate_fd_calls.fetch_add(1, AK::MemoryOrder::memory_order_relaxed);
    int fd = m_fd;
    m_fd = -1;
    return fd;
}

TransportStatsSnapshot LinuxInputQueue::snapshot_stats() const
{
    TransportStatsSnapshot snapshot;
    snapshot.component_name = "linux.input_queue"sv.to_byte_string();
    snapshot.records_pushed = m_records_pushed.load(AK::MemoryOrder::memory_order_relaxed);
    snapshot.records_popped = m_records_popped.load(AK::MemoryOrder::memory_order_relaxed);
    snapshot.records_dropped = dropped_record_count();
    snapshot.duplicate_fd_calls = m_duplicate_fd_calls.load(AK::MemoryOrder::memory_order_relaxed);
    snapshot.close_calls = m_close_calls.load(AK::MemoryOrder::memory_order_relaxed);
    return snapshot;
}

}
