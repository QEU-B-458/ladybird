/*
 * MyceliumVR - a social VR platform built on the Ladybird browser engine
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include "WorldRuntimeHost.h"

#include <AK/HashMap.h>
#include <AK/JsonValue.h>

namespace MyceliumVR {

class ControlBusServer;

class SubprocessWorldRuntimeHost final : public WorldRuntimeHost {
public:
    SubprocessWorldRuntimeHost();
    virtual ~SubprocessWorldRuntimeHost() override;

    void set_control_bus(ControlBusServer* bus) { m_control_bus = bus; }

    virtual void set_camera_state(CameraState const&) override;
    virtual CameraState camera_state() const override;
    virtual void set_scene_light(SceneLightData const&) override;
    virtual SceneLightData scene_light() const override;
    virtual void set_runtime_state(u32 state) override;

    virtual u32 register_mesh(WorldId world_id, ByteString const& virtual_path) override;
    virtual u32 register_material(WorldId world_id, ByteString const& virtual_path) override;
    virtual u32 register_panel(WorldId world_id, u32 entity_id, ByteString const& url, float width, float height) override;

    struct RegistrationSnapshot {
        HashMap<ByteString, u32> meshes;
        HashMap<ByteString, u32> materials;
        struct PanelEntry {
            u32 handle;
            u32 entity_id;
            ByteString url;
            float width;
            float height;
        };
        Vector<PanelEntry> panels;
    };
    RegistrationSnapshot get_all_registrations() const;

private:
    ControlBusServer* m_control_bus { nullptr };
    CameraState m_camera_state;
    SceneLightData m_scene_light;
    HashMap<ByteString, u32> m_meshes;
    HashMap<ByteString, u32> m_materials;

    struct PanelEntry {
        u32 handle;
        u32 entity_id;
        ByteString url;
        float width;
        float height;
    };
    HashMap<u32, PanelEntry> m_panels; // entity_id -> info
};

}
