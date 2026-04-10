/*
 * MyceliumVR - a social VR platform built on the Ladybird browser engine
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "Engine.h"

namespace MyceliumVR {

Engine::Engine(SDL_Renderer& renderer)
    : m_renderer(renderer)
{
}

void Engine::resize(int width, int height)
{
    m_renderer.resize(width, height);
}

void Engine::render()
{
    m_renderer.draw_world(m_session.active_world());
    m_session.active_world().clear_dirty_flags();
}

}
