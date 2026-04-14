/*
 * MyceliumVR - a social VR platform built on the Ladybird browser engine
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include "../World/World.h"

#include <AK/Error.h>
#include <AK/NonnullOwnPtr.h>
#include <AK/OwnPtr.h>
#include <AK/Vector.h>
#include <SDL3/SDL.h>

namespace Gfx {
class Bitmap;
}

namespace MyceliumVR {

class VirtualFileSystem;

class VulkanRenderer {
public:
    struct BitmapView {
        Gfx::Bitmap const* bitmap { nullptr };
        u32 width { 0 };
        u32 height { 0 };
    };

    struct Impl;

    struct CameraState {
        float position[3] { 0.0f, 0.0f, 6.0f };
        float yaw_degrees { 0.0f };
        float pitch_degrees { 0.0f };
    };

    struct SceneLightData {
        float ambient_rgb[3] { 0.15f, 0.18f, 0.25f };  // cool blue ambient sky
        float ambient_intensity { 1.0f };
        float light_to_xyz[3] { 0.408f, 0.816f, 0.408f };  // normalized: sun from upper-right-front
        float light_intensity { 1.0f };
        float light_rgb[3] { 1.0f, 0.93f, 0.80f };  // warm sunlight

        static constexpr int MaxPointLights = 8;
        struct PointLight {
            float position[3] {};
            float radius { 5.0f };
            float color[3] { 1.0f, 1.0f, 1.0f };
            float intensity { 1.0f };
            // Spot light fields — ignored when type == 0 (point).
            float direction[3] {};       // normalized world-space direction the spot points
            float unused0 { 0.0f };
            float cone_inner_cos { 1.0f }; // cos(inner half-angle); 1.0 = no cone (point light)
            float cone_outer_cos { 1.0f }; // cos(outer half-angle); must be <= cone_inner_cos
            int   type { 0 };              // 0 = point, 1 = spot
            float unused1 { 0.0f };
        };
        PointLight point_lights[MaxPointLights] {};
        int point_light_count { 0 };
    };

    struct OverlayView {
        Vector<u8> pixels;
        u32 width { 0 };
        u32 height { 0 };
    };

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

private:
    explicit VulkanRenderer(SDL_Window&, VirtualFileSystem const*);
    ErrorOr<void> initialize();
    void destroy();

    SDL_Window& m_window;
    VirtualFileSystem const* m_file_system { nullptr };
    bool m_initialized { false };

#if defined(USE_VULKAN)
    OwnPtr<Impl> m_impl;
#endif
};

}
