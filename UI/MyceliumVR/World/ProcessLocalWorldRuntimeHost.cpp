/*
 * MyceliumVR - a social VR platform built on the Ladybird browser engine
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "ProcessLocalWorldRuntimeHost.h"

#include "../Rendering/Renderer.h"

namespace MyceliumVR {

ProcessLocalWorldRuntimeHost::ProcessLocalWorldRuntimeHost(Renderer& renderer)
    : m_renderer(renderer)
{
}

void ProcessLocalWorldRuntimeHost::set_camera_state(CameraState const& camera_state)
{
    m_renderer.set_camera_state(camera_state);
}

CameraState ProcessLocalWorldRuntimeHost::camera_state() const
{
    return m_renderer.camera_state();
}

void ProcessLocalWorldRuntimeHost::set_scene_light(SceneLightData const& scene_light)
{
    m_renderer.set_scene_light(scene_light);
}

SceneLightData ProcessLocalWorldRuntimeHost::scene_light() const
{
    return m_renderer.scene_light();
}

}
