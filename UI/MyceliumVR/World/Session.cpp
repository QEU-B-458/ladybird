/*
 * MyceliumVR - a social VR platform built on the Ladybird browser engine
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "Session.h"

namespace MyceliumVR {

Session::Session()
{
    m_active_world_id = allocate_world_id();
    m_active_world_name = "default"_string;
}

Session::~Session() = default;

void Session::set_active_world(WorldId world_id, String name)
{
    m_active_world_id = world_id;
    m_active_world_name = move(name);
}

bool Session::has_loading_world() const
{
    return m_loading_world_id != 0;
}

void Session::set_loading_world(WorldId world_id, String name)
{
    m_loading_world_id = world_id;
    m_loading_world_name = move(name);
}

WorldId Session::begin_loading_world(String name)
{
    auto world_id = allocate_world_id();
    set_loading_world(world_id, move(name));
    return world_id;
}

void Session::cancel_loading_world()
{
    m_loading_world_id = 0;
    m_loading_world_name = {};
}

bool Session::activate_loading_world()
{
    if (!has_loading_world())
        return false;

    m_active_world_id = m_loading_world_id;
    m_active_world_name = move(m_loading_world_name);
    cancel_loading_world();
    return true;
}

WorldId Session::allocate_world_id()
{
    return m_next_world_id++;
}

}
