/*
 * MyceliumVR - a social VR platform built on the Ladybird browser engine
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "Engine.h"

namespace MyceliumVR {

Engine::Engine(SDL_Window& window, VirtualFileSystem const* file_system)
    : m_renderer(window, file_system)
{
}

void Engine::resize(int width, int height)
{
    m_renderer.resize(width, height);
}

ErrorOr<void> Engine::render()
{
    TRY(m_renderer.draw_world(m_session.active_world()));
    m_session.active_world().clear_dirty_flags();
    return {};
}

}
