/*
 * MyceliumVR - a social VR platform built on the Ladybird browser engine
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include "TransportTypes.h"

#include <AK/Error.h>
#include <AK/Optional.h>

namespace MyceliumVR::ProcessTransport {

class IFrameMailbox {
public:
    virtual ~IFrameMailbox() = default;

    virtual size_t slot_count() const = 0;
    virtual size_t slot_capacity_bytes() const = 0;

    virtual ErrorOr<FrameWriteSlot> acquire_write_slot() = 0;
    virtual ErrorOr<void> publish_frame(FramePublishInfo const&) = 0;
    virtual Optional<FramePublishInfo> latest_frame() const = 0;
    virtual Optional<FrameReadView> latest_frame_view() const = 0;
    virtual ReadonlyBytes read_slot_bytes(u32 slot_index) const = 0;

    virtual ErrorOr<void> publish_static_scene(StaticScenePublishInfo const&) = 0;
    virtual Optional<StaticScenePublishInfo> latest_static_scene() const = 0;

    virtual void reset() = 0;
};

}
