/*
 * MyceliumVR - a social VR platform built on the Ladybird browser engine
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include "../World/World.h"
#include "../Rendering/VulkanRenderer.h"

#include <AK/Function.h>
#include <AK/Optional.h>
#include <AK/Span.h>

namespace MyceliumVR {

class BridgeBackend {
public:
    explicit BridgeBackend(World&,
        Function<void(VulkanRenderer::CameraState const&)> = {},
        Function<VulkanRenderer::CameraState()> = {},
        Function<void(VulkanRenderer::SceneLightData const&)> = {},
        Function<VulkanRenderer::SceneLightData()> = {});

    void log(String message);

    EntityId spawn_entity();
    bool destroy_entity(EntityId);

    u32 entity_count() const;
    u32 dirty_transform_count() const;

    bool set_transform(EntityId, double px, double py, double pz, double qx, double qy, double qz, double qw, double sx, double sy, double sz);
    u32 commit_transform_buffer(ReadonlySpan<float> buffer, u32 count);
    bool set_mesh(EntityId, String mesh);
    bool set_material(EntityId, String material);
    bool set_normal_map(EntityId, String normal_map);
    bool create_panel(EntityId, String url, double width, double height);
    Optional<EntityId> entity_id_at(u32 index) const;
    Optional<Transform> transform_for_entity(EntityId) const;
    void set_camera(double px, double py, double pz, double yaw_degrees, double pitch_degrees);
    Optional<VulkanRenderer::CameraState> camera_state() const;
    void set_ambient_light(double r, double g, double b, double intensity);
    void set_directional_light(double to_x, double to_y, double to_z, double r, double g, double b, double intensity);
    void set_point_light(int index, double x, double y, double z, double r, double g, double b, double intensity, double radius);
    void set_spot_light(int index, double x, double y, double z, double dx, double dy, double dz, double inner_deg, double outer_deg, double r, double g, double b, double intensity, double radius);
    void clear_point_lights();
    Optional<VulkanRenderer::SceneLightData> scene_light() const;

    World& world() { return m_world; }
    World const& world() const { return m_world; }

private:
    World& m_world;
    Function<void(VulkanRenderer::CameraState const&)> m_set_camera_state;
    Function<VulkanRenderer::CameraState()> m_camera_state;
    Function<void(VulkanRenderer::SceneLightData const&)> m_set_scene_light;
    Function<VulkanRenderer::SceneLightData()> m_scene_light;
};

}
