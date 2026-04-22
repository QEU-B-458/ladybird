/*
 * MyceliumVR - a social VR platform built on the Ladybird browser engine
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include "OverlayManager.h"

#include <AK/Error.h>
#include <AK/OwnPtr.h>
#include <AK/String.h>
#include <AK/Vector.h>
#include <SDL3/SDL_events.h>

namespace MyceliumVR {

class Engine;
class VirtualFileSystem;
class WorldManagementSystem;
class ControlBusClient;

// Aggregates all overlay data sources and owns the OverlayManager.
// main.cpp interacts only with this class — never with OverlayManager directly.
//
// Frame contract:
//   broker.tick(fps, delta_ms)   — before engine.render()  (stats + hierarchy)
//   engine.render()
//   broker.post_render()         — after  engine.render()  (GPU timings)
class UIOverlayBroker {
public:
    UIOverlayBroker();
    ~UIOverlayBroker();

    ErrorOr<void> initialize(
        int width, int height,
        bool supports_vulkan_external_images,
        VkDevice vulkan_device,
        Engine& engine,
        WorldManagementSystem& world_management_system,
        VirtualFileSystem const* vfs = nullptr);

    void connect_to_world(u16 port, StringView token);

    void tick(double fps, double delta_time_ms);
    void post_render();

    void push_log(StringView level, StringView source, StringView message);

    void handle_sdl_event(SDL_Event const&);
    void resize(int width, int height);
    void toggle_visibility();
    void toggle_focus();
    bool should_route_input(SDL_Event const&) const;

    bool is_initialized() const { return m_overlay && m_overlay->is_initialized(); }
    bool is_visible()    const { return m_overlay && m_overlay->is_visible(); }
    bool is_focused()    const { return m_overlay && m_overlay->is_focused(); }

    ErrorOr<Optional<WebContentBitmapView>> snapshot_overlay_view();
    bool   has_overlay_vulkan_image() const;
    VkImage overlay_vulkan_image()    const;
    u32    overlay_vulkan_width()     const;
    u32    overlay_vulkan_height()    const;

private:
    void handle_control_bus_message(JsonObject const&);

    OwnPtr<OverlayManager> m_overlay;
    OwnPtr<ControlBusClient> m_control_bus_client;
    Engine*        m_engine         { nullptr };
    WorldManagementSystem* m_world_management_system { nullptr };
    Vector<String> m_vfs_mounts;
    EntityId       m_selected_entity { entt::null };
    bool           m_startup_pushed { false };
};

}
