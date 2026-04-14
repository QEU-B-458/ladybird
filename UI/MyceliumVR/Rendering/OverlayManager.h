/*
 * MyceliumVR - a social VR platform built on the Ladybird browser engine
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include "VulkanRenderer.h"
#include "WebContentView.h"

#include <AK/Error.h>
#include <AK/OwnPtr.h>
#include <AK/String.h>
#include <SDL3/SDL_events.h>

namespace MyceliumVR {

class OverlayManager {
public:
    struct Stats {
        double fps { 0.0 };
        double frame_time_ms { 0.0 };
        size_t entity_count { 0 };
        VulkanRenderer::CameraState camera_state;
    };

    OverlayManager();

    ErrorOr<void> initialize(int width, int height);
    void handle_sdl_event(SDL_Event const&);
    void resize(int width, int height);
    void update_stats(Stats const&);

    void toggle_visibility();
    void toggle_focus();

    bool is_initialized() const { return m_view; }
    bool is_visible() const { return m_visible; }
    bool is_focused() const { return m_focused; }

    bool should_route_input(SDL_Event const&) const;
    ErrorOr<Optional<WebContentBitmapView>> snapshot_overlay_view();

private:
    static bool is_input_event(SDL_Event const&);

    OwnPtr<WebContentView> m_view;
    String m_overlay_html;
    bool m_visible { true };
    bool m_focused { false };
};

}
