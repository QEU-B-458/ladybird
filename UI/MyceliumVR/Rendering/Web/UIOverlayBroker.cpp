/*
 * MyceliumVR - a social VR platform built on the Ladybird browser engine
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "UIOverlayBroker.h"

#include "../../Engine/Engine.h"
#include "../../Support/VirtualFileSystem.h"
#include "../../World/WorldManagementSystem.h"
#include "../../World/World.h"

namespace MyceliumVR {

ErrorOr<void> UIOverlayBroker::initialize(
    int width, int height,
    bool supports_vulkan_external_images,
    VkDevice vulkan_device,
    Engine& engine,
    WorldManagementSystem& world_management_system,
    VirtualFileSystem const* vfs)
{
    m_engine = &engine;
    m_world_management_system = &world_management_system;
    if (vfs)
        m_vfs_mounts = vfs->mount_prefixes();

    m_overlay = make<OverlayManager>();
    TRY(m_overlay->initialize(width, height, supports_vulkan_external_images, vulkan_device));

    auto& bridge_backend = m_world_management_system->active_bridge_backend();

    // JS World.log() → overlay console.
    bridge_backend.set_log_callback([this](StringView level, StringView source, StringView message) {
        push_log(level, source, message);
    });

    // World script changes selection → notify overlay + push component data.
    bridge_backend.set_selection_changed_callback([this](EntityId entity) {
        m_overlay->notify_selection_changed(entity);
        auto snap = m_world_management_system->active_bridge_backend().get_entity_components(entity);
        if (snap.has_value())
            m_overlay->push_component_update(*snap);
    });

    // Overlay hierarchy click → ECS selection (fires the callback above).
    m_overlay->set_entity_select_callback([this](u32 id) {
        m_world_management_system->active_bridge_backend().set_selected_entity(static_cast<EntityId>(id));
    });

    return {};
}

void UIOverlayBroker::tick(double fps, double delta_time_ms)
{
    if (!m_overlay || !m_overlay->is_initialized())
        return;

    if (!m_startup_pushed && m_overlay->has_ever_painted()) {
        m_overlay->push_bridge_functions();
        m_overlay->push_assets(m_vfs_mounts);
        m_startup_pushed = true;
    }

    if (m_world_management_system->active_world().layout_dirty())
        m_overlay->push_hierarchy(m_world_management_system->active_bridge_backend().get_entity_hierarchy());

    m_overlay->update_stats({
        .fps            = fps,
        .frame_time_ms  = delta_time_ms,
        .entity_count   = m_world_management_system->active_world().alive_entity_count(),
        .draw_calls     = m_engine->renderer().last_frame_draw_calls(),
        .triangle_count = m_engine->renderer().last_frame_triangle_count(),
        .camera_state   = m_engine->camera_state(),
    });
}

void UIOverlayBroker::post_render()
{
    if (!m_overlay || !m_overlay->is_initialized())
        return;
    m_overlay->update_render_timings(m_engine->renderer().last_frame_timings());
}

void UIOverlayBroker::push_log(StringView level, StringView source, StringView message)
{
    if (m_overlay)
        m_overlay->push_log(level, source, message);
}

void UIOverlayBroker::handle_sdl_event(SDL_Event const& event)
{
    if (m_overlay)
        m_overlay->handle_sdl_event(event);
}

void UIOverlayBroker::resize(int width, int height)
{
    if (m_overlay)
        m_overlay->resize(width, height);
}

void UIOverlayBroker::toggle_visibility()
{
    if (m_overlay)
        m_overlay->toggle_visibility();
}

void UIOverlayBroker::toggle_focus()
{
    if (m_overlay)
        m_overlay->toggle_focus();
}

bool UIOverlayBroker::should_route_input(SDL_Event const& event) const
{
    return m_overlay && m_overlay->should_route_input(event);
}

ErrorOr<Optional<WebContentBitmapView>> UIOverlayBroker::snapshot_overlay_view()
{
    if (!m_overlay)
        return Optional<WebContentBitmapView> {};
    return m_overlay->snapshot_overlay_view();
}

bool UIOverlayBroker::has_overlay_vulkan_image() const
{
    return m_overlay && m_overlay->has_overlay_vulkan_image();
}

VkImage UIOverlayBroker::overlay_vulkan_image() const
{
    return m_overlay ? m_overlay->overlay_vulkan_image() : VK_NULL_HANDLE;
}

u32 UIOverlayBroker::overlay_vulkan_width() const
{
    return m_overlay ? m_overlay->overlay_vulkan_width() : 0;
}

u32 UIOverlayBroker::overlay_vulkan_height() const
{
    return m_overlay ? m_overlay->overlay_vulkan_height() : 0;
}

}
