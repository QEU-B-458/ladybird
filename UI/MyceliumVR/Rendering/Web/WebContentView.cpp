/*
 * MyceliumVR - a social VR platform built on the Ladybird browser engine
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "WebContentView.h"

#include <LibGfx/Bitmap.h>
#include <LibGfx/ExternalVulkanImage.h>
#include <LibGfx/SharedImageBuffer.h>
#include <LibIPC/File.h>
#include <LibWeb/UIEvents/MouseButton.h>
#include <LibWebView/WebContentClient.h>

#include <UI/MyceliumVR/Support/Profiling.h>

namespace MyceliumVR {

static constexpr bool debug_web_content_paint = false;

static Web::UIEvents::MouseButton sdl_button_to_web_button(u8 button)
{
    switch (button) {
    case SDL_BUTTON_LEFT:
        return Web::UIEvents::MouseButton::Primary;
    case SDL_BUTTON_RIGHT:
        return Web::UIEvents::MouseButton::Secondary;
    case SDL_BUTTON_MIDDLE:
        return Web::UIEvents::MouseButton::Middle;
    case SDL_BUTTON_X1:
        return Web::UIEvents::MouseButton::Backward;
    case SDL_BUTTON_X2:
        return Web::UIEvents::MouseButton::Forward;
    default:
        return Web::UIEvents::MouseButton::None;
    }
}

static Web::UIEvents::KeyModifier sdl_modifiers_to_web_modifiers(SDL_Keymod modifiers)
{
    auto web_modifiers = Web::UIEvents::KeyModifier::Mod_None;
    if (modifiers & SDL_KMOD_ALT)
        web_modifiers |= Web::UIEvents::KeyModifier::Mod_Alt;
    if (modifiers & SDL_KMOD_CTRL)
        web_modifiers |= Web::UIEvents::KeyModifier::Mod_Ctrl;
    if (modifiers & SDL_KMOD_SHIFT)
        web_modifiers |= Web::UIEvents::KeyModifier::Mod_Shift;
    if (modifiers & SDL_KMOD_GUI)
        web_modifiers |= Web::UIEvents::KeyModifier::Mod_Super;
    return web_modifiers;
}

static Web::UIEvents::KeyCode sdl_key_to_web_key(SDL_Keycode key)
{
    switch (key) {
    case SDLK_BACKSPACE:
        return Web::UIEvents::KeyCode::Key_Backspace;
    case SDLK_TAB:
        return Web::UIEvents::KeyCode::Key_Tab;
    case SDLK_RETURN:
        return Web::UIEvents::KeyCode::Key_Return;
    case SDLK_ESCAPE:
        return Web::UIEvents::KeyCode::Key_Escape;
    case SDLK_SPACE:
        return Web::UIEvents::KeyCode::Key_Space;
    case SDLK_LEFT:
        return Web::UIEvents::KeyCode::Key_Left;
    case SDLK_RIGHT:
        return Web::UIEvents::KeyCode::Key_Right;
    case SDLK_UP:
        return Web::UIEvents::KeyCode::Key_Up;
    case SDLK_DOWN:
        return Web::UIEvents::KeyCode::Key_Down;
    case SDLK_DELETE:
        return Web::UIEvents::KeyCode::Key_Delete;
    case SDLK_HOME:
        return Web::UIEvents::KeyCode::Key_Home;
    case SDLK_END:
        return Web::UIEvents::KeyCode::Key_End;
    case SDLK_PAGEUP:
        return Web::UIEvents::KeyCode::Key_PageUp;
    case SDLK_PAGEDOWN:
        return Web::UIEvents::KeyCode::Key_PageDown;
    default:
        break;
    }

    if (key >= SDLK_A && key <= SDLK_Z)
        return static_cast<Web::UIEvents::KeyCode>(Web::UIEvents::KeyCode::Key_A + (key - SDLK_A));
    if (key >= SDLK_0 && key <= SDLK_9)
        return static_cast<Web::UIEvents::KeyCode>(Web::UIEvents::KeyCode::Key_0 + (key - SDLK_0));

    return Web::UIEvents::code_point_to_key_code(static_cast<u32>(key));
}

WebContentView::WebContentView(int width, int height, bool supports_vulkan_external_images, VkDevice vulkan_device)
    : m_width(width)
    , m_height(height)
    , m_supports_vulkan_external_images(supports_vulkan_external_images)
    , m_vulkan_device(vulkan_device)
{
    on_ready_to_paint = [this]() {
        if constexpr (debug_web_content_paint)
            dbgln("WebContentView: on_ready_to_paint fired!");
        m_needs_paint = true;
        m_frames_waiting_for_first_paint = 0;
    };
    on_load_start = [](URL::URL const& url, bool is_redirect) {
        if constexpr (debug_web_content_paint)
            dbgln("WebContentView: load start: {} (redirect: {})", url, is_redirect);
    };
    on_load_finish = [](URL::URL const& url) {
        if constexpr (debug_web_content_paint)
            dbgln("WebContentView: load finish: {}", url);
    };
    on_resource_status_change = [](i32 count_waiting) {
        if constexpr (debug_web_content_paint)
            dbgln("WebContentView: resources waiting: {}", count_waiting);
    };
    on_web_content_crashed = []() {
        warnln("WebContentView: WebContent crash notification received");
    };

    if constexpr (debug_web_content_paint)
        dbgln("WebContentView: calling initialize_client...");
    initialize_client(CreateNewClient::Yes);
    client().async_set_use_vulkan_external_images(m_supports_vulkan_external_images);
    if constexpr (debug_web_content_paint) {
        dbgln("WebContentView: initialize_client done, client connected: {}, page id: {}, front bitmap id: {}, back bitmap id: {}",
            m_client_state.client != nullptr,
            page_id(),
            m_client_state.front_bitmap.id,
            m_client_state.back_bitmap.id);
    }
    set_system_visibility_state(Web::HTML::VisibilityState::Visible);
    if constexpr (debug_web_content_paint)
        dbgln("WebContentView: system visibility set to Visible");
}

WebContentView::~WebContentView()
{
    destroy_vulkan_images();
}

void WebContentView::destroy_vulkan_images()
{
#if defined(USE_VULKAN)
    if (m_vulkan_device == VK_NULL_HANDLE || m_vulkan_images.is_empty())
        return;
    // Ensure the GPU has finished sampling any of these images before freeing them.
    // Resize is infrequent so the stall is acceptable — same pattern as swapchain recreation.
    vkDeviceWaitIdle(m_vulkan_device);
    for (auto& [id, slot] : m_vulkan_images) {
        if (slot.imported.image != VK_NULL_HANDLE)
            vkDestroyImage(m_vulkan_device, slot.imported.image, nullptr);
        if (slot.imported.memory != VK_NULL_HANDLE)
            vkFreeMemory(m_vulkan_device, slot.imported.memory, nullptr);
    }
    m_vulkan_images.clear();
#endif
}

void WebContentView::did_allocate_vulkan_backing_stores(
    Badge<WebView::WebContentClient>,
    i32 front_image_id, IPC::File front_fd, u64 front_allocation_size, u32 front_memory_type_index, u32 front_format, u32 front_width, u32 front_height,
    i32 back_image_id, IPC::File back_fd, u64 back_allocation_size, u32 back_memory_type_index, u32 back_format, u32 back_width, u32 back_height)
{
#if defined(USE_VULKAN)
    if (m_vulkan_device == VK_NULL_HANDLE) {
        warnln("WebContentView: received did_allocate_vulkan_backing_stores but no VkDevice — ignoring");
        return;
    }

    destroy_vulkan_images();

    // Update ViewImplementation's id tracking so server_did_paint can swap correctly.
    m_client_state.front_bitmap.id = front_image_id;
    m_client_state.back_bitmap.id = back_image_id;
    m_client_state.has_usable_bitmap = false;

    auto import_one = [&](i32 image_id, IPC::File& file, u64 alloc_size, u32 mem_type_idx, u32 format, u32 width, u32 height) {
        Gfx::ExternalMemoryHandle handle;
        handle.native = file.take_fd();
        handle.allocation_size = alloc_size;
        handle.memory_type_index = mem_type_idx;
        handle.format = static_cast<VkFormat>(format);
        handle.width = width;
        handle.height = height;

        auto imported_or_error = Gfx::import_external_vulkan_image(m_vulkan_device, move(handle));
        if (imported_or_error.is_error()) {
            warnln("WebContentView: failed to import VkImage id={}: {}", image_id, imported_or_error.error());
            return;
        }
        m_vulkan_images.set(image_id, VulkanImageSlot {
            .imported = imported_or_error.release_value(),
            .width = width,
            .height = height,
        });
    };

    import_one(front_image_id, front_fd, front_allocation_size, front_memory_type_index, front_format, front_width, front_height);
    import_one(back_image_id, back_fd, back_allocation_size, back_memory_type_index, back_format, back_width, back_height);

    if constexpr (debug_web_content_paint)
        dbgln("WebContentView: imported Vulkan backing stores front_id={} back_id={}", front_image_id, back_image_id);
#else
    (void)front_image_id; (void)front_fd; (void)front_allocation_size; (void)front_memory_type_index; (void)front_format; (void)front_width; (void)front_height;
    (void)back_image_id; (void)back_fd; (void)back_allocation_size; (void)back_memory_type_index; (void)back_format; (void)back_width; (void)back_height;
#endif
}

bool WebContentView::has_vulkan_image() const
{
#if defined(USE_VULKAN)
    if (!m_client_state.has_usable_bitmap)
        return false;
    return m_vulkan_images.contains(m_client_state.front_bitmap.id);
#else
    return false;
#endif
}

VkImage WebContentView::current_vulkan_image() const
{
#if defined(USE_VULKAN)
    if (auto it = m_vulkan_images.find(m_client_state.front_bitmap.id); it != m_vulkan_images.end())
        return it->value.imported.image;
#endif
    return VK_NULL_HANDLE;
}

u32 WebContentView::vulkan_image_width() const
{
#if defined(USE_VULKAN)
    if (auto it = m_vulkan_images.find(m_client_state.front_bitmap.id); it != m_vulkan_images.end())
        return it->value.width;
#endif
    return 0;
}

u32 WebContentView::vulkan_image_height() const
{
#if defined(USE_VULKAN)
    if (auto it = m_vulkan_images.find(m_client_state.front_bitmap.id); it != m_vulkan_images.end())
        return it->value.height;
#endif
    return 0;
}

bool WebContentView::needs_paint() const
{
#if defined(TRACY_ENABLE)
    TracyPlot("WebContent/NeedsPaint", static_cast<int64_t>(m_needs_paint ? 1 : 0));
    TracyPlot("WebContent/HasUsableBitmap", static_cast<int64_t>(m_client_state.has_usable_bitmap ? 1 : 0));
#endif
    return m_needs_paint || !m_client_state.has_usable_bitmap;
}

ErrorOr<Optional<WebContentBitmapView>> WebContentView::snapshot_bitmap_view()
{
#if defined(TRACY_ENABLE)
    ZoneScopedN("Boundary/Ladybird/SnapshotBitmapView");
#endif
    Gfx::Bitmap const* bitmap = nullptr;
    Web::DevicePixelSize bitmap_size;

    if (m_client_state.has_usable_bitmap) {
        VERIFY(m_client_state.front_bitmap.shared_image_buffer);
        bitmap = m_client_state.front_bitmap.shared_image_buffer->bitmap().ptr();
        bitmap_size = m_client_state.front_bitmap.last_painted_size;
    } else if (m_backup_shared_image_buffer) {
        bitmap = m_backup_shared_image_buffer->bitmap().ptr();
        bitmap_size = m_backup_bitmap_size;
    }

    if (!bitmap)
        return Optional<WebContentBitmapView> {};

    auto snapshot = WebContentBitmapView {
        .width = bitmap->width(),
        .height = bitmap->height(),
        .visible_width = static_cast<int>(bitmap_size.width()),
        .visible_height = static_cast<int>(bitmap_size.height()),
        .bitmap = bitmap,
    };
#if defined(TRACY_ENABLE)
    TracyPlot("WebContent/SnapshotWidth", static_cast<int64_t>(snapshot.width));
    TracyPlot("WebContent/SnapshotHeight", static_cast<int64_t>(snapshot.height));
    TracyPlot("WebContent/SnapshotVisibleWidth", static_cast<int64_t>(snapshot.visible_width));
    TracyPlot("WebContent/SnapshotVisibleHeight", static_cast<int64_t>(snapshot.visible_height));
    TracyPlot("WebContent/SnapshotBytes", static_cast<int64_t>(snapshot.width) * snapshot.height * 4);
#endif
    m_needs_paint = false;
    return snapshot;
}

ErrorOr<Optional<WebContentBitmapView>> WebContentView::snapshot_bitmap_view_for_current_viewport()
{
#if defined(TRACY_ENABLE)
    ZoneScopedN("Boundary/Ladybird/SnapshotBitmapViewport");
#endif
    if (!m_client_state.has_usable_bitmap || !m_needs_paint)
        return Optional<WebContentBitmapView> {};

    VERIFY(m_client_state.front_bitmap.shared_image_buffer);

    auto expected_size = viewport_size();
    auto bitmap_size = m_client_state.front_bitmap.last_painted_size;
    if (bitmap_size.width() != expected_size.width() || bitmap_size.height() != expected_size.height())
        return Optional<WebContentBitmapView> {};

    auto const* bitmap = m_client_state.front_bitmap.shared_image_buffer->bitmap().ptr();
    auto buffer_size = bitmap->size();
    if (buffer_size.width() != expected_size.width() || buffer_size.height() != expected_size.height())
        return Optional<WebContentBitmapView> {};

    auto snapshot = WebContentBitmapView {
        .width = buffer_size.width(),
        .height = buffer_size.height(),
        .visible_width = static_cast<int>(bitmap_size.width()),
        .visible_height = static_cast<int>(bitmap_size.height()),
        .bitmap = bitmap,
    };
#if defined(TRACY_ENABLE)
    TracyPlot("WebContent/ViewportSnapshotWidth", static_cast<int64_t>(snapshot.width));
    TracyPlot("WebContent/ViewportSnapshotHeight", static_cast<int64_t>(snapshot.height));
    TracyPlot("WebContent/ViewportSnapshotBytes", static_cast<int64_t>(snapshot.width) * snapshot.height * 4);
#endif
    m_needs_paint = false;
    return snapshot;
}

ErrorOr<Optional<WebContentBitmapSnapshot>> WebContentView::snapshot_bitmap()
{
    Gfx::Bitmap const* bitmap = nullptr;
    Web::DevicePixelSize bitmap_size;

    if (m_client_state.has_usable_bitmap) {
        VERIFY(m_client_state.front_bitmap.shared_image_buffer);
        bitmap = m_client_state.front_bitmap.shared_image_buffer->bitmap().ptr();
        bitmap_size = m_client_state.front_bitmap.last_painted_size;
    } else if (m_backup_shared_image_buffer) {
        bitmap = m_backup_shared_image_buffer->bitmap().ptr();
        bitmap_size = m_backup_bitmap_size;
    }

    if (!bitmap)
        return Optional<WebContentBitmapSnapshot> {};

    auto buffer_size = bitmap->size();
    auto pixel_count = buffer_size.width() * buffer_size.height() * 4;
    Vector<u8> pixels;
    TRY(pixels.try_resize(pixel_count));

    for (int y = 0; y < buffer_size.height(); ++y) {
        auto* src = bitmap->scanline_u8(y);
        auto* dst = pixels.data() + (y * buffer_size.width() * 4);
        __builtin_memcpy(dst, src, buffer_size.width() * 4);
    }

    auto snapshot = WebContentBitmapSnapshot {
        .width = buffer_size.width(),
        .height = buffer_size.height(),
        .visible_width = static_cast<int>(bitmap_size.width()),
        .visible_height = static_cast<int>(bitmap_size.height()),
        .pixels = move(pixels),
    };
    m_needs_paint = false;
    return snapshot;
}

ErrorOr<Optional<WebContentBitmapSnapshot>> WebContentView::snapshot_bitmap_for_current_viewport()
{
    if (!m_client_state.has_usable_bitmap)
        return Optional<WebContentBitmapSnapshot> {};

    VERIFY(m_client_state.front_bitmap.shared_image_buffer);

    auto expected_size = viewport_size();
    auto bitmap_size = m_client_state.front_bitmap.last_painted_size;
    if (bitmap_size.width() != expected_size.width() || bitmap_size.height() != expected_size.height())
        return Optional<WebContentBitmapSnapshot> {};

    auto const* bitmap = m_client_state.front_bitmap.shared_image_buffer->bitmap().ptr();
    auto buffer_size = bitmap->size();
    if (buffer_size.width() != expected_size.width() || buffer_size.height() != expected_size.height())
        return Optional<WebContentBitmapSnapshot> {};

    auto pixel_count = buffer_size.width() * buffer_size.height() * 4;
    Vector<u8> pixels;
    TRY(pixels.try_resize(pixel_count));

    for (int y = 0; y < buffer_size.height(); ++y) {
        auto* src = bitmap->scanline_u8(y);
        auto* dst = pixels.data() + (y * buffer_size.width() * 4);
        __builtin_memcpy(dst, src, buffer_size.width() * 4);
    }

    auto snapshot = WebContentBitmapSnapshot {
        .width = buffer_size.width(),
        .height = buffer_size.height(),
        .visible_width = static_cast<int>(bitmap_size.width()),
        .visible_height = static_cast<int>(bitmap_size.height()),
        .pixels = move(pixels),
    };
    m_needs_paint = false;
    return snapshot;
}

void WebContentView::resize(int width, int height)
{
#if defined(TRACY_ENABLE)
    ZoneScopedN("Boundary/Ladybird/WebContentResize");
    TracyPlot("WebContent/ResizeWidth", static_cast<int64_t>(width));
    TracyPlot("WebContent/ResizeHeight", static_cast<int64_t>(height));
#endif
    m_width = width;
    m_height = height;
    m_needs_paint = true;
    handle_resize();
}

void WebContentView::handle_sdl_event(SDL_Event const& event)
{
    auto enqueue_mouse_event = [&](Web::MouseEvent::Type type, float x, float y, Web::UIEvents::MouseButton button, int wheel_delta_x = 0, int wheel_delta_y = 0, int click_count = 0) {
        Web::DevicePixelPoint position { static_cast<int>(x * m_device_pixel_ratio), static_cast<int>(y * m_device_pixel_ratio) };
        enqueue_input_event(Web::MouseEvent {
            type,
            position,
            position,
            button,
            m_pressed_mouse_buttons,
            sdl_modifiers_to_web_modifiers(SDL_GetModState()),
            wheel_delta_x,
            wheel_delta_y,
            click_count,
            nullptr });
    };

    switch (event.type) {
    case SDL_EVENT_MOUSE_BUTTON_DOWN: {
        auto button = sdl_button_to_web_button(event.button.button);
        if (button == Web::UIEvents::MouseButton::None)
            return;
        m_pressed_mouse_buttons |= button;
        enqueue_mouse_event(Web::MouseEvent::Type::MouseDown, event.button.x, event.button.y, button, 0, 0, event.button.clicks);
        break;
    }
    case SDL_EVENT_MOUSE_BUTTON_UP: {
        auto button = sdl_button_to_web_button(event.button.button);
        if (button == Web::UIEvents::MouseButton::None)
            return;
        m_pressed_mouse_buttons &= ~button;
        enqueue_mouse_event(Web::MouseEvent::Type::MouseUp, event.button.x, event.button.y, button, 0, 0, event.button.clicks);
        break;
    }
    case SDL_EVENT_MOUSE_MOTION:
        enqueue_mouse_event(Web::MouseEvent::Type::MouseMove, event.motion.x, event.motion.y, Web::UIEvents::MouseButton::None);
        break;
    case SDL_EVENT_MOUSE_WHEEL: {
        float x = 0.0f;
        float y = 0.0f;
        SDL_GetMouseState(&x, &y);
        enqueue_mouse_event(Web::MouseEvent::Type::MouseWheel, x, y, Web::UIEvents::MouseButton::None, static_cast<int>(event.wheel.x * -120), static_cast<int>(event.wheel.y * -120));
        break;
    }
    case SDL_EVENT_WINDOW_MOUSE_LEAVE: {
        float x = 0.0f;
        float y = 0.0f;
        SDL_GetMouseState(&x, &y);
        enqueue_mouse_event(Web::MouseEvent::Type::MouseLeave, x, y, Web::UIEvents::MouseButton::None);
        break;
    }
    case SDL_EVENT_KEY_DOWN:
    case SDL_EVENT_KEY_UP: {
        auto type = event.type == SDL_EVENT_KEY_DOWN ? Web::KeyEvent::Type::KeyDown : Web::KeyEvent::Type::KeyUp;
        auto key = sdl_key_to_web_key(event.key.key);
        u32 code_point = event.key.key <= 0x10ffff ? static_cast<u32>(event.key.key) : 0;
        enqueue_input_event(Web::KeyEvent { type, key, sdl_modifiers_to_web_modifiers(event.key.mod), code_point, event.key.repeat, nullptr });
        break;
    }
    case SDL_EVENT_TEXT_INPUT:
        if (event.text.text && event.text.text[0]) {
            Utf8View view { StringView { event.text.text, strlen(event.text.text) } };
            if (auto iterator = view.begin(); iterator != view.end())
                enqueue_input_event(Web::KeyEvent { Web::KeyEvent::Type::KeyDown, Web::UIEvents::KeyCode::Key_Invalid, sdl_modifiers_to_web_modifiers(SDL_GetModState()), *iterator, false, nullptr });
        }
        break;
    default:
        break;
    }
}

Web::DevicePixelSize WebContentView::viewport_size() const
{
    return { m_width, m_height };
}

Gfx::IntPoint WebContentView::to_content_position(Gfx::IntPoint point) const
{
    return point;
}

Gfx::IntPoint WebContentView::to_widget_position(Gfx::IntPoint point) const
{
    return point;
}

}
