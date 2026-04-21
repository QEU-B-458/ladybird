/*
 * MyceliumVR - a social VR platform built on the Ladybird browser engine
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <AK/Error.h>
#include <AK/HashMap.h>
#include <AK/Optional.h>
#include <AK/Vector.h>
#include <LibGfx/ExternalVulkanImage.h>
#include <LibIPC/File.h>
#include <LibWebView/ViewImplementation.h>
#include <SDL3/SDL_events.h>

namespace Gfx {
class Bitmap;
}

namespace MyceliumVR {

struct WebContentBitmapSnapshot {
    int width { 0 };
    int height { 0 };
    int visible_width { 0 };
    int visible_height { 0 };
    Vector<u8> pixels;
};

struct WebContentBitmapView {
    int width { 0 };
    int height { 0 };
    int visible_width { 0 };
    int visible_height { 0 };
    Gfx::Bitmap const* bitmap { nullptr };
};

class WebContentView final : public WebView::ViewImplementation {
public:
    WebContentView(int width, int height, bool supports_vulkan_external_images = false, VkDevice vulkan_device = VK_NULL_HANDLE);
    virtual ~WebContentView() override;

    void resize(int width, int height);

    // Forward SDL input events into Ladybird
    void handle_sdl_event(SDL_Event const&);

    bool needs_paint() const;
    ErrorOr<Optional<WebContentBitmapView>> snapshot_bitmap_view();
    ErrorOr<Optional<WebContentBitmapView>> snapshot_bitmap_view_for_current_viewport();
    ErrorOr<Optional<WebContentBitmapSnapshot>> snapshot_bitmap();
    ErrorOr<Optional<WebContentBitmapSnapshot>> snapshot_bitmap_for_current_viewport();

    // Zero-copy Vulkan path: returns the current front VkImage after did_paint fires.
    bool has_vulkan_image() const;
    VkImage current_vulkan_image() const;
    u32 vulkan_image_width() const;
    u32 vulkan_image_height() const;

private:
    // ^WebView::ViewImplementation - required pure virtuals
    virtual Web::DevicePixelSize viewport_size() const override;
    virtual Gfx::IntPoint to_content_position(Gfx::IntPoint) const override;
    virtual Gfx::IntPoint to_widget_position(Gfx::IntPoint) const override;

    // ^WebView::ViewImplementation - Vulkan backing store notification
    virtual void did_allocate_vulkan_backing_stores(Badge<WebView::WebContentClient>, i32 front_image_id, IPC::File front_fd, u64 front_allocation_size, u32 front_memory_type_index, u32 front_format, u32 front_width, u32 front_height, i32 back_image_id, IPC::File back_fd, u64 back_allocation_size, u32 back_memory_type_index, u32 back_format, u32 back_width, u32 back_height) override;

    void destroy_vulkan_images();

    int m_width { 1280 };
    int m_height { 720 };
    bool m_needs_paint { false };
    bool m_supports_vulkan_external_images { false };
    VkDevice m_vulkan_device { VK_NULL_HANDLE };
    u32 m_frames_waiting_for_first_paint { 0 };
    Web::UIEvents::MouseButton m_pressed_mouse_buttons { Web::UIEvents::MouseButton::None };

#if defined(USE_VULKAN)
    struct VulkanImageSlot {
        Gfx::ImportedVulkanImage imported;
        u32 width { 0 };
        u32 height { 0 };
    };
    HashMap<i32, VulkanImageSlot> m_vulkan_images;
#endif
};

}
