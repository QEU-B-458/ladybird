/*
 * MyceliumVR - a social VR platform built on the Ladybird browser engine
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include "../IFrameMailbox.h"
#include "../ITransportStatsProvider.h"

#include <AK/Atomic.h>
#include <AK/NonnullOwnPtr.h>
#include <LibCore/AnonymousBuffer.h>

namespace MyceliumVR::ProcessTransport {

class LinuxFrameMailbox final
    : public IFrameMailbox
    , public ITransportStatsProvider {
public:
    static constexpr size_t DefaultSlotCount = 3;
    static constexpr size_t MaxSlotCount = 8;

    static ErrorOr<NonnullOwnPtr<LinuxFrameMailbox>> create(size_t slot_capacity_bytes, size_t slot_count = DefaultSlotCount);
    static ErrorOr<NonnullOwnPtr<LinuxFrameMailbox>> create_from_fd(int fd, size_t slot_capacity_bytes, size_t slot_count = DefaultSlotCount);

    virtual ~LinuxFrameMailbox() override;

    virtual size_t slot_count() const override { return m_slot_count; }
    virtual size_t slot_capacity_bytes() const override { return m_slot_capacity_bytes; }

    virtual ErrorOr<FrameWriteSlot> acquire_write_slot() override;
    virtual ErrorOr<void> publish_frame(FramePublishInfo const&) override;
    virtual Optional<FramePublishInfo> latest_frame() const override;
    virtual Optional<FrameReadView> latest_frame_view() const override;
    virtual ReadonlyBytes read_slot_bytes(u32 slot_index) const override;

    virtual ErrorOr<void> publish_static_scene(StaticScenePublishInfo const&) override;
    virtual Optional<StaticScenePublishInfo> latest_static_scene() const override;

    virtual void reset() override;
    virtual TransportStatsSnapshot snapshot_stats() const override;

    ErrorOr<int> duplicate_fd() const;
    ErrorOr<int> release_fd();

private:
    struct SharedHeader {
        u32 protocol_version { WorldProcessTransportProtocolVersion };
        u32 slot_count { 0 };
        u32 slot_capacity_bytes { 0 };
        Atomic<u64, AK::MemoryOrder::memory_order_seq_cst> published_frame_sequence { 0 };
        Atomic<u32, AK::MemoryOrder::memory_order_seq_cst> published_slot_index { 0 };
        Atomic<u32, AK::MemoryOrder::memory_order_seq_cst> published_slot_bytes { 0 };
        Atomic<u32, AK::MemoryOrder::memory_order_seq_cst> published_scene_revision { 0 };
        Atomic<u32, AK::MemoryOrder::memory_order_seq_cst> static_scene_slot_bytes { 0 };
        Atomic<u32, AK::MemoryOrder::memory_order_seq_cst> static_scene_revision { 0 };
        Array<Atomic<u64, AK::MemoryOrder::memory_order_seq_cst>, MaxSlotCount> slot_states;
    };

    LinuxFrameMailbox(int fd, size_t slot_capacity_bytes, size_t slot_count, Core::AnonymousBuffer buffer)
        : m_fd(fd)
        , m_slot_capacity_bytes(slot_capacity_bytes)
        , m_slot_count(slot_count)
        , m_buffer(move(buffer))
    {
    }

    SharedHeader* header() { return reinterpret_cast<SharedHeader*>(m_buffer.data<void>()); }
    SharedHeader const* header() const { return reinterpret_cast<SharedHeader const*>(m_buffer.data<void>()); }
    size_t header_bytes() const { return sizeof(SharedHeader); }
    size_t total_bytes() const { return header_bytes() + (m_slot_capacity_bytes * m_slot_count); }

    Bytes slot_bytes_mut(u32 slot_index);

    int m_fd { -1 };
    size_t m_slot_capacity_bytes { 0 };
    size_t m_slot_count { 0 };
    size_t m_next_write_slot { 0 };
    Core::AnonymousBuffer m_buffer;
    Atomic<u64> m_frames_published { 0 };
    Atomic<u64> m_static_scenes_published { 0 };
    Atomic<u64> m_duplicate_fd_calls { 0 };
    Atomic<u64> m_close_calls { 0 };
};

}
