/*
 * MyceliumVR - a social VR platform built on the Ladybird browser engine
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "BridgeBackend.h"

namespace MyceliumVR {

BridgeBackend::BridgeBackend(World& world)
    : m_world(world)
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
        auto entity = static_cast<EntityId>(row[0]);
        if (!m_world.entity(entity))
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

bool BridgeBackend::create_panel(EntityId entity, String url, double width, double height)
{
    return m_world.create_panel(entity, move(url), static_cast<float>(width), static_cast<float>(height));
}

}
