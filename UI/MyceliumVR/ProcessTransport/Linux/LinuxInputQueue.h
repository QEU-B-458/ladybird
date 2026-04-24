/*
 * MyceliumVR - a social VR platform built on the Ladybird browser engine
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include "../IInputQueue.h"
#include "../ITransportStatsProvider.h"

#include <AK/Atomic.h>
#include <AK/Array.h>
#include <AK/NonnullOwnPtr.h>
#include <LibCore/AnonymousBuffer.h>

namespace MyceliumVR::ProcessTransport {

class LinuxInputQueue final
    : public IInputQueue
    , public ITransportStatsProvider {
public:
    static constexpr size_t DefaultCapacity = 1;
    static constexpr size_t MaxCapacity = 1;

    struct SharedState {
        Atomic<u64, AK::MemoryOrder::memory_order_seq_cst> published_sequence { 0 };
        Atomic<u64, AK::MemoryOrder::memory_order_seq_cst> consumed_sequence { 0 };
        Atomic<u64, AK::MemoryOrder::memory_order_seq_cst> dropped_count { 0 };
        u32 capacity { 0 };
        InputRecord latest_record;
    };

    static ErrorOr<NonnullOwnPtr<LinuxInputQueue>> create(size_t capacity = DefaultCapacity);
    static ErrorOr<NonnullOwnPtr<LinuxInputQueue>> create_from_fd(int fd, size_t capacity = DefaultCapacity);

    virtual ~LinuxInputQueue() override;

    virtual ErrorOr<void> push_input_record(InputRecord const&) override;
    virtual ErrorOr<bool> pop_input_record(InputRecord&) override;
    virtual size_t capacity() const override;
    virtual u64 dropped_record_count() const override;
    virtual void reset() override;
    virtual TransportStatsSnapshot snapshot_stats() const override;

    ErrorOr<int> duplicate_fd() const;
    ErrorOr<int> release_fd();

private:
    LinuxInputQueue(int fd, size_t capacity, Core::AnonymousBuffer buffer)
        : m_fd(fd)
        , m_capacity(capacity)
        , m_buffer(move(buffer))
    {
    }

    SharedState* state() { return reinterpret_cast<SharedState*>(m_buffer.data<void>()); }
    SharedState const* state() const { return reinterpret_cast<SharedState const*>(m_buffer.data<void>()); }

    int m_fd { -1 };
    size_t m_capacity { 0 };
    Core::AnonymousBuffer m_buffer;
    Atomic<u64> m_records_pushed { 0 };
    Atomic<u64> m_records_popped { 0 };
    Atomic<u64> m_duplicate_fd_calls { 0 };
    Atomic<u64> m_close_calls { 0 };
};

}
