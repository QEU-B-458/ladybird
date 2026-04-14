/*
 * MyceliumVR - a social VR platform built on the Ladybird browser engine
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "WebContentView.h"

#include <LibGfx/Bitmap.h>
#include <LibGfx/SharedImageBuffer.h>
#include <LibWeb/UIEvents/MouseButton.h>

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

WebContentView::WebContentView(int width, int height)
    : m_width(width)
    , m_height(height)
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

WebContentView::~WebContentView() { }

bool WebContentView::needs_paint() const
{
    return m_needs_paint || !m_client_state.has_usable_bitmap;
}

ErrorOr<Optional<WebContentBitmapView>> WebContentView::snapshot_bitmap_view()
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
        return Optional<WebContentBitmapView> {};

    auto snapshot = WebContentBitmapView {
        .width = bitmap->width(),
        .height = bitmap->height(),
        .visible_width = static_cast<int>(bitmap_size.width()),
        .visible_height = static_cast<int>(bitmap_size.height()),
        .bitmap = bitmap,
    };
    m_needs_paint = false;
    return snapshot;
}

ErrorOr<Optional<WebContentBitmapView>> WebContentView::snapshot_bitmap_view_for_current_viewport()
{
    if (!m_client_state.has_usable_bitmap)
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
