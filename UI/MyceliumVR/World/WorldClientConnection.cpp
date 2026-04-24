/*
 * MyceliumVR - a social VR platform built on the Ladybird browser engine
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "WorldClientConnection.h"

namespace MyceliumVR {

WorldClientConnection::WorldClientConnection(NonnullOwnPtr<IPC::Transport> transport)
    : IPC::ConnectionToServer<WorldClientEndpoint, WorldHostEndpoint>(*this, move(transport))
{
}

WorldClientConnection::~WorldClientConnection() = default;

}
