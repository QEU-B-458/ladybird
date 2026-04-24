/*
 * MyceliumVR - a social VR platform built on the Ladybird browser engine
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "LinuxFrameMailbox.h"

#include <AK/Memory.h>
#include <LibCore/System.h>

namespace MyceliumVR::ProcessTransport {

ErrorOr<NonnullOwnPtr<LinuxFrameMailbox>> LinuxFrameMailbox::create(size_t slot_capacity_bytes, size_t slot_count)
{
    if (slot_capacity_bytes == 0 || slot_count == 0 || slot_count > MaxSlotCount)
        return Error::from_string_literal("LinuxFrameMailbox must have non-zero capacity and slot count");

    auto total_size = sizeof(SharedHeader) + (slot_capacity_bytes * slot_count);
    auto buffer = TRY(Core::AnonymousBuffer::create_with_size(total_size));
    auto* shared = new (buffer.data<void>()) SharedHeader;
    shared->slot_count = slot_count;
    shared->slot_capacity_bytes = slot_capacity_bytes;
    auto fd = TRY(Core::System::dup(buffer.fd()));
    return adopt_nonnull_own_or_enomem(new (nothrow) LinuxFrameMailbox(fd, slot_capacity_bytes, slot_count, move(buffer)));
}

ErrorOr<NonnullOwnPtr<LinuxFrameMailbox>> LinuxFrameMailbox::create_from_fd(int fd, size_t slot_capacity_bytes, size_t slot_count)
{
    if (slot_capacity_bytes == 0 || slot_count == 0 || slot_count > MaxSlotCount)
        return Error::from_string_literal("LinuxFrameMailbox must have non-zero capacity and slot count");

    auto total_size = sizeof(SharedHeader) + (slot_capacity_bytes * slot_count);
    auto buffer = TRY(Core::AnonymousBuffer::create_from_anon_fd(fd, total_size));
    auto fd_copy = TRY(Core::System::dup(buffer.fd()));
    return adopt_nonnull_own_or_enomem(new (nothrow) LinuxFrameMailbox(fd_copy, slot_capacity_bytes, slot_count, move(buffer)));
}

LinuxFrameMailbox::~LinuxFrameMailbox()
{
    if (m_fd >= 0)
        MUST(Core::System::close(m_fd));
}

ErrorOr<FrameWriteSlot> LinuxFrameMailbox::acquire_write_slot()
{
    auto slot_index = static_cast<u32>(m_next_write_slot % m_slot_count);
    auto current_state = header()->slot_states[slot_index].load(AK::MemoryOrder::memory_order_relaxed);
    auto busy_state = current_state + 1;
    if ((busy_state & 1) == 0)
        ++busy_state;
    header()->slot_states[slot_index].store(busy_state, AK::MemoryOrder::memory_order_release);
    auto bytes = slot_bytes_mut(slot_index);
    return FrameWriteSlot { .slot_index = slot_index, .bytes = bytes };
}

ErrorOr<void> LinuxFrameMailbox::publish_frame(FramePublishInfo const& info)
{
    auto slot_state = header()->slot_states[info.slot_index].load(AK::MemoryOrder::memory_order_relaxed);
    if ((slot_state & 1) == 0)
        return Error::from_string_literal("LinuxFrameMailbox publish_frame called without owning a write slot");

    header()->published_slot_bytes.store(info.slot_bytes, AK::MemoryOrder::memory_order_relaxed);
    header()->published_scene_revision.store(info.scene_revision, AK::MemoryOrder::memory_order_relaxed);
    header()->published_slot_index.store(info.slot_index, AK::MemoryOrder::memory_order_release);
    header()->published_frame_sequence.store(info.frame_sequence, AK::MemoryOrder::memory_order_release);
    header()->slot_states[info.slot_index].store(slot_state + 1, AK::MemoryOrder::memory_order_release);
    m_next_write_slot = (info.slot_index + 1) % m_slot_count;
    m_frames_published.fetch_add(1, AK::MemoryOrder::memory_order_relaxed);
    return {};
}

Optional<FramePublishInfo> LinuxFrameMailbox::latest_frame() const
{
    auto frame_sequence = header()->published_frame_sequence.load(AK::MemoryOrder::memory_order_acquire);
    if (frame_sequence == 0)
        return {};

    return FramePublishInfo {
        .frame_sequence = frame_sequence,
        .slot_index = header()->published_slot_index.load(AK::MemoryOrder::memory_order_acquire),
        .slot_bytes = header()->published_slot_bytes.load(AK::MemoryOrder::memory_order_relaxed),
        .scene_revision = header()->published_scene_revision.load(AK::MemoryOrder::memory_order_relaxed),
    };
}

Optional<FrameReadView> LinuxFrameMailbox::latest_frame_view() const
{
    auto published = latest_frame();
    if (!published.has_value())
        return {};

    if (published->slot_index >= m_slot_count)
        return {};

    auto pre_state = header()->slot_states[published->slot_index].load(AK::MemoryOrder::memory_order_acquire);
    if ((pre_state & 1) != 0)
        return {};

    auto bytes = read_slot_bytes(published->slot_index);
    auto post_state = header()->slot_states[published->slot_index].load(AK::MemoryOrder::memory_order_acquire);
    if (pre_state != post_state || (post_state & 1) != 0)
        return {};

    return FrameReadView {
        .info = *published,
        .bytes = bytes,
    };
}

ReadonlyBytes LinuxFrameMailbox::read_slot_bytes(u32 slot_index) const
{
    if (slot_index >= m_slot_count)
        return {};

    auto published = latest_frame();
    auto slot_bytes = published.has_value() && published->slot_index == slot_index ? published->slot_bytes : m_slot_capacity_bytes;
    auto offset = sizeof(SharedHeader) + (slot_index * m_slot_capacity_bytes);
    return m_buffer.bytes().slice(offset, min(slot_bytes, m_slot_capacity_bytes));
}

ErrorOr<void> LinuxFrameMailbox::publish_static_scene(StaticScenePublishInfo const& info)
{
    header()->static_scene_slot_bytes.store(info.slot_bytes, AK::MemoryOrder::memory_order_relaxed);
    header()->static_scene_revision.store(info.scene_revision, AK::MemoryOrder::memory_order_release);
    m_static_scenes_published.fetch_add(1, AK::MemoryOrder::memory_order_relaxed);
    return {};
}

Optional<StaticScenePublishInfo> LinuxFrameMailbox::latest_static_scene() const
{
    auto revision = header()->static_scene_revision.load(AK::MemoryOrder::memory_order_acquire);
    if (revision == 0)
        return {};
    return StaticScenePublishInfo {
        .slot_bytes = header()->static_scene_slot_bytes.load(AK::MemoryOrder::memory_order_relaxed),
        .scene_revision = revision,
    };
}

void LinuxFrameMailbox::reset()
{
    header()->published_frame_sequence.store(0, AK::MemoryOrder::memory_order_relaxed);
    header()->published_slot_index.store(0, AK::MemoryOrder::memory_order_relaxed);
    header()->published_slot_bytes.store(0, AK::MemoryOrder::memory_order_relaxed);
    header()->published_scene_revision.store(0, AK::MemoryOrder::memory_order_relaxed);
    header()->static_scene_slot_bytes.store(0, AK::MemoryOrder::memory_order_relaxed);
    header()->static_scene_revision.store(0, AK::MemoryOrder::memory_order_relaxed);
    for (size_t i = 0; i < MaxSlotCount; ++i)
        header()->slot_states[i].store(0, AK::MemoryOrder::memory_order_relaxed);
    m_next_write_slot = 0;
}

ErrorOr<int> LinuxFrameMailbox::duplicate_fd() const
{
    const_cast<LinuxFrameMailbox*>(this)->m_duplicate_fd_calls.fetch_add(1, AK::MemoryOrder::memory_order_relaxed);
    return Core::System::dup(m_fd);
}

ErrorOr<int> LinuxFrameMailbox::release_fd()
{
    m_duplicate_fd_calls.fetch_add(1, AK::MemoryOrder::memory_order_relaxed);
    int fd = m_fd;
    m_fd = -1;
    return fd;
}

Bytes LinuxFrameMailbox::slot_bytes_mut(u32 slot_index)
{
    auto offset = sizeof(SharedHeader) + (slot_index * m_slot_capacity_bytes);
    return { m_buffer.data<u8>() + offset, m_slot_capacity_bytes };
}

TransportStatsSnapshot LinuxFrameMailbox::snapshot_stats() const
{
    TransportStatsSnapshot snapshot;
    snapshot.component_name = "linux.frame_mailbox"sv.to_byte_string();
    snapshot.frames_published = m_frames_published.load(AK::MemoryOrder::memory_order_relaxed);
    snapshot.static_scene_published = m_static_scenes_published.load(AK::MemoryOrder::memory_order_relaxed);
    snapshot.duplicate_fd_calls = m_duplicate_fd_calls.load(AK::MemoryOrder::memory_order_relaxed);
    snapshot.close_calls = m_close_calls.load(AK::MemoryOrder::memory_order_relaxed);
    return snapshot;
}

}
