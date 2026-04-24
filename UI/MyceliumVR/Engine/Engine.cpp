/*
 * MyceliumVR - a social VR platform built on the Ladybird browser engine
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "Engine.h"
#include "../World/RenderSnapshot.h"
#include "../Rendering/Web/OverlayManager.h"

#include "../Support/InputState.h"
#include "../Support/VirtualFileSystem.h"

namespace MyceliumVR {

Engine::Engine(SDL_Window& window, VirtualFileSystem const* file_system)
    : m_renderer(window, file_system)
{
}

ErrorOr<void> Engine::initialize_world_management_system(VirtualFileSystem* virtual_file_system, InputState* input_state)
{
    m_world_management_system.set_environment(virtual_file_system, input_state);
    m_world_management_system.set_renderer(&m_renderer);
    return m_world_management_system.initialize();
}

void Engine::resize(int width, int height)
{
    m_renderer.resize(width, height);
}

ErrorOr<void> Engine::render()
{
    TRY(m_renderer.sync_snapshot_buffers());

    if (!m_world_management_system.is_in_process_mode()) {
        auto* connection = m_world_management_system.foreground_world_process_connection();
        if (!connection || !connection->has_snapshot()) {
            // When switching subprocess worlds, the new foreground world may not have
            // produced its first snapshot yet. Draw an empty frame so the previous
            // world's last image does not remain stuck on the swapchain.
            TRY(m_renderer.draw_snapshot({}, {}));
            m_renderer.post_render();
            return {};
        }
        TRY(m_renderer.draw_snapshot(connection->latest_snapshot(), connection->latest_static_scene()));
        m_renderer.post_render();
        return {};
    }

    auto* runtime = m_world_management_system.foreground_runtime();
    if (!runtime) {
        TRY(m_renderer.draw_snapshot({}, {}));
        m_renderer.post_render();
        return {};
    }

    static Vector<u8> snapshot_buffer;
    if (snapshot_buffer.size() < MyceliumVR::DefaultSnapshotSlotBytes)
        snapshot_buffer.resize(MyceliumVR::DefaultSnapshotSlotBytes);

    static Vector<u8> static_scene_buffer;
    if (static_scene_buffer.size() < MyceliumVR::DefaultStaticSceneSlotBytes)
        static_scene_buffer.resize(MyceliumVR::DefaultStaticSceneSlotBytes);

    if (runtime->sync_scene_revision_for_static_publish()) {
        runtime->build_static_scene_snapshot(static_scene_buffer.span());
        m_world_management_system.overlay_manager().refresh_scene();
    }

    runtime->build_render_snapshot(snapshot_buffer.span());
    TRY(m_renderer.draw_snapshot(snapshot_buffer.span(), static_scene_buffer.span()));
    m_renderer.post_render();

    runtime->with_world_lock([&](World& world) {
        world.clear_dirty_flags();
    });

    return {};
}

}
