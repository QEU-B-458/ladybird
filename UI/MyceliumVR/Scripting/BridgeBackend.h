/*
 * MyceliumVR - a social VR platform built on the Ladybird browser engine
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include "../World/World.h"
#include "../World/WorldRuntimeHost.h"

#include <AK/Function.h>
#include <AK/Optional.h>
#include <AK/Span.h>

namespace MyceliumVR {

struct EntityHierarchyEntry {
    EntityId id { entt::null };
    EntityId parent_id { entt::null };
    String name;
    String kind;     // "mesh" | "panel" | "empty"
    int alpha_mode { 0 }; // 0=opaque, 1=clip, 2=blend, 3=hash
    bool is_static { false };
    int depth { 0 };
};

struct EntityComponentSnapshot {
    EntityId id { entt::null };
    String name;
    Vector<String> attached_components;

    float position[3] { 0.0f, 0.0f, 0.0f };
    float rotation[4] { 0.0f, 0.0f, 0.0f, 1.0f }; // XYZW quaternion
    float scale[3] { 1.0f, 1.0f, 1.0f };

    bool has_mesh_renderer { false };
    String mesh;
    String material;
    String normal_map;

    bool has_panel { false };
    String panel_url;
    float panel_width { 1.0f };
    float panel_height { 1.0f };

    bool has_cull_override { false };
    CullOverride::Mode cull_mode { CullOverride::Mode::Back };

    bool is_static { false };
    bool alpha_blend { false };
    bool alpha_clip { false };
    bool alpha_hash { false };
};

class BridgeBackend {
public:
    explicit BridgeBackend(World&, WorldRuntimeHost* = nullptr);

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
    Vector<EntityHierarchyEntry> get_entity_hierarchy() const;
    Optional<EntityComponentSnapshot> get_entity_components(EntityId) const;
    void set_selected_entity(EntityId);
    void set_camera(double px, double py, double pz, double yaw_degrees, double pitch_degrees);
    Optional<CameraState> camera_state() const;
    void set_ambient_light(double r, double g, double b, double intensity);
    void set_directional_light(double to_x, double to_y, double to_z, double r, double g, double b, double intensity);
    void set_point_light(int index, double x, double y, double z, double r, double g, double b, double intensity, double radius);
    void set_spot_light(int index, double x, double y, double z, double dx, double dy, double dz, double inner_deg, double outer_deg, double r, double g, double b, double intensity, double radius);
    void clear_point_lights();
    Optional<SceneLightData> scene_light() const;

    void set_selection_changed_callback(Function<void(EntityId)> cb) { m_on_selection_changed = move(cb); }
    void set_log_callback(Function<void(StringView, StringView, StringView)> cb) { m_log_callback = move(cb); }

    World& world() { return m_world; }
    World const& world() const { return m_world; }

private:
    World& m_world;
    WorldRuntimeHost* m_runtime_host { nullptr };
    Function<void(EntityId)> m_on_selection_changed;
    Function<void(StringView, StringView, StringView)> m_log_callback;
};

}
