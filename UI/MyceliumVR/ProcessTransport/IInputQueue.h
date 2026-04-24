/*
 * MyceliumVR - a social VR platform built on the Ladybird browser engine
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include "TransportTypes.h"

#include <AK/Error.h>

namespace MyceliumVR::ProcessTransport {

class IInputQueue {
public:
    virtual ~IInputQueue() = default;

    virtual ErrorOr<void> push_input_record(InputRecord const&) = 0;
    virtual ErrorOr<bool> pop_input_record(InputRecord&) = 0;
    virtual size_t capacity() const = 0;
    virtual u64 dropped_record_count() const = 0;
    virtual void reset() = 0;
};

}
