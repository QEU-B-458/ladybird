/*
 * MyceliumVR - a social VR platform built on the Ladybird browser engine
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include "../Backend/VulkanRenderer.h"
#include "WebContentView.h"
#include "../../Networking/ControlBusClient.h"
#include "../../Scripting/BridgeBackend.h"

#include <AK/Error.h>
#include <AK/Function.h>
#include <AK/JsonObject.h>
#include <AK/OwnPtr.h>
#include <AK/String.h>
#include <AK/Vector.h>
#include <LibCore/Timer.h>
#include <SDL3/SDL_events.h>
#include <entt/entt.hpp>

namespace MyceliumVR {

class Engine;
class WorldManagementSystem;
class VirtualFileSystem;

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

    ErrorOr<void> initialize(
        int width, int height,
        bool supports_vulkan_external_images,
        VkDevice vulkan_device,
        Engine&,
        WorldManagementSystem&,
        VirtualFileSystem const* = nullptr);
    void connect_to_world(u16 port, StringView token);
    void notify_world_faulted(u32 world_id, StringView reason);
    void refresh_scene();
    void tick(double fps, double delta_time_ms);
    void post_render();

    void handle_sdl_event(SDL_Event const&);
    void resize(int width, int height);

    void toggle_visibility();
    void toggle_focus();

    bool is_initialized()  const { return m_view; }
    bool is_visible()      const { return m_visible; }
    bool is_focused()      const { return m_focused; }
    bool has_ever_painted() const { return m_has_ever_painted; }

    bool should_route_input(SDL_Event const&) const;
    // Zero-copy Vulkan accessors (valid only when supports_vulkan_external_images was true).
    bool has_overlay_vulkan_image() const;
    VkImage overlay_vulkan_image() const;
    u32 overlay_vulkan_width() const;
    u32 overlay_vulkan_height() const;
    bool supports_overlay_vulkan_images() const { return m_supports_vulkan_external_images; }

private:
    static bool is_input_event(SDL_Event const&);
    void update_stats(Stats const&);
    void push_log(StringView level, StringView source, StringView message);
    void push_hierarchy(Vector<EntityHierarchyEntry> const&);
    void notify_selection_changed(EntityId);
    void push_component_update(EntityComponentSnapshot const&);
    void update_render_timings(Vector<VulkanRenderer::PassTiming> const&);
    void push_bridge_functions();
    void push_assets(Vector<String> const& mount_prefixes);
    void push_worlds(WorldManagementSystem const&);
    void handle_control_bus_message(JsonObject const&);

    struct LogEntry {
        String level;
        String source;
        String message;
    };
    struct PublishedOverlayImage {
        VkImage image { VK_NULL_HANDLE };
        u32 width { 0 };
        u32 height { 0 };
    };

    Engine* m_engine { nullptr };
    WorldManagementSystem* m_world_management_system { nullptr };
    OwnPtr<ControlBusClient> m_control_bus_client;
    RefPtr<Core::Timer> m_retry_timer;
    OwnPtr<WebContentView> m_view;
    Vector<String> m_vfs_mounts;
    EntityId m_selected_entity { entt::null };
    u16 m_pending_port { 0 };
    String m_pending_token;
    int m_retry_count { 0 };
    bool m_visible { true };
    bool m_focused { false };
    bool m_has_ever_painted { false };
    bool m_startup_pushed { false };
    bool m_supports_vulkan_external_images { false };
    bool m_overlay_needs_ack { false };
    PublishedOverlayImage m_published_overlay_image;
    Vector<LogEntry> m_pending_logs;
    size_t m_frames_since_init { 0 };
    u32 m_last_entity_count { 0 };
    void publish_current_vulkan_image();
    void clear_published_vulkan_image();
};

}
