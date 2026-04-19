/*
 * MyceliumVR - a social VR platform built on the Ladybird browser engine
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "BridgeBackend.h"

namespace MyceliumVR {

BridgeBackend::BridgeBackend(World& world, Function<void(VulkanRenderer::CameraState const&)> set_camera_state, Function<VulkanRenderer::CameraState()> camera_state, Function<void(VulkanRenderer::SceneLightData const&)> set_scene_light, Function<VulkanRenderer::SceneLightData()> scene_light)
    : m_world(world)
    , m_set_camera_state(move(set_camera_state))
    , m_camera_state(move(camera_state))
    , m_set_scene_light(move(set_scene_light))
    , m_scene_light(move(scene_light))
{
}

void BridgeBackend::log(String message)
{
    outln("[mycelium] {}", message);
}

EntityId BridgeBackend::spawn_entity()
{
    return m_world.spawn_entity();
}

bool BridgeBackend::destroy_entity(EntityId entity)
{
    return m_world.destroy_entity(entity);
}

u32 BridgeBackend::entity_count() const
{
    return static_cast<u32>(m_world.alive_entity_count());
}

u32 BridgeBackend::dirty_transform_count() const
{
    return static_cast<u32>(m_world.dirty_transform_count());
}

bool BridgeBackend::set_transform(EntityId entity, double px, double py, double pz, double qx, double qy, double qz, double qw, double sx, double sy, double sz)
{
    Transform transform;
    transform.position[0] = static_cast<float>(px);
    transform.position[1] = static_cast<float>(py);
    transform.position[2] = static_cast<float>(pz);
    transform.rotation[0] = static_cast<float>(qx);
    transform.rotation[1] = static_cast<float>(qy);
    transform.rotation[2] = static_cast<float>(qz);
    transform.rotation[3] = static_cast<float>(qw);
    transform.scale[0] = static_cast<float>(sx);
    transform.scale[1] = static_cast<float>(sy);
    transform.scale[2] = static_cast<float>(sz);
    return m_world.set_transform(entity, transform);
}

u32 BridgeBackend::commit_transform_buffer(ReadonlySpan<float> buffer, u32 count)
{
    constexpr size_t transform_stride = 11;
    auto available_rows = buffer.size() / transform_stride;
    auto rows_to_apply = min(static_cast<size_t>(count), available_rows);

    u32 applied_count = 0;
    for (size_t row_index = 0; row_index < rows_to_apply; ++row_index) {
        auto row = buffer.slice(row_index * transform_stride, transform_stride);
        auto entity = static_cast<EntityId>(static_cast<u32>(row[0]));
        if (!m_world.registry().valid(entity))
            continue;

        Transform transform;
        transform.position[0] = row[1];
        transform.position[1] = row[2];
        transform.position[2] = row[3];
        transform.rotation[0] = row[4];
        transform.rotation[1] = row[5];
        transform.rotation[2] = row[6];
        transform.rotation[3] = row[7];
        transform.scale[0] = row[8];
        transform.scale[1] = row[9];
        transform.scale[2] = row[10];

        if (m_world.set_transform(entity, transform))
            ++applied_count;
    }

    return applied_count;
}

bool BridgeBackend::set_mesh(EntityId entity, String mesh)
{
    return m_world.set_mesh(entity, move(mesh));
}

bool BridgeBackend::set_material(EntityId entity, String material)
{
    return m_world.set_material(entity, move(material));
}

bool BridgeBackend::set_normal_map(EntityId entity, String normal_map)
{
    return m_world.set_normal_map(entity, move(normal_map));
}

bool BridgeBackend::create_panel(EntityId entity, String url, double width, double height)
{
    return m_world.create_panel(entity, move(url), static_cast<float>(width), static_cast<float>(height));
}

Optional<EntityId> BridgeBackend::entity_id_at(u32 index) const
{
    u32 i = 0;
    auto view = m_world.registry().view<Transform>();
    for (auto entity : view) {
        if (i == index)
            return entity;
        ++i;
    }
    return {};
}

Optional<Transform> BridgeBackend::transform_for_entity(EntityId entity) const
{
    auto const* transform = m_world.registry().try_get<Transform>(entity);
    if (!transform)
        return {};
    return *transform;
}

void BridgeBackend::set_camera(double px, double py, double pz, double yaw_degrees, double pitch_degrees)
{
    if (!m_set_camera_state)
        return;

    VulkanRenderer::CameraState camera_state {};
    camera_state.position[0] = static_cast<float>(px);
    camera_state.position[1] = static_cast<float>(py);
    camera_state.position[2] = static_cast<float>(pz);
    camera_state.yaw_degrees = static_cast<float>(yaw_degrees);
    camera_state.pitch_degrees = static_cast<float>(pitch_degrees);
    m_set_camera_state(camera_state);
}

Optional<VulkanRenderer::CameraState> BridgeBackend::camera_state() const
{
    if (!m_camera_state)
        return {};
    return m_camera_state();
}

void BridgeBackend::set_ambient_light(double r, double g, double b, double intensity)
{
    if (!m_set_scene_light)
        return;
    auto light = m_scene_light ? m_scene_light() : VulkanRenderer::SceneLightData {};
    light.ambient_rgb[0] = static_cast<float>(r);
    light.ambient_rgb[1] = static_cast<float>(g);
    light.ambient_rgb[2] = static_cast<float>(b);
    light.ambient_intensity = static_cast<float>(intensity);
    m_set_scene_light(light);
}

void BridgeBackend::set_directional_light(double to_x, double to_y, double to_z, double r, double g, double b, double intensity)
{
    if (!m_set_scene_light)
        return;
    auto light = m_scene_light ? m_scene_light() : VulkanRenderer::SceneLightData {};
    light.light_to_xyz[0] = static_cast<float>(to_x);
    light.light_to_xyz[1] = static_cast<float>(to_y);
    light.light_to_xyz[2] = static_cast<float>(to_z);
    light.light_rgb[0] = static_cast<float>(r);
    light.light_rgb[1] = static_cast<float>(g);
    light.light_rgb[2] = static_cast<float>(b);
    light.light_intensity = static_cast<float>(intensity);
    m_set_scene_light(light);
}

void BridgeBackend::set_point_light(int index, double x, double y, double z, double r, double g, double b, double intensity, double radius)
{
    if (!m_set_scene_light || index < 0 || index >= MaxPointLights)
        return;
    auto light = m_scene_light ? m_scene_light() : VulkanRenderer::SceneLightData {};
    auto& pl = light.point_lights[index];
    pl.position[0] = static_cast<float>(x);
    pl.position[1] = static_cast<float>(y);
    pl.position[2] = static_cast<float>(z);
    pl.color[0] = static_cast<float>(r);
    pl.color[1] = static_cast<float>(g);
    pl.color[2] = static_cast<float>(b);
    pl.intensity = static_cast<float>(intensity);
    pl.radius = static_cast<float>(radius);
    if (index >= light.point_light_count)
        light.point_light_count = index + 1;
    m_set_scene_light(light);
}

void BridgeBackend::set_spot_light(int index, double x, double y, double z, double dx, double dy, double dz, double inner_deg, double outer_deg, double r, double g, double b, double intensity, double radius)
{
    if (!m_set_scene_light || index < 0 || index >= MaxPointLights)
        return;
    auto light = m_scene_light ? m_scene_light() : VulkanRenderer::SceneLightData {};
    auto& pl = light.point_lights[index];
    pl.position[0] = static_cast<float>(x);
    pl.position[1] = static_cast<float>(y);
    pl.position[2] = static_cast<float>(z);
    pl.color[0] = static_cast<float>(r);
    pl.color[1] = static_cast<float>(g);
    pl.color[2] = static_cast<float>(b);
    pl.intensity = static_cast<float>(intensity);
    pl.radius    = static_cast<float>(radius);
    // Normalize direction.
    float len = __builtin_sqrtf(static_cast<float>(dx*dx + dy*dy + dz*dz));
    if (len > 0.00001f) {
        pl.direction[0] = static_cast<float>(dx) / len;
        pl.direction[1] = static_cast<float>(dy) / len;
        pl.direction[2] = static_cast<float>(dz) / len;
    }
    // Convert half-angles from degrees to cosines.
    float inner_rad = static_cast<float>(inner_deg) * (3.14159265f / 180.0f);
    float outer_rad = static_cast<float>(outer_deg) * (3.14159265f / 180.0f);
    pl.cone_inner_cos = __builtin_cosf(inner_rad);
    pl.cone_outer_cos = __builtin_cosf(outer_rad);
    pl.type = 1; // spot
    if (index >= light.point_light_count)
        light.point_light_count = index + 1;
    m_set_scene_light(light);
}

void BridgeBackend::clear_point_lights()
{
    if (!m_set_scene_light)
        return;
    auto light = m_scene_light ? m_scene_light() : VulkanRenderer::SceneLightData {};
    light.point_light_count = 0;
    m_set_scene_light(light);
}

Optional<VulkanRenderer::SceneLightData> BridgeBackend::scene_light() const
{
    if (!m_scene_light)
        return {};
    return m_scene_light();
}

}
