/*
 * MyceliumVR - a social VR platform built on the Ladybird browser engine
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include "WebContentView.h"

#include "../../World/World.h"

#include <AK/Error.h>
#include <AK/HashMap.h>
#include <AK/OwnPtr.h>
#include <SDL3/SDL_events.h>

namespace MyceliumVR {

using WorldId = u32;

class WebViewManager {
public:
    WebViewManager(bool supports_vulkan_external_images = false, VkDevice vulkan_device = VK_NULL_HANDLE, bool defer_vulkan_paint_acks = false);

    void sync_world(WorldId world_id, World const&);
    void resize(int width, int height);
    void handle_sdl_event(SDL_Event const&);

    u32 register_panel(u32 world_id, u32 entity_id, String const& url, float width, float height);
    void unregister_panel(u32 handle);
    void unload_world_panels(u32 world_id);

    bool has_active_panel() const;
    void post_render();
    bool has_pending_presented_vulkan_panels() const { return !m_pending_presented_vulkan_panels.is_empty(); }

    // New API for multi-panel support
    struct PanelSnapshot {
        u32 handle;
        u32 width { 0 };
        u32 height { 0 };
        VkImage vulkan_image { VK_NULL_HANDLE };
    };
    ErrorOr<Vector<PanelSnapshot>> snapshot_dirty_panels();

private:
    struct ManagedView {
        u32 handle { 0 };
        WorldId world_id { 0 };
        EntityId entity_id { 0 };
        OwnPtr<WebContentView> view;
        String url;
        int pixel_width { 1280 };
        int pixel_height { 720 };
    };

    static void calculate_panel_pixel_size(float panel_width, float panel_height, int& width, int& height);

    ManagedView& ensure_panel_view(u32 handle, WorldId world_id, u32 entity_id, String const& url, float width, float height);
    void configure_managed_view(ManagedView&);
    void remove_stale_views(WorldId world_id, World const&);
    void choose_active_panel(WorldId world_id, World const&);

    HashMap<u32, ManagedView> m_views_by_handle;
    HashMap<u64, u32> m_handle_by_world_entity;
    Vector<u32> m_pending_presented_vulkan_panels;
    u32 m_active_panel_handle { 0 };

    bool m_supports_vulkan_external_images { false };
    VkDevice m_vulkan_device { VK_NULL_HANDLE };
    bool m_defer_vulkan_paint_acks { false };
};

}
