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
#include <mutex>
namespace MyceliumVR {

class VirtualFileSystem;

class Renderer {
public:
    explicit Renderer(SDL_Window&, VirtualFileSystem const* = nullptr);

    void resize(int width, int height);
    void sync_web_views(u32 world_id, World const&);
    void handle_web_view_event(SDL_Event const&);
    void set_camera_state(VulkanRenderer::CameraState const&);
    VulkanRenderer::CameraState camera_state() const;
    void set_scene_light(VulkanRenderer::SceneLightData const&);
    VulkanRenderer::SceneLightData scene_light() const;
    void set_shadow_quality(VulkanRenderer::ShadowQuality);
    VulkanRenderer::ShadowQuality shadow_quality() const;
    void wait_for_graphics_queue_idle();
    void wait_for_device_idle();
    void clear_overlay_bitmap();
    void set_external_overlay_image(VkImage image, u32 width, u32 height);
    void clear_external_overlay();
    void post_render();
    ErrorOr<void> draw_world(World const&);
    ErrorOr<void> draw_snapshot(ReadonlySpan<u8> slot, ReadonlySpan<u8> static_scene = {});
    ErrorOr<void> sync_snapshot_buffers();
    void register_mesh(u32 handle, ByteString const& virtual_path);
    void register_material(u32 handle, ByteString const& virtual_path);
    void register_panel(u32 handle, u32 entity_id, ByteString const& url, float width, float height);
    void force_rebuild_scene();
    void unload_world_resources(u32 world_id);

    bool is_using_vulkan() const { return true; }
    bool supports_external_image_import() const;
    VkDevice vulkan_device() const;
    u32 last_frame_draw_calls() const;
    u32 last_frame_triangle_count() const;
    Vector<VulkanRenderer::PassTiming> last_frame_timings() const;

private:
    mutable std::mutex m_mutex;
    OwnPtr<VulkanRenderer> m_vulkan_renderer;
};

}
