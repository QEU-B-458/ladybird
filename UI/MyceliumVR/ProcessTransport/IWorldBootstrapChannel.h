/*
 * MyceliumVR - a social VR platform built on the Ladybird browser engine
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <AK/ByteBuffer.h>
#include <AK/Error.h>
#include <AK/Optional.h>

namespace MyceliumVR::ProcessTransport {

enum class BootstrapMessageKind {
    Hello,
    ResourcesReady,
    ControlBusDiscovery,
    WorldReady,
    StaticSceneRegionResized,
    Shutdown,
    Fault,
    Log,
};

struct BootstrapMessage {
    BootstrapMessageKind kind { BootstrapMessageKind::Hello };
    ByteBuffer payload;
};

class IWorldBootstrapChannel {
public:
    virtual ~IWorldBootstrapChannel() = default;

    virtual ErrorOr<void> send(BootstrapMessage const&) = 0;
    virtual ErrorOr<Optional<BootstrapMessage>> receive() = 0;
    virtual void close() = 0;
};

}
