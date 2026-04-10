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

World& Session::reset_active_world(String name)
{
    m_active_world = {};
    m_active_world_id = allocate_world_id();
    m_active_world_name = move(name);
    return m_active_world;
}

World& Session::begin_loading_world(String name)
{
    m_loading_world = make<World>();
    m_loading_world_id = allocate_world_id();
    m_loading_world_name = move(name);
    return *m_loading_world;
}

void Session::cancel_loading_world()
{
    m_loading_world = nullptr;
    m_loading_world_id = 0;
    m_loading_world_name = {};
}

bool Session::activate_loading_world()
{
    if (!m_loading_world)
        return false;

    m_active_world = move(*m_loading_world);
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
