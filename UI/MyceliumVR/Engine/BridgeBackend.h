/*
 * MyceliumVR - a social VR platform built on the Ladybird browser engine
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include "World.h"

#include <AK/Span.h>

namespace MyceliumVR {

class BridgeBackend {
public:
    explicit BridgeBackend(World&);

    void log(String message);

    EntityId spawn_entity();
    bool destroy_entity(EntityId);

    u32 entity_count() const;
    u32 dirty_transform_count() const;

    bool set_transform(EntityId, double px, double py, double pz, double qx, double qy, double qz, double qw, double sx, double sy, double sz);
    u32 commit_transform_buffer(ReadonlySpan<float> buffer, u32 count);
    bool set_mesh(EntityId, String mesh);
    bool set_material(EntityId, String material);
    bool create_panel(EntityId, String url, double width, double height);

    World& world() { return m_world; }
    World const& world() const { return m_world; }

private:
    World& m_world;
};

}
