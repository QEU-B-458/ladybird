/*
 * MyceliumVR - a social VR platform built on the Ladybird browser engine
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include "WorldRuntimeHost.h"

namespace MyceliumVR {

class Renderer;

class ProcessLocalWorldRuntimeHost final : public WorldRuntimeHost {
public:
    explicit ProcessLocalWorldRuntimeHost(Renderer&);

    virtual void set_camera_state(CameraState const&) override;
    virtual CameraState camera_state() const override;
    virtual void set_scene_light(SceneLightData const&) override;
    virtual SceneLightData scene_light() const override;

private:
    Renderer& m_renderer;
};

}
