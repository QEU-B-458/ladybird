/*
 * MyceliumVR - a social VR platform built on the Ladybird browser engine
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "UIOverlayBroker.h"

#include "../../Engine/Engine.h"
#include "../../Networking/ControlBusClient.h"
#include "../../Support/VirtualFileSystem.h"
#include "../../World/WorldManagementSystem.h"
#include "../../World/World.h"

namespace MyceliumVR {

UIOverlayBroker::UIOverlayBroker() = default;
UIOverlayBroker::~UIOverlayBroker() = default;

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

    // Overlay hierarchy click → ControlBus request.
    m_overlay->set_entity_select_callback([this](u32 id) {
        if (m_control_bus_client) {
            JsonObject params;
            params.set("entity_id"sv, id);
            (void)m_control_bus_client->send_request("entities.select"sv, params);
        }
    });

    return {};
}

void UIOverlayBroker::connect_to_world(u16 port, StringView token)
{
    m_control_bus_client = make<ControlBusClient>();
    m_control_bus_client->set_message_callback([this](JsonObject const& message) {
        handle_control_bus_message(message);
    });

    if (auto result = m_control_bus_client->connect(port, token); result.is_error()) {
        warnln("UIOverlayBroker: Failed to connect to world ControlBus: {}", result.error());
        return;
    }

    // Subscribe to everything
    JsonObject params;
    JsonArray topics;
    (void)topics.append("*"_string);
    params.set("topics"sv, move(topics));
    (void)m_control_bus_client->send_request("events.subscribe"sv, params);

    // Initial hierarchy
    (void)m_control_bus_client->send_request("entities.list"sv);
}

void UIOverlayBroker::tick(double fps, double delta_time_ms)
{
    if (!m_overlay || !m_overlay->is_initialized())
        return;

    if (m_control_bus_client)
        m_control_bus_client->poll();

    if (!m_startup_pushed && m_overlay->has_ever_painted()) {
        m_overlay->push_bridge_functions();
        m_overlay->push_assets(m_vfs_mounts);
        m_startup_pushed = true;
    }

    u32 entity_count = 0;
    auto* runtime = m_world_management_system->foreground_runtime();
    if (runtime) {
        entity_count = runtime->world().alive_entity_count();
        if (runtime->world().layout_dirty()) {
             (void)m_control_bus_client->send_request("entities.list"sv);
        }
        if (m_selected_entity != entt::null && (runtime->world().transform_dirty() || runtime->world().layout_dirty())) {
            JsonObject params;
            params.set("entity_id"sv, static_cast<u32>(m_selected_entity));
            (void)m_control_bus_client->send_request("entities.inspect"sv, params);
        }
    }

    m_overlay->update_stats({
        .fps            = fps,
        .frame_time_ms  = delta_time_ms,
        .entity_count   = entity_count,
        .draw_calls     = m_engine->renderer().last_frame_draw_calls(),
        .triangle_count = m_engine->renderer().last_frame_triangle_count(),
        .camera_state   = m_engine->camera_state(),
    });
}

void UIOverlayBroker::handle_control_bus_message(JsonObject const& message)
{
    auto type = message.get_string("type"sv).value_or(""_string);
    if (type == "event"sv) {
        auto event = message.get_string("event"sv).value_or(""_string);
        auto data = message.get_object("data"sv);
        if (event == "log.entry"sv && data.has_value()) {
            auto level = data->get_string("level"sv).value_or(""_string);
            auto source = data->get_string("source"sv).value_or(""_string);
            auto msg = data->get_string("message"sv).value_or(""_string);
            push_log(level, source, msg);
        } else if (event == "entity.selected"sv && data.has_value()) {
            auto entity_id = data->get_u32("entity_id"sv).value_or(0);
            m_selected_entity = static_cast<EntityId>(entity_id);
            m_overlay->notify_selection_changed(m_selected_entity);
            
            JsonObject params;
            params.set("entity_id"sv, entity_id);
            (void)m_control_bus_client->send_request("entities.inspect"sv, params);
        }
    } else if (type == "response"sv) {
        auto ok = message.get_bool("ok"sv).value_or(false);
        if (!ok) return;

        auto result = message.get_object("result"sv);
        if (!result.has_value()) return;

        if (result->has("entities"sv)) {
            auto entities = result->get_array("entities"sv);
            if (entities.has_value()) {
                Vector<EntityHierarchyEntry> hierarchy;
                for (auto const& val : entities->values()) {
                    auto obj = val.as_object();
                    EntityHierarchyEntry entry;
                    entry.id = static_cast<EntityId>(obj.get_u32("entity_id"sv).value_or(0));
                    entry.parent_id = obj.has("parent_id"sv) ? static_cast<EntityId>(obj.get_u32("parent_id"sv).value()) : entt::null;
                    entry.name = obj.get_string("name"sv).value_or(""_string);
                    entry.kind = obj.get_string("kind"sv).value_or(""_string);
                    entry.alpha_mode = obj.get_u32("alpha_mode"sv).value_or(0);
                    entry.is_static = obj.get_bool("is_static"sv).value_or(false);
                    entry.depth = obj.get_u32("depth"sv).value_or(0);
                    hierarchy.append(move(entry));
                }
                m_overlay->push_hierarchy(hierarchy);
            }
        } else if (result->has("attached_components"sv)) {
            EntityComponentSnapshot snap;
            snap.id = static_cast<EntityId>(result->get_u32("id"sv).value_or(0));
            snap.name = result->get_string("name"sv).value_or(""_string);
            snap.has_mesh_renderer = result->get_bool("has_mesh_renderer"sv).value_or(false);
            snap.mesh = result->get_string("mesh"sv).value_or(""_string);
            snap.material = result->get_string("material"sv).value_or(""_string);
            snap.normal_map = result->get_string("normal_map"sv).value_or(""_string);
            snap.has_panel = result->get_bool("has_panel"sv).value_or(false);
            snap.panel_url = result->get_string("panel_url"sv).value_or(""_string);
            snap.panel_width = result->get_float_with_precision_loss("panel_width"sv).value_or(0);
            snap.panel_height = result->get_float_with_precision_loss("panel_height"sv).value_or(0);
            snap.has_cull_override = result->get_bool("has_cull_override"sv).value_or(false);
            snap.cull_mode = static_cast<CullOverride::Mode>(result->get_u32("cull_mode"sv).value_or(0));
            snap.is_static = result->get_bool("is_static"sv).value_or(false);
            snap.alpha_blend = result->get_bool("alpha_blend"sv).value_or(false);
            snap.alpha_clip = result->get_bool("alpha_clip"sv).value_or(false);
            snap.alpha_hash = result->get_bool("alpha_hash"sv).value_or(false);
            
            auto components = result->get_array("attached_components"sv);
            if (components.has_value()) {
                for (auto const& comp : components->values())
                    snap.attached_components.append(comp.as_string());
            }

            auto transform = result->get_object("transform"sv);
            if (transform.has_value()) {
                snap.position[0] = transform->get_float_with_precision_loss("px"sv).value_or(0);
                snap.position[1] = transform->get_float_with_precision_loss("py"sv).value_or(0);
                snap.position[2] = transform->get_float_with_precision_loss("pz"sv).value_or(0);
                snap.rotation[0] = transform->get_float_with_precision_loss("qx"sv).value_or(0);
                snap.rotation[1] = transform->get_float_with_precision_loss("qy"sv).value_or(0);
                snap.rotation[2] = transform->get_float_with_precision_loss("qz"sv).value_or(0);
                snap.rotation[3] = transform->get_float_with_precision_loss("qw"sv).value_or(1);
                snap.scale[0] = transform->get_float_with_precision_loss("sx"sv).value_or(1);
                snap.scale[1] = transform->get_float_with_precision_loss("sy"sv).value_or(1);
                snap.scale[2] = transform->get_float_with_precision_loss("sz"sv).value_or(1);
            }

            m_overlay->push_component_update(snap);
        }
    }
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
