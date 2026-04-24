/*
 * MyceliumVR - a social VR platform built on the Ladybird browser engine
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include "WorldHostEndpoint.h"
#include "WorldClientEndpoint.h"

#include <AK/Function.h>
#include <LibIPC/ConnectionToServer.h>

namespace MyceliumVR {

class WorldClientConnection final
    : public IPC::ConnectionToServer<WorldClientEndpoint, WorldHostEndpoint> {
    C_OBJECT(WorldClientConnection);

public:
    virtual ~WorldClientConnection() override;

private:
    WorldClientConnection(NonnullOwnPtr<IPC::Transport> transport);
};

}
