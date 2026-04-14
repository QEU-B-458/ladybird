/*
 * MyceliumVR - a social VR platform built on the Ladybird browser engine
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include "../World/World.h"
#include "VulkanRenderer.h"

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
    void set_panel_bitmap_view(VulkanRenderer::BitmapView);
    void set_panel_bitmap(Vector<u8>, u32 width, u32 height);
    void clear_panel_bitmap();
    void set_overlay_bitmap_view(VulkanRenderer::BitmapView);
    void set_overlay_view(VulkanRenderer::OverlayView);
    void clear_overlay_bitmap();
    ErrorOr<void> draw_world(World const&);
    bool is_using_vulkan() const { return true; }

private:
    OwnPtr<VulkanRenderer> m_vulkan_renderer;
};

}
