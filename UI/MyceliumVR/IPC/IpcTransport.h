/*
 * MyceliumVR - a social VR platform built on the Ladybird browser engine
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include "IpcProtocol.h"

#include <AK/Error.h>
#include <AK/OwnPtr.h>
#include <AK/String.h>

namespace MyceliumVR::IPC {

class IIpcConnection {
public:
    virtual ~IIpcConnection() = default;

    virtual ErrorOr<void> send_frame(Frame const&) = 0;
    virtual ErrorOr<Frame> receive_frame() = 0;
    virtual ErrorOr<bool> can_read_without_blocking() const = 0;
    virtual bool is_open() const = 0;
    virtual void close() = 0;
    virtual StringView description() const = 0;
};

class IIpcListener {
public:
    virtual ~IIpcListener() = default;

    virtual ErrorOr<NonnullOwnPtr<IIpcConnection>> accept() = 0;
    virtual bool is_open() const = 0;
    virtual void close() = 0;
    virtual StringView description() const = 0;
};

}
