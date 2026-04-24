/*
 * MyceliumVR - a social VR platform built on the Ladybird browser engine
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include "TransportTypes.h"

#include <AK/Error.h>

namespace MyceliumVR::ProcessTransport {

class IWakeSignal {
public:
    virtual ~IWakeSignal() = default;

    virtual ErrorOr<void> signal() = 0;
    virtual ErrorOr<WakeWaitStatus> wait(u32 timeout_ms) = 0;
    virtual ErrorOr<void> drain() = 0;
};

}
