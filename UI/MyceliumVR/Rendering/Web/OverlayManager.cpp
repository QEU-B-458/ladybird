/*
 * MyceliumVR - a social VR platform built on the Ladybird browser engine
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "OverlayManager.h"

#include <AK/ByteString.h>
#include <AK/StringBuilder.h>
#include <AK/Utf16String.h>
#include <entt/entt.hpp>
#include <LibCore/File.h>
#include <LibFileSystem/FileSystem.h>

#include <UI/MyceliumVR/Scripting/BridgeRegistry.h>
#include <UI/MyceliumVR/Support/Profiling.h>

namespace MyceliumVR {

OverlayManager::OverlayManager() = default;

ErrorOr<void> OverlayManager::initialize(int width, int height, bool supports_vulkan_external_images, VkDevice vulkan_device)
{
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

    html.append("</head>\n<body>\n  <div id=\"root\"></div>\n"sv);

    StringView const lib_scripts[] = {
        "lib/data.js"sv, "lib/state.js"sv, "lib/icons.js"sv,
        "lib/tweaks.js"sv, "lib/stats.js"sv, "lib/render-graph.js"sv,
        "lib/hierarchy.js"sv, "lib/inspector.js"sv, "lib/console.js"sv,
        "lib/viewport.js"sv, "lib/topbar.js"sv, "lib/statusbar.js"sv,
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
    m_view = make<WebContentView>(width, height, supports_vulkan_external_images, vulkan_device);
    m_view->debug_request("transparent-top-level-canvas"sv, "on"sv);

    m_view->on_title_change = [this](Utf16String const& title) {
        auto utf8 = title.to_utf8();
        auto utf8_sv = utf8.bytes_as_string_view();
        if (!utf8_sv.starts_with("mvr:select:"sv))
            return;
        auto id_str = utf8_sv.substring_view(11);
        auto id = id_str.to_number<u32>();
        if (id.has_value() && m_on_entity_select)
            m_on_entity_select(*id);
    };

    m_view->load_html(html.string_view());
    outln("OverlayManager: overlay initialized ({}x{}).", width, height);
    return {};
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
    // Warn once after ~5s if the overlay has never produced a bitmap.
    if (m_frames_since_init == 300 && !m_has_ever_painted)
        warnln("OverlayManager: no overlay bitmap after 300 frames — WebContent may have crashed or JS failed to load.");

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

ErrorOr<Optional<WebContentBitmapView>> OverlayManager::snapshot_overlay_view()
{
#if defined(TRACY_ENABLE)
    ZoneScopedN("Boundary/Ladybird/OverlaySnapshot");
#endif
    if (!m_view || !m_visible)
        return Optional<WebContentBitmapView> {};

    auto snapshot = TRY(m_view->snapshot_bitmap_view_for_current_viewport());
    if (!snapshot.has_value())
        return Optional<WebContentBitmapView> {};

    if (!m_has_ever_painted) {
        m_has_ever_painted = true;
        outln("OverlayManager: first overlay paint received ({}x{}).", snapshot->width, snapshot->height);
    }

#if defined(TRACY_ENABLE)
    auto bytes = static_cast<int64_t>(snapshot->width) * snapshot->height * 4;
    TracyPlot("Overlay/SnapshotWidth", static_cast<int64_t>(snapshot->width));
    TracyPlot("Overlay/SnapshotHeight", static_cast<int64_t>(snapshot->height));
    TracyPlot("Overlay/SnapshotBytes", bytes);
#endif
    return snapshot;
}

bool OverlayManager::has_overlay_vulkan_image() const
{
    return m_view && m_visible && m_view->has_vulkan_image();
}

VkImage OverlayManager::overlay_vulkan_image() const
{
    return m_view ? m_view->current_vulkan_image() : VK_NULL_HANDLE;
}

u32 OverlayManager::overlay_vulkan_width() const
{
    return m_view ? m_view->vulkan_image_width() : 0;
}

u32 OverlayManager::overlay_vulkan_height() const
{
    return m_view ? m_view->vulkan_image_height() : 0;
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
        js.appendff("panel:{{width:{:.3f},height:{:.3f}}},"sv, snap.panel_width, snap.panel_height);
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

}
