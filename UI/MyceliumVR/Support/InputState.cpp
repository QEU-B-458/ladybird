/*
 * MyceliumVR - a social VR platform built on the Ladybird browser engine
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "InputState.h"

#include <AK/String.h>

namespace MyceliumVR {

void InputState::begin_frame()
{
    m_keys_pressed.fill(false);
    m_mouse_delta_x = 0.0f;
    m_mouse_delta_y = 0.0f;
    m_wheel_delta_x = 0.0f;
    m_wheel_delta_y = 0.0f;
}

void InputState::handle_sdl_event(SDL_Event const& event)
{
    switch (event.type) {
    case SDL_EVENT_KEY_DOWN:
        if (event.key.scancode < m_keys_down.size()) {
            if (!m_keys_down[event.key.scancode])
                m_keys_pressed[event.key.scancode] = true;
            m_keys_down[event.key.scancode] = true;
        }
        break;
    case SDL_EVENT_KEY_UP:
        if (event.key.scancode < m_keys_down.size())
            m_keys_down[event.key.scancode] = false;
        break;
    case SDL_EVENT_MOUSE_BUTTON_DOWN:
        if (event.button.button < m_mouse_buttons_down.size())
            m_mouse_buttons_down[event.button.button] = true;
        break;
    case SDL_EVENT_MOUSE_BUTTON_UP:
        if (event.button.button < m_mouse_buttons_down.size())
            m_mouse_buttons_down[event.button.button] = false;
        break;
    case SDL_EVENT_MOUSE_MOTION:
        m_mouse_delta_x += event.motion.xrel;
        m_mouse_delta_y += event.motion.yrel;
        break;
    case SDL_EVENT_MOUSE_WHEEL:
        m_wheel_delta_x += event.wheel.x;
        m_wheel_delta_y += event.wheel.y;
        break;
    default:
        break;
    }
}

bool InputState::key_down(SDL_Scancode scancode) const
{
    if (scancode >= m_keys_down.size())
        return false;
    return m_keys_down[scancode];
}

bool InputState::key_down(StringView name) const
{
    auto key_name = MUST(String::from_utf8(name)).to_byte_string();
    auto scancode = SDL_GetScancodeFromName(key_name.characters());
    if (scancode == SDL_SCANCODE_UNKNOWN)
        return false;
    return key_down(scancode);
}

bool InputState::mouse_button_down(u8 button) const
{
    if (button >= m_mouse_buttons_down.size())
        return false;
    return m_mouse_buttons_down[button];
}

bool InputState::consume_key_press(SDL_Scancode scancode)
{
    if (scancode >= m_keys_pressed.size())
        return false;
    auto pressed = m_keys_pressed[scancode];
    m_keys_pressed[scancode] = false;
    return pressed;
}

}
