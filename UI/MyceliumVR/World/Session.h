/*
 * MyceliumVR - a social VR platform built on the Ladybird browser engine
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <AK/String.h>

namespace MyceliumVR {

using WorldId = u32;

class Session {
public:
    Session();
    ~Session();

    WorldId active_world_id() const { return m_active_world_id; }
    String const& active_world_name() const { return m_active_world_name; }
    void set_active_world(WorldId, String);

    bool has_loading_world() const;
    WorldId loading_world_id() const { return m_loading_world_id; }
    String const& loading_world_name() const { return m_loading_world_name; }
    void set_loading_world(WorldId, String);
    WorldId begin_loading_world(String name);
    void cancel_loading_world();
    bool activate_loading_world();

private:
    WorldId allocate_world_id();

    WorldId m_next_world_id { 1 };
    WorldId m_active_world_id { 0 };
    String m_active_world_name;
    WorldId m_loading_world_id { 0 };
    String m_loading_world_name;
};

}
