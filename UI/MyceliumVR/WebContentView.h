/*
 * MyceliumVR - a social VR platform built on the Ladybird browser engine
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <LibWebView/ViewImplementation.h>

#include <SDL3/SDL.h>

namespace MyceliumVR {

class WebContentView final : public WebView::ViewImplementation {
public:
    WebContentView(SDL_Renderer* renderer, int width, int height);
    virtual ~WebContentView() override;

    // Paint the current Ladybird frame into the SDL renderer
    void paint(SDL_Renderer* renderer);

    void resize(int width, int height);

    // Forward SDL input events into Ladybird
    void handle_sdl_event(SDL_Event const&);

private:
    // ^WebView::ViewImplementation - required pure virtuals
    virtual Web::DevicePixelSize viewport_size() const override;
    virtual Gfx::IntPoint to_content_position(Gfx::IntPoint) const override;
    virtual Gfx::IntPoint to_widget_position(Gfx::IntPoint) const override;

    int m_width { 1280 };
    int m_height { 720 };

    SDL_Texture* m_texture { nullptr };
    SDL_Renderer* m_renderer { nullptr };
    bool m_needs_paint { false };
    u32 m_frames_waiting_for_first_paint { 0 };
    Web::UIEvents::MouseButton m_pressed_mouse_buttons { Web::UIEvents::MouseButton::None };
};

}
