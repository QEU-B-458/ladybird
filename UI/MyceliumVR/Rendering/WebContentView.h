/*
 * MyceliumVR - a social VR platform built on the Ladybird browser engine
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <AK/Error.h>
#include <AK/Optional.h>
#include <AK/Vector.h>
#include <LibWebView/ViewImplementation.h>
#include <SDL3/SDL_events.h>

namespace Gfx {
class Bitmap;
}

namespace MyceliumVR {

struct WebContentBitmapSnapshot {
    int width { 0 };
    int height { 0 };
    int visible_width { 0 };
    int visible_height { 0 };
    Vector<u8> pixels;
};

struct WebContentBitmapView {
    int width { 0 };
    int height { 0 };
    int visible_width { 0 };
    int visible_height { 0 };
    Gfx::Bitmap const* bitmap { nullptr };
};

class WebContentView final : public WebView::ViewImplementation {
public:
    WebContentView(int width, int height);
    virtual ~WebContentView() override;

    void resize(int width, int height);

    // Forward SDL input events into Ladybird
    void handle_sdl_event(SDL_Event const&);

    bool needs_paint() const;
    ErrorOr<Optional<WebContentBitmapView>> snapshot_bitmap_view();
    ErrorOr<Optional<WebContentBitmapView>> snapshot_bitmap_view_for_current_viewport();
    ErrorOr<Optional<WebContentBitmapSnapshot>> snapshot_bitmap();
    ErrorOr<Optional<WebContentBitmapSnapshot>> snapshot_bitmap_for_current_viewport();

private:
    // ^WebView::ViewImplementation - required pure virtuals
    virtual Web::DevicePixelSize viewport_size() const override;
    virtual Gfx::IntPoint to_content_position(Gfx::IntPoint) const override;
    virtual Gfx::IntPoint to_widget_position(Gfx::IntPoint) const override;

    int m_width { 1280 };
    int m_height { 720 };
    bool m_needs_paint { false };
    u32 m_frames_waiting_for_first_paint { 0 };
    Web::UIEvents::MouseButton m_pressed_mouse_buttons { Web::UIEvents::MouseButton::None };
};

}
