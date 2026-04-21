/*
 * MyceliumVR - a social VR platform built on the Ladybird browser engine
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <AK/ByteBuffer.h>
#include <AK/Error.h>
#include <AK/JsonValue.h>
#include <AK/Optional.h>
#include <AK/Stream.h>
#include <AK/Types.h>

namespace MyceliumVR::IPC {

constexpr u32 ipc_frame_magic = 0x4d594950;
constexpr u16 ipc_protocol_version = 1;
constexpr size_t max_reasonable_frame_payload_size = 1024 * 1024;

enum class FrameKind : u16 {
    Hello = 1,
    HelloAck = 2,
    Error = 3,
    Ping = 4,
    Pong = 5,

    SpawnWorld = 16,
    SpawnWorldAck = 17,
    StopWorld = 18,
    StopWorldAck = 19,
    WorldStateChanged = 20,

    ChannelOpen = 32,
    ChannelOpenAck = 33,
    ChannelClose = 34,
    ChannelCloseAck = 35,
    ChannelData = 36,
    ChannelEvent = 37,

    CapabilityDenied = 48,
    LogEvent = 49,
    MetricsSnapshot = 50,

    PortalOpen = 64,
    PortalOpenAck = 65,
    PortalClose = 66,
    PortalCloseAck = 67,
    CameraPoseUpdate = 68,
    VisibilityStateChange = 69,
};

struct FrameHeader {
    u32 magic { ipc_frame_magic };
    u16 version { ipc_protocol_version };
    FrameKind kind { FrameKind::Hello };
    u64 request_id { 0 };
    u32 source_world { 0 };
    u32 target_world { 0 };
    u32 channel_id { 0 };
    u32 flags { 0 };
    u32 payload_size { 0 };
    u32 header_crc32 { 0 };

    static ErrorOr<FrameHeader> read_from_stream(AK::Stream&);
    ErrorOr<void> write_to_stream(AK::Stream&) const;
    ErrorOr<void> validate() const;
};

struct Frame {
    FrameHeader header;
    ByteBuffer payload;

    ErrorOr<void> validate() const;
};

bool frame_kind_uses_json_payload(FrameKind);
ErrorOr<Frame> read_frame(AK::Stream&);
ErrorOr<void> write_frame(AK::Stream&, Frame const&);
ErrorOr<Frame> make_json_frame(FrameHeader const&, JsonValue const&);
ErrorOr<JsonValue> parse_json_payload(Frame const&);

}
