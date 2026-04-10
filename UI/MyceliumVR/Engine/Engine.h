/*
 * MyceliumVR - a social VR platform built on the Ladybird browser engine
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include "Renderer.h"
#include "Session.h"

namespace MyceliumVR {

class Engine {
public:
    explicit Engine(SDL_Renderer&);

    Session& session() { return m_session; }
    Session const& session() const { return m_session; }

    World& active_world() { return m_session.active_world(); }
    World const& active_world() const { return m_session.active_world(); }

    World& world() { return active_world(); }
    World const& world() const { return active_world(); }

    Renderer& renderer() { return m_renderer; }

    void resize(int width, int height);
    void render();

private:
    Session m_session;
    Renderer m_renderer;
};

}
