/*
 * MyceliumVR - a social VR platform built on the Ladybird browser engine
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include "VulkanBackend.h"
#include "../VulkanCommon.h"
#include "VulkanContext.h"
#include "../Passes/PostProcessPass.h"
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

    struct PassTiming {
        String name;
        double gpu_ms { 0.0 };
    };

    struct Impl;

    static ErrorOr<NonnullOwnPtr<VulkanRenderer>> create(SDL_Window&, VirtualFileSystem const* = nullptr);
    ~VulkanRenderer();

    ErrorOr<void> draw_test_triangle();
    ErrorOr<void> draw_world(World const&);
    ErrorOr<void> draw_snapshot(ReadonlySpan<u8> snapshot, ReadonlySpan<u8> static_scene = {});
    ErrorOr<void> sync_snapshot_buffers();
    void post_render();

    void register_mesh(u32 handle, ByteString const& virtual_path);
    void register_material(u32 handle, ByteString const& virtual_path);
    void register_panel(u32 handle, u32 entity_id, ByteString const& url, float width, float height);
    void force_rebuild_scene();
    void unload_world_resources(u32 world_id);
    void resize(int width, int height);
    void sync_web_views(u32 world_id, World const&);
    void handle_web_view_event(SDL_Event const&);
    void clear_overlay_bitmap();
    void set_external_overlay_image(VkImage image, u32 width, u32 height);
    void clear_external_overlay();
    void set_camera_state(CameraState const&);
    CameraState camera_state() const;
    void set_scene_light(SceneLightData const&);
    SceneLightData scene_light() const;
    void set_shadow_quality(ShadowQuality);
    ShadowQuality shadow_quality() const;
    void wait_for_graphics_queue_idle();
    void wait_for_device_idle();
    bool supports_external_image_import() const;
    VkDevice vulkan_device() const;
    u32 last_frame_draw_calls() const;
    u32 last_frame_triangle_count() const;
    Vector<PassTiming> last_frame_timings() const;

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
