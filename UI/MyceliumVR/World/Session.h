/*
 * MyceliumVR - a social VR platform built on the Ladybird browser engine
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include "World.h"

#include <AK/OwnPtr.h>
#include <AK/String.h>

namespace MyceliumVR {

using WorldId = u32;

class Session {
public:
    Session();

    WorldId active_world_id() const { return m_active_world_id; }
    String const& active_world_name() const { return m_active_world_name; }

    World& active_world() { return m_active_world; }
    World const& active_world() const { return m_active_world; }

    World& reset_active_world(String name);

    bool has_loading_world() const { return !!m_loading_world; }
    WorldId loading_world_id() const { return m_loading_world_id; }
    String const& loading_world_name() const { return m_loading_world_name; }

    World& begin_loading_world(String name);
    void cancel_loading_world();
    bool activate_loading_world();

private:
    WorldId allocate_world_id();

    WorldId m_next_world_id { 1 };
    WorldId m_active_world_id { 0 };
    String m_active_world_name;
    World m_active_world;

    WorldId m_loading_world_id { 0 };
    String m_loading_world_name;
    OwnPtr<World> m_loading_world;
};

}
