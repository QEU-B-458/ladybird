/*
 * MyceliumVR - a social VR platform built on the Ladybird browser engine
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "Renderer.h"

namespace MyceliumVR {

Renderer::Renderer(SDL_Window& window, VirtualFileSystem const* file_system)
{
    m_vulkan_renderer = MUST(VulkanRenderer::create(window, file_system));
    outln("Renderer: using Vulkan backend");
}

void Renderer::resize(int width, int height)
{
    std::lock_guard lock(m_mutex);
    if (m_vulkan_renderer)
        m_vulkan_renderer->resize(width, height);
}

void Renderer::sync_web_views(u32 world_id, World const& world)
{
    std::lock_guard lock(m_mutex);
    if (m_vulkan_renderer)
        m_vulkan_renderer->sync_web_views(world_id, world);
}

void Renderer::handle_web_view_event(SDL_Event const& event)
{
    std::lock_guard lock(m_mutex);
    if (m_vulkan_renderer)
        m_vulkan_renderer->handle_web_view_event(event);
}

void Renderer::set_camera_state(VulkanRenderer::CameraState const& camera_state)
{
    std::lock_guard lock(m_mutex);
    VERIFY(m_vulkan_renderer);
    m_vulkan_renderer->set_camera_state(camera_state);
}

VulkanRenderer::CameraState Renderer::camera_state() const
{
    std::lock_guard lock(m_mutex);
    VERIFY(m_vulkan_renderer);
    return m_vulkan_renderer->camera_state();
}

void Renderer::set_scene_light(VulkanRenderer::SceneLightData const& light)
{
    std::lock_guard lock(m_mutex);
    VERIFY(m_vulkan_renderer);
    m_vulkan_renderer->set_scene_light(light);
}

VulkanRenderer::SceneLightData Renderer::scene_light() const
{
    std::lock_guard lock(m_mutex);
    VERIFY(m_vulkan_renderer);
    return m_vulkan_renderer->scene_light();
}

void Renderer::set_shadow_quality(VulkanRenderer::ShadowQuality shadow_quality)
{
    std::lock_guard lock(m_mutex);
    VERIFY(m_vulkan_renderer);
    m_vulkan_renderer->set_shadow_quality(shadow_quality);
}

VulkanRenderer::ShadowQuality Renderer::shadow_quality() const
{
    std::lock_guard lock(m_mutex);
    VERIFY(m_vulkan_renderer);
    return m_vulkan_renderer->shadow_quality();
}

void Renderer::wait_for_graphics_queue_idle()
{
    std::lock_guard lock(m_mutex);
    if (m_vulkan_renderer)
        m_vulkan_renderer->wait_for_graphics_queue_idle();
}

void Renderer::wait_for_device_idle()
{
    std::lock_guard lock(m_mutex);
    if (m_vulkan_renderer)
        m_vulkan_renderer->wait_for_device_idle();
}

void Renderer::clear_overlay_bitmap()
{
    if (m_vulkan_renderer)
        m_vulkan_renderer->clear_overlay_bitmap();
}

ErrorOr<void> Renderer::draw_world(World const& world)
{
    std::lock_guard lock(m_mutex);
    VERIFY(m_vulkan_renderer);
    TRY(m_vulkan_renderer->draw_world(world));
    return {};
}

ErrorOr<void> Renderer::draw_snapshot(ReadonlySpan<u8> slot, ReadonlySpan<u8> static_scene)
{
    std::lock_guard lock(m_mutex);
    VERIFY(m_vulkan_renderer);
    TRY(m_vulkan_renderer->draw_snapshot(slot, static_scene));
    return {};
}

ErrorOr<void> Renderer::sync_snapshot_buffers()
{
    std::lock_guard lock(m_mutex);
    if (m_vulkan_renderer)
        TRY(m_vulkan_renderer->sync_snapshot_buffers());
    return {};
}

void Renderer::register_mesh(u32 handle, ByteString const& virtual_path)
{
    std::lock_guard lock(m_mutex);
    if (m_vulkan_renderer)
        m_vulkan_renderer->register_mesh(handle, virtual_path);
}

void Renderer::register_material(u32 handle, ByteString const& virtual_path)
{
    std::lock_guard lock(m_mutex);
    if (m_vulkan_renderer)
        m_vulkan_renderer->register_material(handle, virtual_path);
}

void Renderer::register_panel(u32 handle, u32 entity_id, ByteString const& url, float width, float height)
{
    std::lock_guard lock(m_mutex);
    if (m_vulkan_renderer) m_vulkan_renderer->register_panel(handle, entity_id, url, width, height);
}

void Renderer::force_rebuild_scene()
{
    std::lock_guard lock(m_mutex);
    if (m_vulkan_renderer) m_vulkan_renderer->force_rebuild_scene();
}

void Renderer::unload_world_resources(u32 world_id)
{
    std::lock_guard lock(m_mutex);
    if (m_vulkan_renderer)
        m_vulkan_renderer->unload_world_resources(world_id);
}

void Renderer::set_external_overlay_image(VkImage image, u32 width, u32 height)
{
    if (m_vulkan_renderer)
        m_vulkan_renderer->set_external_overlay_image(image, width, height);
}

void Renderer::clear_external_overlay()
{
    if (m_vulkan_renderer)
        m_vulkan_renderer->clear_external_overlay();
}

void Renderer::post_render()
{
    std::lock_guard lock(m_mutex);
    if (m_vulkan_renderer)
        m_vulkan_renderer->post_render();
}

bool Renderer::supports_external_image_import() const
{
    std::lock_guard lock(m_mutex);
    return m_vulkan_renderer && m_vulkan_renderer->supports_external_image_import();
}

VkDevice Renderer::vulkan_device() const
{
    std::lock_guard lock(m_mutex);
    return m_vulkan_renderer ? m_vulkan_renderer->vulkan_device() : VK_NULL_HANDLE;
}

u32 Renderer::last_frame_draw_calls() const
{
    std::lock_guard lock(m_mutex);
    return m_vulkan_renderer ? m_vulkan_renderer->last_frame_draw_calls() : 0;
}

u32 Renderer::last_frame_triangle_count() const
{
    std::lock_guard lock(m_mutex);
    return m_vulkan_renderer ? m_vulkan_renderer->last_frame_triangle_count() : 0;
}

Vector<VulkanRenderer::PassTiming> Renderer::last_frame_timings() const
{
    std::lock_guard lock(m_mutex);
    return m_vulkan_renderer ? m_vulkan_renderer->last_frame_timings() : Vector<VulkanRenderer::PassTiming> {};
}

}
