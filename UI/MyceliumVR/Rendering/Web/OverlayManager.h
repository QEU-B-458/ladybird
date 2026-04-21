/*
 * MyceliumVR - a social VR platform built on the Ladybird browser engine
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include "../Backend/VulkanRenderer.h"
#include "WebContentView.h"
#include "../../Scripting/BridgeBackend.h"

#include <AK/Error.h>
#include <AK/Function.h>
#include <AK/OwnPtr.h>
#include <AK/String.h>
#include <AK/Vector.h>
#include <SDL3/SDL_events.h>

namespace MyceliumVR {

class OverlayManager {
public:
    struct Stats {
        double fps { 0.0 };
        double frame_time_ms { 0.0 };
        size_t entity_count { 0 };
        u32 draw_calls { 0 };
        u32 triangle_count { 0 };
        VulkanRenderer::CameraState camera_state;
    };

    OverlayManager();

    ErrorOr<void> initialize(int width, int height, bool supports_vulkan_external_images = false, VkDevice vulkan_device = VK_NULL_HANDLE);
    void handle_sdl_event(SDL_Event const&);
    void resize(int width, int height);
    void update_stats(Stats const&);

    // Queue a log entry to be forwarded to the UI console on the next update_stats() call.
    void push_log(StringView level, StringView source, StringView message);

    void toggle_visibility();
    void toggle_focus();

    void push_hierarchy(Vector<EntityHierarchyEntry> const&);
    void notify_selection_changed(EntityId);
    void push_component_update(EntityComponentSnapshot const&);
    void update_render_timings(Vector<VulkanRenderer::PassTiming> const&);
    void push_bridge_functions();
    void push_assets(Vector<String> const& mount_prefixes);

    // Called when the overlay UI selects an entity by clicking the hierarchy panel.
    void set_entity_select_callback(Function<void(u32)> cb) { m_on_entity_select = move(cb); }

    bool is_initialized()  const { return m_view; }
    bool is_visible()      const { return m_visible; }
    bool is_focused()      const { return m_focused; }
    bool has_ever_painted() const { return m_has_ever_painted; }

    bool should_route_input(SDL_Event const&) const;
    ErrorOr<Optional<WebContentBitmapView>> snapshot_overlay_view();

    // Zero-copy Vulkan accessors (valid only when supports_vulkan_external_images was true).
    bool has_overlay_vulkan_image() const;
    VkImage overlay_vulkan_image() const;
    u32 overlay_vulkan_width() const;
    u32 overlay_vulkan_height() const;

private:
    static bool is_input_event(SDL_Event const&);

    struct LogEntry {
        String level;
        String source;
        String message;
    };

    OwnPtr<WebContentView> m_view;
    Function<void(u32)> m_on_entity_select;
    bool m_visible { true };
    bool m_focused { false };
    bool m_has_ever_painted { false };
    Vector<LogEntry> m_pending_logs;
    size_t m_frames_since_init { 0 };
};

}
