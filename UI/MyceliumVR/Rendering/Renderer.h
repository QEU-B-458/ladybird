/*
 * MyceliumVR - a social VR platform built on the Ladybird browser engine
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include "../World/World.h"
#include "Backend/VulkanRenderer.h"

#include <AK/OwnPtr.h>
#include <AK/Vector.h>
#include <SDL3/SDL.h>
namespace MyceliumVR {

class VirtualFileSystem;

class Renderer {
public:
    explicit Renderer(SDL_Window&, VirtualFileSystem const* = nullptr);

    void resize(int width, int height);
    void set_camera_state(VulkanRenderer::CameraState const&);
    VulkanRenderer::CameraState camera_state() const;
    void set_scene_light(VulkanRenderer::SceneLightData const&);
    VulkanRenderer::SceneLightData scene_light() const;
    void set_shadow_quality(VulkanRenderer::ShadowQuality);
    VulkanRenderer::ShadowQuality shadow_quality() const;
    void set_panel_bitmap_view(VulkanRenderer::BitmapView);
    void set_panel_bitmap(Vector<u8>, u32 width, u32 height);
    void clear_panel_bitmap();
    void set_overlay_bitmap_view(VulkanRenderer::BitmapView);
    void set_overlay_view(VulkanRenderer::OverlayView);
    void clear_overlay_bitmap();
    void set_external_overlay_image(VkImage image, u32 width, u32 height);
    void clear_external_overlay();
    ErrorOr<void> draw_world(World const&);
    bool is_using_vulkan() const { return true; }
    bool supports_external_image_import() const;
    VkDevice vulkan_device() const;
    u32 last_frame_draw_calls() const;
    u32 last_frame_triangle_count() const;
    Vector<VulkanRenderer::PassTiming> last_frame_timings() const;

private:
    OwnPtr<VulkanRenderer> m_vulkan_renderer;
};

}
