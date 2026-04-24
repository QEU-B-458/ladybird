/*
 * MyceliumVR - a social VR platform built on the Ladybird browser engine
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include "../ISharedMemoryRegion.h"
#include "../ITransportStatsProvider.h"

#include <AK/Atomic.h>
#include <AK/NonnullOwnPtr.h>
#include <LibCore/AnonymousBuffer.h>

namespace MyceliumVR::ProcessTransport {

class LinuxSharedMemoryRegion final
    : public ISharedMemoryRegion
    , public ITransportStatsProvider {
public:
    static ErrorOr<NonnullOwnPtr<LinuxSharedMemoryRegion>> create(size_t size);
    static ErrorOr<NonnullOwnPtr<LinuxSharedMemoryRegion>> create_from_fd(int fd, size_t size);

    virtual ~LinuxSharedMemoryRegion() override;

    virtual ErrorOr<void> map() override;
    virtual void unmap() override;

    virtual Bytes bytes() override;
    virtual ReadonlyBytes bytes() const override;

    virtual size_t size() const override { return m_size; }
    virtual bool is_mapped() const override { return m_buffer.is_valid(); }
    virtual TransportStatsSnapshot snapshot_stats() const override;

    ErrorOr<int> duplicate_fd() const;
    ErrorOr<int> release_fd();

private:
    LinuxSharedMemoryRegion(int fd, size_t size, Core::AnonymousBuffer buffer)
        : m_fd(fd)
        , m_size(size)
        , m_buffer(move(buffer))
    {
    }

    int m_fd { -1 };
    size_t m_size { 0 };
    Core::AnonymousBuffer m_buffer;
    Atomic<u64> m_map_calls { 0 };
    Atomic<u64> m_duplicate_fd_calls { 0 };
    Atomic<u64> m_close_calls { 0 };
};

}
