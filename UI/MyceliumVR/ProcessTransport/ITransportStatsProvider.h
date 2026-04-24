/*
 * MyceliumVR - a social VR platform built on the Ladybird browser engine
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include "TransportTypes.h"

namespace MyceliumVR::ProcessTransport {

class ITransportStatsProvider {
public:
    virtual ~ITransportStatsProvider() = default;

    virtual TransportStatsSnapshot snapshot_stats() const = 0;
};

}
