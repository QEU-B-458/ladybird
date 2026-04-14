/*
 * MyceliumVR - a social VR platform built on the Ladybird browser engine
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "OverlayManager.h"

#include <LibCore/File.h>

namespace MyceliumVR {

OverlayManager::OverlayManager() = default;

ErrorOr<void> OverlayManager::initialize(int width, int height)
{
    auto overlay_file = TRY(Core::File::open("UI/MyceliumVR/ui/overlay.html"sv, Core::File::OpenMode::Read));
    auto overlay_html = TRY(overlay_file->read_until_eof());
    m_overlay_html = TRY(String::from_utf8(overlay_html));

    m_view = make<WebContentView>(width, height);
    m_view->debug_request("transparent-top-level-canvas"sv, "on"sv);
    m_view->load_html(m_overlay_html);
    return {};
}

void OverlayManager::handle_sdl_event(SDL_Event const& event)
{
    if (!m_view || !m_visible || !m_focused || !is_input_event(event))
        return;
    m_view->handle_sdl_event(event);
}

void OverlayManager::resize(int width, int height)
{
    if (m_view)
        m_view->resize(width, height);
}

void OverlayManager::update_stats(Stats const& stats)
{
    if (!m_view || !m_visible)
        return;

    m_view->run_javascript(MUST(String::formatted(
        "window.__myceliumOverlayUpdate && window.__myceliumOverlayUpdate({:.2f}, {:.3f}, {}, {:.3f}, {:.3f}, {:.3f}, {:.3f}, {:.3f}, {});",
        stats.fps,
        stats.frame_time_ms,
        stats.entity_count,
        stats.camera_state.position[0],
        stats.camera_state.position[1],
        stats.camera_state.position[2],
        stats.camera_state.yaw_degrees,
        stats.camera_state.pitch_degrees,
        m_focused ? "true" : "false")));
}

void OverlayManager::toggle_visibility()
{
    m_visible = !m_visible;
    if (!m_visible)
        m_focused = false;
}

void OverlayManager::toggle_focus()
{
    if (!m_visible)
        return;
    m_focused = !m_focused;
}

bool OverlayManager::should_route_input(SDL_Event const& event) const
{
    return m_visible && m_focused && is_input_event(event);
}

ErrorOr<Optional<WebContentBitmapView>> OverlayManager::snapshot_overlay_view()
{
    if (!m_view || !m_visible)
        return Optional<WebContentBitmapView> {};

    auto snapshot = TRY(m_view->snapshot_bitmap_view_for_current_viewport());
    if (!snapshot.has_value())
        return Optional<WebContentBitmapView> {};

    return snapshot;
}

bool OverlayManager::is_input_event(SDL_Event const& event)
{
    return event.type == SDL_EVENT_MOUSE_BUTTON_DOWN
        || event.type == SDL_EVENT_MOUSE_BUTTON_UP
        || event.type == SDL_EVENT_MOUSE_MOTION
        || event.type == SDL_EVENT_MOUSE_WHEEL
        || event.type == SDL_EVENT_KEY_DOWN
        || event.type == SDL_EVENT_KEY_UP
        || event.type == SDL_EVENT_TEXT_INPUT;
}

}
