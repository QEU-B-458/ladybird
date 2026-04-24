/*
 * MyceliumVR - a social VR platform built on the Ladybird browser engine
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <AK/Types.h>

namespace MyceliumVR {

// SnapshotCamera is shared by both legacy and new formats for now.
struct SnapshotCamera {
    u32   entity_id;
    float position[3];
    float yaw_degrees;
    float pitch_degrees;
    float fov_degrees;
    float near_plane;
    float far_plane;
    // 0 = render to main viewport.
    // nonzero = render to this panel handle (portal, security cam, VR eye, etc.)
    u32   render_target_panel_handle;
};

struct SnapshotInstance {
    float transform[16];  // column-major mat4
};

struct SnapshotPanelEntry {
    u32   panel_handle;   // registered via register_panel()
    float transform[16];
};

struct SnapshotLightEntry {
    float position[3];
    float radius;
    float color[3];
    float intensity;
};

// Optimized dynamic frame packet. Sequential layout:
//
//   FrameHeader
//   SnapshotCamera[camera_count]
//   FrameDrawGroup[draw_group_count]
//   SnapshotInstance[instance_count]
//   SnapshotPanelEntry[panel_count]
//   SnapshotLightEntry[point_light_count]

struct FrameHeader {
    u32 frame_id;
    u32 scene_revision;
    u32 camera_count;
    u32 primary_camera_idx;
    u32 draw_group_count;
    u32 instance_count;
    u32 panel_count;
    u32 point_light_count;
    u32 slot_bytes;

    float ambient_rgb[3];
    float ambient_intensity;
    float sun_direction[3];
    float sun_intensity;
    float sun_rgb[3];
};

struct FrameDrawGroup {
    u32 draw_group_id;
    u32 instance_count;
    u32 instance_offset;
};

// Static scene packet. Written only when scene topology changes.
//
//   StaticSceneHeader
//   StaticDrawGroup[draw_group_count]
//   StaticPanelEntry[panel_count]

struct StaticSceneHeader {
    u32 scene_revision;
    u32 draw_group_count;
    u32 panel_count;
    u32 slot_bytes;
};

struct StaticDrawGroup {
    u32 draw_group_id;
    u32 mesh_handle;
    u32 material_handle;
    u8 alpha_mode;
    u8 cull_mode;
    u8 has_normal_map;
    u8 _pad;
    float aabb_local_min[3];
    float aabb_local_max[3];
};

struct StaticPanelEntry {
    u32 panel_handle;
    float width;
    float height;
};

// Helpers — used by world process when building the snapshot.
inline size_t static_scene_slot_bytes(u32 draw_groups, u32 panels)
{
    return sizeof(StaticSceneHeader)
        + sizeof(StaticDrawGroup) * draw_groups
        + sizeof(StaticPanelEntry) * panels;
}

inline size_t frame_slot_bytes(u32 cameras, u32 draw_groups,
                               u32 instances, u32 panels, u32 lights)
{
    return sizeof(FrameHeader)
        + sizeof(SnapshotCamera)    * cameras
        + sizeof(FrameDrawGroup)    * draw_groups
        + sizeof(SnapshotInstance)  * instances
        + sizeof(SnapshotPanelEntry)* panels
        + sizeof(SnapshotLightEntry)* lights;
}

// Starting capacity negotiated at world boot. Grows via snapshot_buffer_resize().
// 64MB handles ~500k instances comfortably; adjust per world manifest hint.
static constexpr size_t DefaultSnapshotSlotBytes = 64 * 1024 * 1024;
static constexpr size_t DefaultStaticSceneSlotBytes = 4 * 1024 * 1024;

} // namespace MyceliumVR
