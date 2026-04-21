/*
 * MyceliumVR - a social VR platform built on the Ladybird browser engine
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "SupervisorClient.h"
#include "../World/WorldManagementSystem.h"

namespace MyceliumVR {

Messages::WorldWorker::PingResponse SupervisorClient::ping(u64 time_ms)
{
    return { time_ms };
}

}
