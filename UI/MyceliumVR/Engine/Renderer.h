/*
 * MyceliumVR - a social VR platform built on the Ladybird browser engine
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include "World.h"

#include <SDL3/SDL.h>

namespace MyceliumVR {

class Renderer {
public:
    explicit Renderer(SDL_Renderer&);

    void resize(int width, int height);
    void draw_world(World const&);

private:
    SDL_Renderer& m_renderer;
    int m_width { 1280 };
    int m_height { 720 };
};

}
