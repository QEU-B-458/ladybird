/*
 * MyceliumVR - a social VR platform built on the Ladybird browser engine
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "OverlayManager.h"

#include <AK/ByteString.h>
#include <AK/JsonObject.h>
#include <AK/StringBuilder.h>
#include <AK/Utf16String.h>
#include <entt/entt.hpp>
#include <LibCore/File.h>
#include <LibFileSystem/FileSystem.h>

#include "../../Engine/Engine.h"
#include "../../Networking/ControlBusClient.h"
#include <UI/MyceliumVR/Scripting/BridgeRegistry.h>
#include <UI/MyceliumVR/Support/Profiling.h>

#include "../../Support/VirtualFileSystem.h"
#include "../../World/World.h"
#include "../../World/WorldManagementSystem.h"

namespace MyceliumVR {

OverlayManager::OverlayManager() = default;

ErrorOr<void> OverlayManager::initialize(
    int width, int height,
    bool supports_vulkan_external_images,
    VkDevice vulkan_device,
    Engine& engine,
    WorldManagementSystem& world_management_system,
    VirtualFileSystem const* vfs)
{
    m_engine = &engine;
    m_world_management_system = &world_management_system;
    m_supports_vulkan_external_images = supports_vulkan_external_images;
    if (vfs)
        m_vfs_mounts = vfs->mount_prefixes();

    // Bundle all UI assets inline so WebContent never has to fetch file:// sub-resources
    // (which causes heap corruption in LibWeb's file:// loader on this platform).
    auto ui_base = TRY(FileSystem::real_path("UI/MyceliumVR/ui"sv));

    auto read_ui_file = [&](StringView relative_path) -> ErrorOr<ByteBuffer> {
        auto full_path = ByteString::formatted("{}/{}", ui_base, relative_path);
        auto file = TRY(Core::File::open(full_path, Core::File::OpenMode::Read));
        return TRY(file->read_until_eof());
    };

    StringBuilder html;
    html.append("<!doctype html>\n<html lang=\"en\">\n<head>\n"sv);
    html.append("  <meta charset=\"utf-8\">\n"sv);
    html.append("  <meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">\n"sv);
    html.append("  <title>MyceliumVR</title>\n"sv);

    auto css = TRY(read_ui_file("lib/styles.css"sv));
    html.append("  <style>"sv);
    html.append(StringView { css.bytes() });
    html.append("</style>\n"sv);

    auto logger_js = TRY(read_ui_file("lib/logger.js"sv));
    html.append("  <script>"sv);
    html.append(StringView { logger_js.bytes() });
    html.append("</script>\n"sv);

    html.append("</head>\n<body>\n"sv);
    html.append("  <div id=\"root\" style=\"min-height:100vh;background:rgba(10,14,12,0.92);color:#e8efe9;padding:12px;font:12px monospace;\">MyceliumVR UI loading...</div>\n"sv);

    StringView const lib_scripts[] = {
        "lib/data.js"sv, "lib/state.js"sv, "lib/icons.js"sv,
        "lib/tweaks.js"sv, "lib/stats.js"sv, "lib/render-graph.js"sv,
        "lib/hierarchy.js"sv, "lib/inspector.js"sv, "lib/console.js"sv,
        "lib/worlds.js"sv, "lib/viewport.js"sv, "lib/topbar.js"sv, "lib/statusbar.js"sv,
        "lib/tweaks-panel.js"sv, "lib/events.js"sv, "lib/bridge.js"sv, "main.js"sv,
    };
    for (auto script_path : lib_scripts) {
        auto content = TRY(read_ui_file(script_path));
        html.append("  <script>"sv);
        html.append(StringView { content.bytes() });
        html.append("</script>\n"sv);
    }

    html.append("</body>\n</html>\n"sv);

    outln("OverlayManager: HTML bundle built ({} bytes), calling load_html...", html.length());
    m_view = make<WebContentView>(width, height, m_supports_vulkan_external_images, vulkan_device);
    m_view->set_debug_name("overlay"_string);
    if (m_supports_vulkan_external_images) {
        // Keep the current imported image alive until the host frame has
        // actually sampled it; immediate ack on paint receipt causes the two
        // exported images to be recycled underneath the overlay renderer.
        m_view->configure_deferred_ready_to_paint_acks(true);
    }
    m_view->on_vulkan_image_ready = [this]() {
        publish_current_vulkan_image();
    };
    m_view->on_vulkan_images_invalidated = [this]() {
        if (m_published_overlay_image.image != VK_NULL_HANDLE)
            dbgln("OverlayManager: invalidating published overlay VkImage before backing-store reallocation");
        clear_published_vulkan_image();
    };

    m_view->on_title_change = [this](Utf16String const& title) {
        auto utf8 = title.to_utf8();
        auto utf8_sv = utf8.bytes_as_string_view();
        if (utf8_sv.starts_with("mvr:select:"sv)) {
            auto id_str = utf8_sv.substring_view(11);
            auto id = id_str.to_number<u32>();
            if (id.has_value() && m_control_bus_client) {
                JsonObject params;
                params.set("entity_id"sv, *id);
                (void)m_control_bus_client->send_request("entities.select"sv, params);
            }
        } else if (utf8_sv.starts_with("mvr:switch_world:"sv)) {
            if (!m_world_management_system)
                return;
            auto result = m_world_management_system->switch_to_world({
                .world_path = ByteString(utf8_sv.substring_view(17)),
                .script_path = ByteString("UI/MyceliumVR/scripts/main.js"sv),
                .control_script_path = ByteString("UI/MyceliumVR/scripts/controls.js"sv),
                .has_script_path_override = false,
                .load_in_background = false,
            });
            if (!result.is_error())
                push_worlds(*m_world_management_system);
        } else if (utf8_sv.starts_with("mvr:load_world_background:"sv)) {
            if (!m_world_management_system)
                return;
            auto result = m_world_management_system->boot({
                .world_path = ByteString(utf8_sv.substring_view(26)),
                .script_path = ByteString("UI/MyceliumVR/scripts/main.js"sv),
                .control_script_path = ByteString("UI/MyceliumVR/scripts/controls.js"sv),
                .has_script_path_override = false,
                .load_in_background = true,
            });
            if (!result.is_error())
                push_worlds(*m_world_management_system);
        } else if (utf8_sv == "mvr:refresh_worlds"sv) {
            if (m_world_management_system)
                push_worlds(*m_world_management_system);
        } else if (utf8_sv == "mvr:refresh_scene"sv) {
            if (m_control_bus_client)
                (void)m_control_bus_client->send_request("entities.list"sv);
        } else if (utf8_sv.starts_with("mvr:eval:"sv)) {
            if (m_control_bus_client) {
                JsonObject params;
                params.set("source"sv, utf8_sv.substring_view(9));
                (void)m_control_bus_client->send_request("script.eval"sv, params);
            }
        }
    };

    m_view->load_html(html.string_view());
    outln("OverlayManager: overlay initialized ({}x{}).", width, height);
    return {};
}

void OverlayManager::connect_to_world(u16 port, StringView token)
{
    m_pending_port = port;
    m_pending_token = MUST(String::from_utf8(token));
    m_retry_count = 0;

    if (!m_retry_timer) {
        m_retry_timer = Core::Timer::create_single_shot(0, [this] {
            m_control_bus_client = make<ControlBusClient>();
            m_control_bus_client->set_message_callback([this](JsonObject const& message) {
                handle_control_bus_message(message);
            });

            auto result = m_control_bus_client->connect(m_pending_port, m_pending_token);
            if (result.is_error()) {
                warnln("OverlayManager: Connect attempt {}/10 failed to port {}: {}", m_retry_count + 1, m_pending_port, result.error());
                if (m_retry_count < 10) {
                    ++m_retry_count;
                    m_retry_timer->start(100);
                    return;
                }
                warnln("OverlayManager: Failed to connect to world ControlBus after 10 attempts: {}", result.error());
                return;
            }

            JsonObject params;
            JsonArray topics;
            (void)topics.append("*"_string);
            params.set("topics"sv, move(topics));
            (void)m_control_bus_client->send_request("events.subscribe"sv, params);
            (void)m_control_bus_client->send_request("entities.list"sv);
        });
    }

    m_retry_timer->start(0);
}

void OverlayManager::notify_world_faulted(u32 world_id, StringView reason)
{
    push_log("error"_string, MUST(String::formatted("World:{}", world_id)), MUST(String::from_utf8(reason)));
}

void OverlayManager::refresh_scene()
{
    if (m_control_bus_client && m_control_bus_client->is_connected())
        (void)m_control_bus_client->send_request("entities.list"sv);
}

void OverlayManager::tick(double fps, double delta_time_ms)
{
    if (!is_initialized())
        return;

    if (m_control_bus_client && m_control_bus_client->is_connected())
        m_control_bus_client->poll();

    if (!m_startup_pushed && has_ever_painted()) {
        push_bridge_functions();
        push_assets(m_vfs_mounts);
        if (m_world_management_system)
            push_worlds(*m_world_management_system);
        m_startup_pushed = true;
    }

    static int worlds_push_count = 0;
    if (m_world_management_system && worlds_push_count++ % 300 == 0 && has_ever_painted())
        push_worlds(*m_world_management_system);

    static int update_tick = 0;
    update_tick++;

    if (m_control_bus_client && m_control_bus_client->is_connected()) {
        if (m_selected_entity != entt::null && update_tick % 10 == 0) {
            JsonObject params;
            params.set("entity_id"sv, static_cast<u32>(m_selected_entity));
            (void)m_control_bus_client->send_request("entities.inspect"sv, params);
        }

        if (update_tick % 15 == 0) {
            (void)m_control_bus_client->send_request("metrics.snapshot"sv);
        }
    }

    update_stats({
        .fps = fps,
        .frame_time_ms = delta_time_ms,
        .entity_count = m_last_entity_count,
        .draw_calls = m_engine ? m_engine->renderer().last_frame_draw_calls() : 0,
        .triangle_count = m_engine ? m_engine->renderer().last_frame_triangle_count() : 0,
        .camera_state = m_engine ? m_engine->camera_state() : VulkanRenderer::CameraState {},
    });
}

void OverlayManager::post_render()
{
    if (!is_initialized() || !m_engine)
        return;
    update_render_timings(m_engine->renderer().last_frame_timings());

    if (!m_overlay_needs_ack)
        return;
    m_overlay_needs_ack = false;

    if (has_overlay_vulkan_image())
        m_engine->renderer().wait_for_graphics_queue_idle();
    m_view->acknowledge_ready_to_paint_with_trace("overlay-post-render"sv);
}

void OverlayManager::handle_control_bus_message(JsonObject const& message)
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
            notify_selection_changed(m_selected_entity);

            JsonObject params;
            params.set("entity_id"sv, entity_id);
            (void)m_control_bus_client->send_request("entities.inspect"sv, params);
        }
        return;
    }

    if (type != "response"sv)
        return;

    auto ok = message.get_bool("ok"sv).value_or(false);
    if (!ok)
        return;

    auto result = message.get_object("result"sv);
    if (!result.has_value())
        return;

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
            push_hierarchy(hierarchy);
        }
        return;
    }

    if (result->has("attached_components"sv)) {
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
        push_component_update(snap);
        return;
    } else if (result->has("value"sv)) {
        auto value = result->get_string("value"sv).value_or(""_string);
        push_log("OK"_string, "Bridge"_string, MUST(String::formatted("\u2192 {}", value)));
    } else if (result->has("entity_count"sv)) {
        m_last_entity_count = result->get_u32("entity_count"sv).value_or(0);
    }
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

    ++m_frames_since_init;
    if (m_frames_since_init == 300 && !m_has_ever_painted) {
        if (m_supports_vulkan_external_images)
            warnln("OverlayManager: no overlay Vulkan image after 300 frames — WebContent may have crashed or failed to paint.");
        else
            warnln("OverlayManager: zero-copy overlay unavailable after 300 frames — external Vulkan image import is required for the overlay.");
    }

    // Let the HTML/CSS bundle reach its first paint before we start injecting
    // per-frame bridge traffic. This keeps overlay bootstrap independent of the
    // world/control-bus timing and avoids hammering WebContent during startup.
    if (!m_has_ever_painted)
        return;

    m_view->run_javascript(MUST(String::formatted(
        "window.__myceliumBridge && window.__myceliumBridge.update({:.2f}, {:.3f}, {}, {}, {}, {:.3f}, {:.3f}, {:.3f}, {:.3f}, {:.3f}, {});",
        stats.fps,
        stats.frame_time_ms,
        stats.entity_count,
        stats.draw_calls,
        stats.triangle_count,
        stats.camera_state.position[0],
        stats.camera_state.position[1],
        stats.camera_state.position[2],
        stats.camera_state.yaw_degrees,
        stats.camera_state.pitch_degrees,
        m_focused ? "true" : "false")));

    // Flush queued log entries — cap at 32 per frame to avoid JS overload.
    size_t const flush_limit = 32;
    size_t flushed = 0;
    while (!m_pending_logs.is_empty() && flushed < flush_limit) {
        auto entry = m_pending_logs.take_first();
        // Escape backslashes and single-quotes so the string is safe to inline.
        auto escape = [](String const& s) -> String {
            auto result = MUST(s.replace("\\"sv, "\\\\"sv, ReplaceMode::All));
            result = MUST(result.replace("'"sv, "\\'"sv, ReplaceMode::All));
            return result;
        };
        m_view->run_javascript(MUST(String::formatted(
            "window.__myceliumBridge && window.__myceliumBridge.log('{}','{}','{}');",
            escape(entry.level),
            escape(entry.source),
            escape(entry.message))));
        ++flushed;
    }
}

void OverlayManager::push_log(StringView level, StringView source, StringView message)
{
    // Drop entries silently if the overlay isn't up yet (e.g. during early init).
    if (!m_view || !m_visible)
        return;
    m_pending_logs.append({
        MUST(String::from_utf8(level)),
        MUST(String::from_utf8(source)),
        MUST(String::from_utf8(message)),
    });
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

bool OverlayManager::has_overlay_vulkan_image() const
{
    return m_visible && m_published_overlay_image.image != VK_NULL_HANDLE;
}

VkImage OverlayManager::overlay_vulkan_image() const
{
    return m_published_overlay_image.image;
}

u32 OverlayManager::overlay_vulkan_width() const
{
    return m_published_overlay_image.width;
}

u32 OverlayManager::overlay_vulkan_height() const
{
    return m_published_overlay_image.height;
}

void OverlayManager::push_hierarchy(Vector<EntityHierarchyEntry> const& entries)
{
    if (!m_view || !m_visible)
        return;

    static StringView const alpha_names[] = { "opaque"sv, "clip"sv, "blend"sv, "hash"sv };
    auto escape_name = [](StringView s) -> String {
        auto result = MUST(String::from_utf8(s));
        result = MUST(result.replace("\\"sv, "\\\\"sv, ReplaceMode::All));
        result = MUST(result.replace("'"sv, "\\'"sv, ReplaceMode::All));
        return result;
    };

    StringBuilder js;
    js.append("window.__myceliumBridge && window.__myceliumBridge.setHierarchy(["sv);
    for (size_t i = 0; i < entries.size(); ++i) {
        auto const& e = entries[i];
        if (i > 0) js.append(","sv);
        auto alpha_idx = static_cast<size_t>(e.alpha_mode);
        if (alpha_idx >= 4) alpha_idx = 0;
        js.appendff("{{id:{},name:'{}',kind:'{}',alpha:'{}',static:{},depth:{}",
            static_cast<u32>(e.id),
            escape_name(e.name),
            e.kind,
            alpha_names[alpha_idx],
            e.is_static ? "true"sv : "false"sv,
            e.depth);
        if (e.parent_id != entt::null)
            js.appendff(",parent:{}", static_cast<u32>(e.parent_id));
        js.append("}"sv);
    }
    js.append("]);"sv);

    m_view->run_javascript(MUST(String::from_utf8(js.string_view())));
}

void OverlayManager::push_component_update(EntityComponentSnapshot const& snap)
{
    if (!m_view || !m_visible)
        return;

    auto escape = [](String const& s) -> String {
        auto r = MUST(s.replace("\\"sv, "\\\\"sv, ReplaceMode::All));
        r = MUST(r.replace("'"sv, "\\'"sv, ReplaceMode::All));
        return r;
    };

    StringView cull_str = snap.has_cull_override
        ? (snap.cull_mode == CullOverride::Mode::Front    ? "front"sv
         : snap.cull_mode == CullOverride::Mode::Disabled ? "disabled"sv
         : "back"sv)
        : ""sv;

    StringBuilder js;
    js.append("window.__myceliumBridge && window.__myceliumBridge.componentUpdate({"sv);
    js.appendff("id:{},name:'{}',"sv, static_cast<u32>(snap.id), escape(snap.name));
    js.append("attachedComponents:["sv);
    for (size_t i = 0; i < snap.attached_components.size(); ++i) {
        if (i > 0)
            js.append(","sv);
        js.appendff("'{}'"sv, escape(snap.attached_components[i]));
    }
    js.append("],"sv);
    js.appendff("transform:{{px:{:.5f},py:{:.5f},pz:{:.5f},"sv,
        snap.position[0], snap.position[1], snap.position[2]);
    js.appendff("qx:{:.6f},qy:{:.6f},qz:{:.6f},qw:{:.6f},"sv,
        snap.rotation[0], snap.rotation[1], snap.rotation[2], snap.rotation[3]);
    js.appendff("sx:{:.5f},sy:{:.5f},sz:{:.5f}}},"sv,
        snap.scale[0], snap.scale[1], snap.scale[2]);
    if (snap.has_mesh_renderer)
        js.appendff("meshRenderer:{{mesh:'{}',material:'{}',normalMap:'{}'}},"sv,
            escape(snap.mesh), escape(snap.material), escape(snap.normal_map));
    else
        js.append("meshRenderer:null,"sv);
    if (snap.has_panel)
        js.appendff("panel:{{url:'{}',width:{:.3f},height:{:.3f}}},"sv, escape(snap.panel_url), snap.panel_width, snap.panel_height);
    else
        js.append("panel:null,"sv);
    if (snap.has_cull_override)
        js.appendff("cull:'{}',"sv, cull_str);
    else
        js.append("cull:null,"sv);
    js.appendff("tags:{{isStatic:{},alphaBlend:{},alphaClip:{},alphaHash:{}}}"sv,
        snap.is_static   ? "true"sv : "false"sv,
        snap.alpha_blend ? "true"sv : "false"sv,
        snap.alpha_clip  ? "true"sv : "false"sv,
        snap.alpha_hash  ? "true"sv : "false"sv);
    js.append("});"sv);
    m_view->run_javascript(MUST(String::from_utf8(js.string_view())));
}

void OverlayManager::update_render_timings(Vector<VulkanRenderer::PassTiming> const& timings)
{
    if (!m_view || !m_visible || timings.is_empty())
        return;

    auto escape_name = [](StringView s) -> String {
        auto result = MUST(String::from_utf8(s));
        result = MUST(result.replace("\\"sv, "\\\\"sv, ReplaceMode::All));
        result = MUST(result.replace("'"sv, "\\'"sv, ReplaceMode::All));
        return result;
    };

    StringBuilder js;
    js.append("window.__myceliumBridge && window.__myceliumBridge.renderTimings(["sv);
    for (size_t i = 0; i < timings.size(); ++i) {
        if (i > 0) js.append(","sv);
        js.appendff("{{name:'{}',ms:{:.4f}}}", escape_name(timings[i].name), timings[i].gpu_ms);
    }
    js.append("]);"sv);
    m_view->run_javascript(MUST(String::from_utf8(js.string_view())));
}

void OverlayManager::notify_selection_changed(EntityId entity)
{
    if (!m_view || !m_visible)
        return;
    m_view->run_javascript(MUST(String::formatted(
        "window.__myceliumBridge && window.__myceliumBridge.selectionChanged({});",
        static_cast<u32>(entity))));
}

void OverlayManager::push_bridge_functions()
{
    if (!m_view || !m_visible)
        return;

    StringBuilder js;
    js.append("window.__myceliumBridge && window.__myceliumBridge.registerFunctions(["sv);
    auto const& fns = BridgeRegistry::the().functions();
    for (size_t i = 0; i < fns.size(); ++i) {
        if (i > 0) js.append(","sv);
        if (fns[i].js_namespace.is_empty())
            js.appendff("'mycelium.{}'", fns[i].name);
        else
            js.appendff("'mycelium.{}.{}'", fns[i].js_namespace, fns[i].name);
    }
    js.append("]);"sv);
    m_view->run_javascript(MUST(String::from_utf8(js.string_view())));
}

void MyceliumVR::OverlayManager::push_worlds(const MyceliumVR::WorldManagementSystem& wms)
{
    if (!m_view)
        return;

    auto worlds = wms.list_available_worlds();
    JsonArray arr;
    for (auto const& world : worlds) {
        JsonObject obj;
        obj.set("path"sv, JsonValue(world.path));
        obj.set("name"sv, JsonValue(world.name));
        obj.set("is_running"sv, world.is_running);
        obj.set("is_active"sv, world.is_active);
        (void)arr.append(move(obj));
    }

    m_view->run_javascript(MUST(String::formatted("if (window.onWorldsUpdate) window.onWorldsUpdate({});", arr.serialized())));
}

void OverlayManager::push_assets(Vector<String> const& mount_prefixes)
{
    if (!m_view || !m_visible)
        return;

    StringBuilder js;
    js.append("window.__myceliumBridge && window.__myceliumBridge.registerAssets(["sv);
    for (size_t i = 0; i < mount_prefixes.size(); ++i) {
        if (i > 0) js.append(","sv);
        auto escaped = MUST(mount_prefixes[i].replace("'"sv, "\\'"sv, ReplaceMode::All));
        js.appendff("'{}'", escaped);
    }
    js.append("]);"sv);
    m_view->run_javascript(MUST(String::from_utf8(js.string_view())));
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

void OverlayManager::publish_current_vulkan_image()
{
    if (!m_view)
        return;

    if (!m_view->has_vulkan_image()) {
        clear_published_vulkan_image();
        return;
    }

    m_published_overlay_image.image = m_view->current_vulkan_image();
    m_published_overlay_image.width = m_view->vulkan_image_width();
    m_published_overlay_image.height = m_view->vulkan_image_height();
    dbgln("OverlayManager: published overlay VkImage={} ({}x{})",
        (void*)m_published_overlay_image.image,
        m_published_overlay_image.width,
        m_published_overlay_image.height);
    dbgln("OverlayManager: published overlay front_id={} back_id={} pending_acks={} has_vulkan_image={}",
        m_view->current_front_bitmap_id(),
        m_view->current_back_bitmap_id(),
        m_view->pending_ready_to_paint_ack_count(),
        m_view->has_vulkan_image());

    if (!m_has_ever_painted) {
        m_has_ever_painted = true;
        outln("OverlayManager: first overlay Vulkan image received ({}x{}).", m_published_overlay_image.width, m_published_overlay_image.height);
    }
    m_overlay_needs_ack = true;
}

void OverlayManager::clear_published_vulkan_image()
{
    if (m_published_overlay_image.image != VK_NULL_HANDLE)
        dbgln("OverlayManager: clearing published overlay VkImage={}", (void*)m_published_overlay_image.image);
    m_published_overlay_image = {};
}

}
