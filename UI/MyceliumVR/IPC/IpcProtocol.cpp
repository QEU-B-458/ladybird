/*
 * MyceliumVR - a social VR platform built on the Ladybird browser engine
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "IpcProtocol.h"

#include <AK/JsonValue.h>

namespace MyceliumVR::IPC {

template<typename T>
static ErrorOr<T> read_little_endian(AK::Stream& stream)
{
    u8 bytes[sizeof(T)] {};
    TRY(stream.read_until_filled({ bytes, sizeof(bytes) }));

    T value = 0;
    for (size_t i = 0; i < sizeof(T); ++i)
        value |= static_cast<T>(bytes[i]) << (i * 8);
    return value;
}

template<typename T>
static ErrorOr<void> write_little_endian(AK::Stream& stream, T value)
{
    u8 bytes[sizeof(T)] {};
    for (size_t i = 0; i < sizeof(T); ++i)
        bytes[i] = static_cast<u8>((value >> (i * 8)) & 0xff);
    return stream.write_until_depleted({ bytes, sizeof(bytes) });
}

ErrorOr<FrameHeader> FrameHeader::read_from_stream(AK::Stream& stream)
{
    FrameHeader header;
    header.magic = TRY(read_little_endian<u32>(stream));
    header.version = TRY(read_little_endian<u16>(stream));
    header.kind = static_cast<FrameKind>(TRY(read_little_endian<u16>(stream)));
    header.request_id = TRY(read_little_endian<u64>(stream));
    header.source_world = TRY(read_little_endian<u32>(stream));
    header.target_world = TRY(read_little_endian<u32>(stream));
    header.channel_id = TRY(read_little_endian<u32>(stream));
    header.flags = TRY(read_little_endian<u32>(stream));
    header.payload_size = TRY(read_little_endian<u32>(stream));
    header.header_crc32 = TRY(read_little_endian<u32>(stream));
    TRY(header.validate());
    return header;
}

ErrorOr<void> FrameHeader::write_to_stream(AK::Stream& stream) const
{
    TRY(validate());
    TRY(write_little_endian(stream, magic));
    TRY(write_little_endian(stream, version));
    TRY(write_little_endian(stream, to_underlying(kind)));
    TRY(write_little_endian(stream, request_id));
    TRY(write_little_endian(stream, source_world));
    TRY(write_little_endian(stream, target_world));
    TRY(write_little_endian(stream, channel_id));
    TRY(write_little_endian(stream, flags));
    TRY(write_little_endian(stream, payload_size));
    TRY(write_little_endian(stream, header_crc32));
    return {};
}

ErrorOr<void> FrameHeader::validate() const
{
    if (magic != ipc_frame_magic)
        return Error::from_string_literal("IPC frame magic is invalid");
    if (version != ipc_protocol_version)
        return Error::from_string_literal("IPC protocol version is unsupported");
    if (payload_size > max_reasonable_frame_payload_size)
        return Error::from_string_literal("IPC frame payload exceeds v1 size limit");
    return {};
}

ErrorOr<void> Frame::validate() const
{
    TRY(header.validate());
    if (payload.size() != header.payload_size)
        return Error::from_string_literal("IPC frame payload size does not match header");
    return {};
}

bool frame_kind_uses_json_payload(FrameKind kind)
{
    switch (kind) {
    case FrameKind::ChannelData:
        return false;
    default:
        return true;
    }
}

ErrorOr<Frame> read_frame(AK::Stream& stream)
{
    auto header = TRY(FrameHeader::read_from_stream(stream));
    auto payload = TRY(ByteBuffer::create_uninitialized(header.payload_size));
    if (header.payload_size > 0)
        TRY(stream.read_until_filled(payload.bytes()));

    Frame frame {
        .header = header,
        .payload = move(payload),
    };
    TRY(frame.validate());
    return frame;
}

ErrorOr<void> write_frame(AK::Stream& stream, Frame const& frame)
{
    TRY(frame.validate());
    TRY(frame.header.write_to_stream(stream));
    if (!frame.payload.is_empty())
        TRY(stream.write_until_depleted(frame.payload.bytes()));
    return {};
}

ErrorOr<Frame> make_json_frame(FrameHeader const& header_template, JsonValue const& value)
{
    auto serialized = value.serialized();
    auto payload = TRY(ByteBuffer::copy(serialized.bytes()));
    Frame frame {
        .header = header_template,
        .payload = move(payload),
    };
    frame.header.payload_size = frame.payload.size();
    TRY(frame.validate());
    return frame;
}

ErrorOr<JsonValue> parse_json_payload(Frame const& frame)
{
    if (!frame_kind_uses_json_payload(frame.header.kind))
        return Error::from_string_literal("IPC frame kind uses binary payloads");
    return JsonValue::from_string(frame.payload.bytes());
}

}
