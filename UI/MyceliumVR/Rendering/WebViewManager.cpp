/*
 * MyceliumVR - a social VR platform built on the Ladybird browser engine
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "WebViewManager.h"

#include <LibURL/Parser.h>

namespace MyceliumVR {

WebViewManager::WebViewManager() = default;

void WebViewManager::sync_world(World const& world)
{
    remove_stale_views(world);

    for (auto const& entity : world.entities()) {
        if (!entity.alive || !entity.panel.has_value())
            continue;
        auto& managed_view = ensure_panel_view(entity.id, *entity.panel);
        if (managed_view.url != entity.panel->url) {
            managed_view.url = entity.panel->url;
            if (auto url = URL::Parser::basic_parse(managed_view.url.bytes_as_string_view()); url.has_value())
                managed_view.view->load(url.release_value());
        }
    }

    choose_active_panel(world);
}

void WebViewManager::resize(int, int)
{
}

void WebViewManager::handle_sdl_event(SDL_Event const& event)
{
    (void)event;
}

bool WebViewManager::has_active_panel() const
{
    return m_active_panel != 0 && m_views.contains(m_active_panel);
}

ErrorOr<Optional<WebContentBitmapView>> WebViewManager::snapshot_active_panel_if_needed()
{
    auto it = m_views.find(m_active_panel);
    if (it == m_views.end())
        return Optional<WebContentBitmapView> {};
    if (!it->value.view->needs_paint())
        return Optional<WebContentBitmapView> {};
    return TRY(it->value.view->snapshot_bitmap_view());
}

void WebViewManager::calculate_panel_pixel_size(Panel const& panel, int& width, int& height)
{
    constexpr int target_width = 1280;
    auto safe_width = panel.width > 0.01f ? panel.width : 1.0f;
    auto safe_height = panel.height > 0.01f ? panel.height : 1.0f;
    width = target_width;
    height = max(1, static_cast<int>(roundf(target_width * (safe_height / safe_width))));
}

WebViewManager::ManagedView& WebViewManager::ensure_panel_view(EntityId entity_id, Panel const& panel)
{
    auto it = m_views.find(entity_id);
    if (it != m_views.end()) {
        int width = 1280;
        int height = 720;
        calculate_panel_pixel_size(panel, width, height);
        if (it->value.pixel_width != width || it->value.pixel_height != height) {
            it->value.pixel_width = width;
            it->value.pixel_height = height;
            it->value.view->resize(width, height);
        }
        return it->value;
    }

    int width = 1280;
    int height = 720;
    calculate_panel_pixel_size(panel, width, height);
    auto view = make<WebContentView>(width, height);
    if (auto url = URL::Parser::basic_parse(panel.url.bytes_as_string_view()); url.has_value())
        view->load(url.release_value());
    m_views.set(entity_id, ManagedView {
        .view = move(view),
        .url = panel.url,
        .pixel_width = width,
        .pixel_height = height,
    });
    return m_views.find(entity_id)->value;
}

void WebViewManager::remove_stale_views(World const& world)
{
    Vector<EntityId> to_remove;
    for (auto const& it : m_views) {
        auto const* entity = world.entity(it.key);
        if (!entity || !entity->alive || !entity->panel.has_value())
            to_remove.append(it.key);
    }
    for (auto entity_id : to_remove)
        m_views.remove(entity_id);
    if (m_active_panel != 0 && !m_views.contains(m_active_panel))
        m_active_panel = 0;
}

void WebViewManager::choose_active_panel(World const& world)
{
    if (m_active_panel != 0 && m_views.contains(m_active_panel))
        return;

    m_active_panel = 0;
    for (auto const& entity : world.entities()) {
        if (!entity.alive || !entity.panel.has_value())
            continue;
        m_active_panel = entity.id;
        break;
    }
}

}
