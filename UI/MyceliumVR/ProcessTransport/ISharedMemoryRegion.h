/*
 * MyceliumVR - a social VR platform built on the Ladybird browser engine
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <AK/Error.h>
#include <AK/Span.h>

namespace MyceliumVR::ProcessTransport {

class ISharedMemoryRegion {
public:
    virtual ~ISharedMemoryRegion() = default;

    virtual ErrorOr<void> map() = 0;
    virtual void unmap() = 0;

    virtual Bytes bytes() = 0;
    virtual ReadonlyBytes bytes() const = 0;

    virtual size_t size() const = 0;
    virtual bool is_mapped() const = 0;
};

}
