/*
 * MyceliumVR - a social VR platform built on the Ladybird browser engine
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <AK/ByteString.h>
#include <AK/Types.h>

namespace MyceliumVR {

using WorldId = u32;

static constexpr int MaxPointLights = 64;

struct CameraState {
    float position[3] { 0.0f, 0.0f, 6.0f };
    float yaw_degrees { 0.0f };
    float pitch_degrees { 0.0f };
};

struct SceneLightData {
    float ambient_rgb[3] { 0.15f, 0.18f, 0.25f };
    float ambient_intensity { 1.0f };
    float light_to_xyz[3] { 0.408f, 0.816f, 0.408f };
    float light_intensity { 1.0f };
    float light_rgb[3] { 1.0f, 0.93f, 0.80f };

    struct PointLight {
        float position[3] {};
        float radius { 5.0f };
        float color[3] { 1.0f, 1.0f, 1.0f };
        float intensity { 1.0f };
        float direction[3] {};
        float unused0 { 0.0f };
        float cone_inner_cos { 1.0f };
        float cone_outer_cos { 1.0f };
        int type { 0 };
        float unused1 { 0.0f };
    };

    PointLight point_lights[MaxPointLights] {};
    int point_light_count { 0 };
};

class WorldRuntimeHost {
public:
    virtual ~WorldRuntimeHost() = default;

    virtual void set_camera_state(CameraState const&) = 0;
    virtual CameraState camera_state() const = 0;
    virtual void set_scene_light(SceneLightData const&) = 0;
    virtual SceneLightData scene_light() const = 0;
    virtual void set_runtime_state(u32 state) = 0;

    virtual u32 register_mesh(WorldId world_id, ByteString const& virtual_path) = 0;
    virtual u32 register_material(WorldId world_id, ByteString const& virtual_path) = 0;
    virtual u32 register_panel(WorldId world_id, u32 entity_id, ByteString const& url, float width, float height) = 0;
};

}
