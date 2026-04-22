/*
 * MyceliumVR - a social VR platform built on the Ladybird browser engine
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include "../Rendering/Renderer.h"
#include "../World/WorldManagementSystem.h"

namespace MyceliumVR {

class InputState;
class VirtualFileSystem;

class Engine {
public:
    explicit Engine(SDL_Window&, VirtualFileSystem const* = nullptr);
    ErrorOr<void> initialize_world_management_system(VirtualFileSystem*, InputState*);

    WorldManagementSystem& world_management_system() { return m_world_management_system; }
    WorldManagementSystem const& world_management_system() const { return m_world_management_system; }
    Session& session() { return m_world_management_system.session(); }
    Session const& session() const { return m_world_management_system.session(); }

    World& active_world() { return m_world_management_system.foreground_runtime()->world(); }
    World const& active_world() const { return m_world_management_system.foreground_runtime()->world(); }

    World& world() { return active_world(); }
    World const& world() const { return active_world(); }

    Renderer& renderer() { return m_renderer; }
    Renderer const& renderer() const { return m_renderer; }
    void set_camera_state(VulkanRenderer::CameraState const& camera_state) { m_renderer.set_camera_state(camera_state); }
    VulkanRenderer::CameraState camera_state() const { return m_renderer.camera_state(); }
    void set_scene_light(VulkanRenderer::SceneLightData const& light) { m_renderer.set_scene_light(light); }
    VulkanRenderer::SceneLightData scene_light() const { return m_renderer.scene_light(); }
    void set_shadow_quality(VulkanRenderer::ShadowQuality shadow_quality) { m_renderer.set_shadow_quality(shadow_quality); }
    VulkanRenderer::ShadowQuality shadow_quality() const { return m_renderer.shadow_quality(); }
    bool supports_external_image_import() const { return m_renderer.supports_external_image_import(); }
    VkDevice vulkan_device() const { return m_renderer.vulkan_device(); }

    void resize(int width, int height);
    ErrorOr<void> render();

private:
    Renderer m_renderer;
    WorldManagementSystem m_world_management_system;
};

}
