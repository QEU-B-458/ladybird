/*
 * MyceliumVR - a social VR platform built on the Ladybird browser engine
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "Engine.h"

#include "../Support/InputState.h"
#include "../Support/VirtualFileSystem.h"

namespace MyceliumVR {

Engine::Engine(SDL_Window& window, VirtualFileSystem const* file_system)
    : m_renderer(window, file_system)
{
}

ErrorOr<void> Engine::initialize_world_management_system(VirtualFileSystem* virtual_file_system, InputState* input_state)
{
    m_world_management_system.set_environment(virtual_file_system, input_state);
    m_world_management_system.set_renderer(&m_renderer);
    return m_world_management_system.initialize();
}

void Engine::resize(int width, int height)
{
    m_renderer.resize(width, height);
}

ErrorOr<void> Engine::render()
{
    TRY(m_renderer.draw_world(m_world_management_system.active_world()));
    m_world_management_system.active_world().clear_dirty_flags();
    return {};
}

}
