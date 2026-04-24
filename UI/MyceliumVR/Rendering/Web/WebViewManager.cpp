/*
 * MyceliumVR - a social VR platform built on the Ladybird browser engine
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "WebViewManager.h"

#include <UI/MyceliumVR/Support/Profiling.h>

#include <LibURL/Parser.h>

namespace MyceliumVR {

static u64 make_world_entity_key(WorldId world_id, EntityId entity_id)
{
    return (static_cast<u64>(world_id) << 32) | static_cast<u32>(entity_id);
}

WebViewManager::WebViewManager(bool supports_vulkan_external_images, VkDevice vulkan_device, bool defer_vulkan_paint_acks)
    : m_supports_vulkan_external_images(supports_vulkan_external_images)
    , m_vulkan_device(vulkan_device)
    , m_defer_vulkan_paint_acks(defer_vulkan_paint_acks)
{
}

void WebViewManager::sync_world(WorldId world_id, World const& world)
{
#if defined(TRACY_ENABLE)
    ZoneScopedN("Boundary/Ladybird/WebViewSync");
    TracyPlot("WebViewManager/ViewCount", static_cast<int64_t>(m_views_by_handle.size()));
#endif
    remove_stale_views(world_id, world);

    auto view = world.registry().view<Panel const>();
    for (auto entity : view) {
        auto const& panel = view.get<Panel const>(entity);
        
        u32 handle = 0;
        auto key = make_world_entity_key(world_id, entity);
        if (auto it = m_handle_by_world_entity.find(key); it != m_handle_by_world_entity.end()) {
            handle = it->value;
        } else {
            handle = register_panel(world_id, static_cast<u32>(entity), panel.url, panel.width, panel.height);
        }

        auto& managed_view = ensure_panel_view(handle, world_id, static_cast<u32>(entity), panel.url, panel.width, panel.height);
        if (managed_view.url != panel.url) {
            managed_view.url = panel.url;
            if (auto url = URL::Parser::basic_parse(managed_view.url.bytes_as_string_view()); url.has_value())
                managed_view.view->load(url.release_value());
        }
    }

    choose_active_panel(world_id, world);
}

u32 WebViewManager::register_panel(u32 world_id, u32 entity_id, String const& url, float width, float height)
{
    auto key = make_world_entity_key(world_id, static_cast<EntityId>(entity_id));
    if (auto it = m_handle_by_world_entity.find(key); it != m_handle_by_world_entity.end())
        return it->value;

    u32 handle = (world_id << 24) | (m_views_by_handle.size() + 1);
    m_handle_by_world_entity.set(key, handle);
    ensure_panel_view(handle, world_id, entity_id, url, width, height);
    return handle;
}

void WebViewManager::unregister_panel(u32 handle)
{
    auto it = m_views_by_handle.find(handle);
    if (it == m_views_by_handle.end())
        return;

    Vector<u64> keys_to_remove;
    for (auto const& entry : m_handle_by_world_entity) {
        if (entry.value == handle)
            keys_to_remove.append(entry.key);
    }
    for (auto key : keys_to_remove)
        m_handle_by_world_entity.remove(key);

    m_views_by_handle.remove(handle);
}

void WebViewManager::unload_world_panels(u32 world_id)
{
    Vector<u32> handles_to_remove;
    for (auto const& entry : m_views_by_handle) {
        if (entry.value.world_id == world_id)
            handles_to_remove.append(entry.key);
    }
    for (auto handle : handles_to_remove)
        unregister_panel(handle);
    
    if (m_active_panel_handle != 0 && !m_views_by_handle.contains(m_active_panel_handle))
        m_active_panel_handle = 0;
}

void WebViewManager::resize(int width, int height)
{
    (void)width;
    (void)height;
}

void WebViewManager::handle_sdl_event(SDL_Event const& event)
{
    (void)event;
}

bool WebViewManager::has_active_panel() const
{
    return m_active_panel_handle != 0 && m_views_by_handle.contains(m_active_panel_handle);
}

void WebViewManager::post_render()
{
    for (auto handle : m_pending_presented_vulkan_panels) {
        if (auto it = m_views_by_handle.find(handle); it != m_views_by_handle.end())
            it->value.view->acknowledge_ready_to_paint_with_trace("panel-post-render"sv);
    }
    m_pending_presented_vulkan_panels.clear();
}

ErrorOr<Vector<WebViewManager::PanelSnapshot>> WebViewManager::snapshot_dirty_panels()
{
    Vector<PanelSnapshot> snapshots;
    for (auto& it : m_views_by_handle) {
        if (it.value.view->needs_paint()) {
            if (m_supports_vulkan_external_images && m_defer_vulkan_paint_acks) {
                auto snapshot_vulkan_image = it.value.view->snapshot_vulkan_image_view();
                if (snapshot_vulkan_image.has_value()) {
                    PanelSnapshot snapshot;
                    snapshot.handle = it.key;
                    snapshot.vulkan_image = snapshot_vulkan_image->image;
                    snapshot.width = snapshot_vulkan_image->width;
                    snapshot.height = snapshot_vulkan_image->height;
                    dbgln("WebViewManager: dirty panel handle={} using zero-copy image={} size={}x{}",
                        snapshot.handle,
                        (void*)snapshot.vulkan_image,
                        snapshot.width,
                        snapshot.height);
                    snapshots.append(move(snapshot));
                    m_pending_presented_vulkan_panels.append(it.key);
                    continue;
                }
            }

            if (m_supports_vulkan_external_images && it.value.view->has_vulkan_image()) {
                PanelSnapshot snapshot;
                snapshot.handle = it.key;
                snapshot.vulkan_image = it.value.view->current_vulkan_image();
                snapshot.width = it.value.view->vulkan_image_width();
                snapshot.height = it.value.view->vulkan_image_height();
                dbgln("WebViewManager: dirty panel handle={} using current Vulkan image without deferred ack image={} size={}x{}",
                    snapshot.handle,
                    (void*)snapshot.vulkan_image,
                    snapshot.width,
                    snapshot.height);
                snapshots.append(move(snapshot));
            }
        }
    }
    return snapshots;
}

void WebViewManager::calculate_panel_pixel_size(float panel_width, float panel_height, int& width, int& height)
{
    constexpr int target_width = 1280;
    auto safe_width = panel_width > 0.01f ? panel_width : 1.0f;
    auto safe_height = panel_height > 0.01f ? panel_height : 1.0f;
    width = target_width;
    height = max(1, static_cast<int>(roundf(target_width * (safe_height / safe_width))));
}

WebViewManager::ManagedView& WebViewManager::ensure_panel_view(u32 handle, WorldId world_id, u32 entity_id, String const& url, float width, float height)
{
    auto it = m_views_by_handle.find(handle);
    if (it != m_views_by_handle.end()) {
        it->value.world_id = world_id;
        it->value.entity_id = static_cast<EntityId>(entity_id);
        int p_width = 1280;
        int p_height = 720;
        calculate_panel_pixel_size(width, height, p_width, p_height);
        if (it->value.pixel_width != p_width || it->value.pixel_height != p_height) {
            it->value.pixel_width = p_width;
            it->value.pixel_height = p_height;
            it->value.view->resize(p_width, p_height);
        }
        return it->value;
    }

    int p_width = 1280;
    int p_height = 720;
    calculate_panel_pixel_size(width, height, p_width, p_height);
    auto view = make<WebContentView>(p_width, p_height, m_supports_vulkan_external_images, m_vulkan_device);
    m_views_by_handle.set(handle, ManagedView {
        .handle = handle,
        .world_id = world_id,
        .entity_id = static_cast<EntityId>(entity_id),
        .view = move(view),
        .url = url,
        .pixel_width = p_width,
        .pixel_height = p_height,
    });
    auto& managed_view = m_views_by_handle.find(handle)->value;
    configure_managed_view(managed_view);
    if (auto parsed_url = URL::Parser::basic_parse(url.bytes_as_string_view()); parsed_url.has_value())
        managed_view.view->load(parsed_url.release_value());
    return managed_view;
}

void WebViewManager::configure_managed_view(ManagedView& managed_view)
{
    managed_view.view->set_debug_name(MUST(String::formatted("panel:{}:{}",
        managed_view.world_id,
        managed_view.handle)));
    if (m_supports_vulkan_external_images && m_defer_vulkan_paint_acks)
        managed_view.view->configure_deferred_ready_to_paint_acks(true);
}

void WebViewManager::remove_stale_views(WorldId world_id, World const& world)
{
    Vector<u32> stale_handles;
    for (auto const& it : m_views_by_handle) {
        auto const& managed_view = it.value;
        if (managed_view.world_id != world_id
            || !world.registry().valid(managed_view.entity_id)
            || !world.registry().all_of<Panel>(managed_view.entity_id)) {
            stale_handles.append(it.key);
        }
    }
    for (auto handle : stale_handles)
        unregister_panel(handle);
    
    if (m_active_panel_handle != 0 && !m_views_by_handle.contains(m_active_panel_handle))
        m_active_panel_handle = 0;
}

void WebViewManager::choose_active_panel(WorldId world_id, World const& world)
{
    if (m_active_panel_handle != 0 && m_views_by_handle.contains(m_active_panel_handle))
        return;

    auto view = world.registry().view<Panel const>();
    if (!view.empty()) {
        auto entity = *view.begin();
        auto key = make_world_entity_key(world_id, entity);
        if (auto handle = m_handle_by_world_entity.get(key); handle.has_value())
            m_active_panel_handle = handle.value();
    }
}

}
