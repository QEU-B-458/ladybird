/*
 * MyceliumVR - a social VR platform built on the Ladybird browser engine
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include "../Support/InputState.h"

#include <AK/ByteString.h>
#include <AK/Span.h>
#include <AK/Types.h>

namespace MyceliumVR::ProcessTransport {

static constexpr u32 WorldProcessTransportProtocolVersion = 1;

enum class WakeWaitStatus {
    Signaled,
    Timeout,
};

struct TickMetadata {
    u64 host_frame_id { 0 };
    double delta_time_seconds { 0.0 };
    u64 host_timestamp_ns { 0 };
};

struct InputRecord {
    u64 sequence { 0 };
    TickMetadata tick;
    InputFrameState input;
};

struct FramePublishInfo {
    u64 frame_sequence { 0 };
    u32 slot_index { 0 };
    u32 slot_bytes { 0 };
    u32 scene_revision { 0 };
};

struct StaticScenePublishInfo {
    u32 slot_bytes { 0 };
    u32 scene_revision { 0 };
};

struct FrameWriteSlot {
    u32 slot_index { 0 };
    Bytes bytes;
};

struct FrameReadSlot {
    u32 slot_index { 0 };
    ReadonlyBytes bytes;
};

struct FrameReadView {
    FramePublishInfo info;
    ReadonlyBytes bytes;
};

struct TransportStatsSnapshot {
    ByteString component_name;
    u64 messages_sent { 0 };
    u64 messages_received { 0 };
    u64 bytes_sent { 0 };
    u64 bytes_received { 0 };
    u64 signals_sent { 0 };
    u64 wait_calls { 0 };
    u64 wait_timeouts { 0 };
    u64 drain_calls { 0 };
    u64 records_pushed { 0 };
    u64 records_popped { 0 };
    u64 records_dropped { 0 };
    u64 frames_published { 0 };
    u64 static_scene_published { 0 };
    u64 map_calls { 0 };
    u64 duplicate_fd_calls { 0 };
    u64 close_calls { 0 };
};

}
