/*
 * MyceliumVR - a social VR platform built on the Ladybird browser engine
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include "../Rendering/Renderer.h"
#include "../World/Session.h"

namespace MyceliumVR {

class VirtualFileSystem;

class Engine {
public:
    explicit Engine(SDL_Window&, VirtualFileSystem const* = nullptr);

    Session& session() { return m_session; }
    Session const& session() const { return m_session; }

    World& active_world() { return m_session.active_world(); }
    World const& active_world() const { return m_session.active_world(); }

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

    void resize(int width, int height);
    ErrorOr<void> render();

private:
    Session m_session;
    Renderer m_renderer;
};

}
