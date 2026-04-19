/*
 * MyceliumVR - a social VR platform built on the Ladybird browser engine
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include "VulkanBackend.h"
#include "../VulkanCommon.h"
#include "VulkanContext.h"
#include "../../World/World.h"

#include <AK/Error.h>
#include <AK/NonnullOwnPtr.h>
#include <AK/OwnPtr.h>
#include <AK/Vector.h>
#include <SDL3/SDL.h>

namespace MyceliumVR {

class VirtualFileSystem;

class VulkanRenderer {
public:
    using CameraState = MyceliumVR::CameraState;
    using SceneLightData = MyceliumVR::SceneLightData;
    using ShadowQuality = MyceliumVR::ShadowQuality;

    struct BitmapView {
        Gfx::Bitmap const* bitmap { nullptr };
        u32 width { 0 };
        u32 height { 0 };
    };

    struct OverlayView {
        Vector<u8> pixels;
        u32 width { 0 };
        u32 height { 0 };
    };

    struct Impl;

    static ErrorOr<NonnullOwnPtr<VulkanRenderer>> create(SDL_Window&, VirtualFileSystem const* = nullptr);
    ~VulkanRenderer();

    ErrorOr<void> draw_test_triangle();
    ErrorOr<void> draw_world(World const&);
    void resize(int width, int height);
    void set_panel_bitmap_view(BitmapView);
    void set_panel_bitmap(Vector<u8>, u32 width, u32 height);
    void clear_panel_bitmap();
    void set_overlay_bitmap_view(BitmapView);
    void set_overlay_view(OverlayView);
    void clear_overlay_bitmap();
    void set_camera_state(CameraState const&);
    CameraState camera_state() const;
    void set_scene_light(SceneLightData const&);
    SceneLightData scene_light() const;
    void set_shadow_quality(ShadowQuality);
    ShadowQuality shadow_quality() const;

private:
    explicit VulkanRenderer(SDL_Window&, VirtualFileSystem const*);
    ErrorOr<void> initialize();
    void destroy();

    SDL_Window& m_window;
    VirtualFileSystem const* m_file_system { nullptr };
    bool m_initialized { false };

#if defined(USE_VULKAN)
    OwnPtr<VulkanContext> m_context;
    OwnPtr<Impl> m_impl;
#endif
};

}
