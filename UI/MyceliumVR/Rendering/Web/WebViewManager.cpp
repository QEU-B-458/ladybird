/*
 * MyceliumVR - a social VR platform built on the Ladybird browser engine
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "WebViewManager.h"

#if defined(TRACY_ENABLE)
#    include <tracy/Tracy.hpp>
#endif

#include <LibURL/Parser.h>

namespace MyceliumVR {

WebViewManager::WebViewManager() = default;

void WebViewManager::sync_world(World const& world)
{
#if defined(TRACY_ENABLE)
    ZoneScopedN("Boundary/Ladybird/WebViewSync");
    TracyPlot("WebViewManager/ViewCount", static_cast<int64_t>(m_views.size()));
    TracyPlot("WebViewManager/HasActivePanel", static_cast<int64_t>(has_active_panel() ? 1 : 0));
#endif
    remove_stale_views(world);

    auto view = world.registry().view<Panel const>();
    for (auto entity : view) {
        auto const& panel = view.get<Panel const>(entity);
        auto& managed_view = ensure_panel_view(entity, panel);
        if (managed_view.url != panel.url) {
            managed_view.url = panel.url;
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
    return m_active_panel != entt::null && m_views.contains(m_active_panel);
}

ErrorOr<Optional<WebContentBitmapView>> WebViewManager::snapshot_active_panel_if_needed()
{
#if defined(TRACY_ENABLE)
    ZoneScopedN("Boundary/Ladybird/Snapshot");
#endif
    auto it = m_views.find(m_active_panel);
    if (it == m_views.end())
        return Optional<WebContentBitmapView> {};
    if (!it->value.view->needs_paint())
        return Optional<WebContentBitmapView> {};
    auto snapshot = TRY(it->value.view->snapshot_bitmap_view());
#if defined(TRACY_ENABLE)
    if (snapshot.has_value()) {
        auto bytes = static_cast<int64_t>(snapshot->width) * snapshot->height * 4;
        TracyPlot("WebViewManager/PanelSnapshotWidth", static_cast<int64_t>(snapshot->width));
        TracyPlot("WebViewManager/PanelSnapshotHeight", static_cast<int64_t>(snapshot->height));
        TracyPlot("WebViewManager/PanelSnapshotBytes", bytes);
    } else {
        TracyPlot("WebViewManager/PanelSnapshotBytes", static_cast<int64_t>(0));
    }
#endif
    return snapshot;
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
#if defined(TRACY_ENABLE)
            ZoneScopedN("Boundary/Ladybird/WebViewResize");
            TracyPlot("WebViewManager/PanelResizeWidth", static_cast<int64_t>(width));
            TracyPlot("WebViewManager/PanelResizeHeight", static_cast<int64_t>(height));
#endif
            it->value.pixel_width = width;
            it->value.pixel_height = height;
            it->value.view->resize(width, height);
        }
        return it->value;
    }

    int width = 1280;
    int height = 720;
    calculate_panel_pixel_size(panel, width, height);
#if defined(TRACY_ENABLE)
    ZoneScopedN("Boundary/Ladybird/WebViewCreate");
    TracyPlot("WebViewManager/CreatedPanelWidth", static_cast<int64_t>(width));
    TracyPlot("WebViewManager/CreatedPanelHeight", static_cast<int64_t>(height));
#endif
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
#if defined(TRACY_ENABLE)
    ZoneScopedN("Boundary/Ladybird/RemoveStaleViews");
#endif
    Vector<EntityId> to_remove;
    for (auto const& it : m_views) {
        if (!world.registry().valid(it.key) || !world.registry().all_of<Panel>(it.key))
            to_remove.append(it.key);
    }
    for (auto entity_id : to_remove)
        m_views.remove(entity_id);
#if defined(TRACY_ENABLE)
    TracyPlot("WebViewManager/RemovedViews", static_cast<int64_t>(to_remove.size()));
    TracyPlot("WebViewManager/ViewCount", static_cast<int64_t>(m_views.size()));
#endif
    if (m_active_panel != entt::null && !m_views.contains(m_active_panel))
        m_active_panel = entt::null;
}

void WebViewManager::choose_active_panel(World const& world)
{
    if (m_active_panel != entt::null && m_views.contains(m_active_panel))
        return;

    m_active_panel = entt::null;
    auto view = world.registry().view<Panel const>();
    if (!view.empty()) {
        m_active_panel = *view.begin();
    }
}

}
