/*
 * MyceliumVR - a social VR platform built on the Ladybird browser engine
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <AK/Array.h>
#include <AK/StringView.h>
#include <SDL3/SDL.h>

namespace MyceliumVR {

class InputState {
public:
    void begin_frame();
    void handle_sdl_event(SDL_Event const&);

    bool key_down(SDL_Scancode) const;
    bool key_down(StringView name) const;
    bool mouse_button_down(u8 button) const;

    float mouse_delta_x() const { return m_mouse_delta_x; }
    float mouse_delta_y() const { return m_mouse_delta_y; }
    float wheel_delta_x() const { return m_wheel_delta_x; }
    float wheel_delta_y() const { return m_wheel_delta_y; }
    bool consume_key_press(SDL_Scancode);

private:
    Array<bool, SDL_SCANCODE_COUNT> m_keys_down {};
    Array<bool, SDL_SCANCODE_COUNT> m_keys_pressed {};
    Array<bool, 8> m_mouse_buttons_down {};
    float m_mouse_delta_x { 0.0f };
    float m_mouse_delta_y { 0.0f };
    float m_wheel_delta_x { 0.0f };
    float m_wheel_delta_y { 0.0f };
};

}
