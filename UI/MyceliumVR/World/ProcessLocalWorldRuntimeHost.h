/*
 * MyceliumVR - a social VR platform built on the Ladybird browser engine
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include "WorldRuntimeHost.h"
#include <AK/HashMap.h>

namespace MyceliumVR {

class Renderer;

class ProcessLocalWorldRuntimeHost final : public WorldRuntimeHost {
public:
    explicit ProcessLocalWorldRuntimeHost(Renderer&);

    virtual void set_camera_state(CameraState const&) override;
    virtual CameraState camera_state() const override;
    virtual void set_scene_light(SceneLightData const&) override;
    virtual SceneLightData scene_light() const override;
    virtual void set_runtime_state(u32 state) override;

    virtual u32 register_mesh(WorldId world_id, ByteString const& virtual_path) override;
    virtual u32 register_material(WorldId world_id, ByteString const& virtual_path) override;
    virtual u32 register_panel(WorldId world_id, u32 entity_id, ByteString const& url, float width, float height) override;

private:
    Renderer& m_renderer;

    HashMap<ByteString, u32> m_meshes;
    HashMap<ByteString, u32> m_materials;
    struct PanelEntry {
        u32 entity_id;
        ByteString url;
        float width;
        float height;
    };
    HashMap<u64, u32> m_panels; // (world_id << 32) | entity_id -> panel_handle
};

}
