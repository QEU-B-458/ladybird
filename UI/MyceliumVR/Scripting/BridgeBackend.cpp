/*
 * MyceliumVR - a social VR platform built on the Ladybird browser engine
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "BridgeBackend.h"
#include "../Support/Profiling.h"

namespace MyceliumVR {

BridgeBackend::BridgeBackend(World& world, WorldRuntimeHost* runtime_host)
    : m_world(world)
    , m_runtime_host(runtime_host)
{
}

void BridgeBackend::log(String message)
{
    outln("[mycelium] {}", message);
    if (m_log_callback)
        m_log_callback("info"sv, "script"sv, message.bytes_as_string_view());
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
    ZoneScopedN("WorldProcess/Bridge/SetCamera");
    if (!m_runtime_host)
        return;

    static bool s_logged_first_camera_update = false;
    if (!s_logged_first_camera_update) {
        s_logged_first_camera_update = true;
        outln("MyceliumWorld: received first camera update from script");
    }

    CameraState camera_state {};
    camera_state.position[0] = static_cast<float>(px);
    camera_state.position[1] = static_cast<float>(py);
    camera_state.position[2] = static_cast<float>(pz);
    camera_state.yaw_degrees = static_cast<float>(yaw_degrees);
    camera_state.pitch_degrees = static_cast<float>(pitch_degrees);
    m_runtime_host->set_camera_state(camera_state);
}

Optional<CameraState> BridgeBackend::camera_state() const
{
    if (!m_runtime_host)
        return {};
    return m_runtime_host->camera_state();
}

void BridgeBackend::set_ambient_light(double r, double g, double b, double intensity)
{
    if (!m_runtime_host)
        return;
    auto light = m_runtime_host->scene_light();
    light.ambient_rgb[0] = static_cast<float>(r);
    light.ambient_rgb[1] = static_cast<float>(g);
    light.ambient_rgb[2] = static_cast<float>(b);
    light.ambient_intensity = static_cast<float>(intensity);
    m_runtime_host->set_scene_light(light);
}

void BridgeBackend::set_directional_light(double to_x, double to_y, double to_z, double r, double g, double b, double intensity)
{
    if (!m_runtime_host)
        return;
    auto light = m_runtime_host->scene_light();
    light.light_to_xyz[0] = static_cast<float>(to_x);
    light.light_to_xyz[1] = static_cast<float>(to_y);
    light.light_to_xyz[2] = static_cast<float>(to_z);
    light.light_rgb[0] = static_cast<float>(r);
    light.light_rgb[1] = static_cast<float>(g);
    light.light_rgb[2] = static_cast<float>(b);
    light.light_intensity = static_cast<float>(intensity);
    m_runtime_host->set_scene_light(light);
}

void BridgeBackend::set_point_light(int index, double x, double y, double z, double r, double g, double b, double intensity, double radius)
{
    if (!m_runtime_host || index < 0 || index >= MaxPointLights)
        return;
    auto light = m_runtime_host->scene_light();
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
    m_runtime_host->set_scene_light(light);
}

void BridgeBackend::set_spot_light(int index, double x, double y, double z, double dx, double dy, double dz, double inner_deg, double outer_deg, double r, double g, double b, double intensity, double radius)
{
    if (!m_runtime_host || index < 0 || index >= MaxPointLights)
        return;
    auto light = m_runtime_host->scene_light();
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
    m_runtime_host->set_scene_light(light);
}

void BridgeBackend::clear_point_lights()
{
    if (!m_runtime_host)
        return;
    auto light = m_runtime_host->scene_light();
    light.point_light_count = 0;
    m_runtime_host->set_scene_light(light);
}

Optional<SceneLightData> BridgeBackend::scene_light() const
{
    if (!m_runtime_host)
        return {};
    return m_runtime_host->scene_light();
}

Vector<EntityHierarchyEntry> BridgeBackend::get_entity_hierarchy() const
{
    auto& reg = m_world.registry();
    Vector<EntityHierarchyEntry> entries;

    for (auto entity : reg.storage<entt::entity>()) {
        if (!reg.valid(entity)) continue;
        EntityHierarchyEntry entry;
        entry.id = entity;

        if (auto const* name = reg.try_get<Name>(entity))
            entry.name = name->value;
        else
            entry.name = MUST(String::formatted("entity_{}", static_cast<u32>(entity)));

        if (auto const* parent = reg.try_get<Parent>(entity)) {
            if (reg.valid(parent->id))
                entry.parent_id = parent->id;
        }

        if (reg.try_get<MeshRenderer>(entity)) {
            entry.kind = "mesh"_string;
            if (reg.any_of<AlphaBlend>(entity))     entry.alpha_mode = 2;
            else if (reg.any_of<AlphaClip>(entity)) entry.alpha_mode = 1;
            else if (reg.any_of<AlphaHash>(entity)) entry.alpha_mode = 3;
        } else if (reg.try_get<Panel>(entity)) {
            entry.kind = "panel"_string;
        } else {
            entry.kind = "empty"_string;
        }

        entry.is_static = reg.any_of<Static>(entity);
        entries.append(move(entry));
    }

    // Compute depth by walking up parent chain.
    HashMap<EntityId, int> depth_cache;
    for (auto& entry : entries) {
        int depth = 0;
        EntityId current = entry.parent_id;
        size_t guard = entries.size() + 1;
        while (reg.valid(current) && guard-- > 0) {
            ++depth;
            auto const* p = reg.try_get<Parent>(current);
            if (!p || !reg.valid(p->id)) break;
            current = p->id;
        }
        entry.depth = depth;
    }

    return entries;
}

Optional<EntityComponentSnapshot> BridgeBackend::get_entity_components(EntityId entity) const
{
    auto& reg = m_world.registry();
    if (!reg.valid(entity))
        return {};

    EntityComponentSnapshot snap;
    snap.id = entity;

    if (auto const* name = reg.try_get<Name>(entity))
        snap.name = name->value;
    else
        snap.name = MUST(String::formatted("entity_{}", static_cast<u32>(entity)));

    if (auto const* t = reg.try_get<Transform>(entity)) {
        snap.attached_components.append("Transform"_string);
        snap.position[0] = t->position[0];
        snap.position[1] = t->position[1];
        snap.position[2] = t->position[2];
        snap.rotation[0] = t->rotation[0];
        snap.rotation[1] = t->rotation[1];
        snap.rotation[2] = t->rotation[2];
        snap.rotation[3] = t->rotation[3];
        snap.scale[0] = t->scale[0];
        snap.scale[1] = t->scale[1];
        snap.scale[2] = t->scale[2];
    }

    if (auto const* mr = reg.try_get<MeshRenderer>(entity)) {
        snap.attached_components.append("MeshRenderer"_string);
        snap.has_mesh_renderer = true;
        snap.mesh = mr->mesh;
        snap.material = mr->material;
        snap.normal_map = mr->normal_map;
    }

    if (auto const* panel = reg.try_get<Panel>(entity)) {
        snap.attached_components.append("Panel"_string);
        snap.has_panel = true;
        snap.panel_url = panel->url;
        snap.panel_width = panel->width;
        snap.panel_height = panel->height;
    }

    if (auto const* cull = reg.try_get<CullOverride>(entity)) {
        snap.attached_components.append("CullOverride"_string);
        snap.has_cull_override = true;
        snap.cull_mode = cull->mode;
    }

    snap.is_static   = reg.any_of<Static>(entity);
    snap.alpha_blend = reg.any_of<AlphaBlend>(entity);
    snap.alpha_clip  = reg.any_of<AlphaClip>(entity);
    snap.alpha_hash  = reg.any_of<AlphaHash>(entity);

    if (reg.any_of<Name>(entity))
        snap.attached_components.append("Name"_string);
    if (reg.any_of<Parent>(entity))
        snap.attached_components.append("Parent"_string);
    if (reg.any_of<ScriptComponent>(entity))
        snap.attached_components.append("ScriptComponent"_string);
    if (reg.any_of<ScriptRuntimeHandle>(entity))
        snap.attached_components.append("ScriptRuntimeHandle"_string);
    if (reg.any_of<Selected>(entity))
        snap.attached_components.append("Selected"_string);
    if (snap.is_static)
        snap.attached_components.append("Static"_string);
    if (snap.alpha_blend)
        snap.attached_components.append("AlphaBlend"_string);
    if (snap.alpha_clip)
        snap.attached_components.append("AlphaClip"_string);
    if (snap.alpha_hash)
        snap.attached_components.append("AlphaHash"_string);

    return snap;
}

void BridgeBackend::set_selected_entity(EntityId entity)
{
    auto& reg = m_world.registry();
    // Clear all existing selections.
    auto selected_view = reg.view<Selected>();
    reg.remove<Selected>(selected_view.begin(), selected_view.end());
    // Apply new selection.
    if (reg.valid(entity))
        reg.emplace_or_replace<Selected>(entity);
    if (m_on_selection_changed)
        m_on_selection_changed(entity);
}

}
