/*
 * MyceliumVR - a social VR platform built on the Ladybird browser engine
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include "IFrameMailbox.h"
#include "IInputQueue.h"
#include "ISharedMemoryRegion.h"
#include "ITransportStatsProvider.h"
#include "IWakeSignal.h"
#include "IWorldBootstrapChannel.h"

#include <AK/OwnPtr.h>
#include <AK/Vector.h>

namespace MyceliumVR::ProcessTransport {

class WorldProcessTransportHost {
public:
    struct Endpoints {
        OwnPtr<IWorldBootstrapChannel> bootstrap_channel;
        OwnPtr<IWakeSignal> host_to_world_wake;
        OwnPtr<IWakeSignal> world_to_host_wake;
        OwnPtr<IInputQueue> input_queue;
        OwnPtr<IFrameMailbox> frame_mailbox;
        OwnPtr<ISharedMemoryRegion> static_scene_region;
    };

    explicit WorldProcessTransportHost(Endpoints endpoints)
        : m_endpoints(move(endpoints))
    {
    }

    bool is_configured() const
    {
        return m_endpoints.bootstrap_channel
            && m_endpoints.host_to_world_wake
            && m_endpoints.world_to_host_wake
            && m_endpoints.input_queue
            && m_endpoints.frame_mailbox
            && m_endpoints.static_scene_region;
    }

    IWorldBootstrapChannel* bootstrap_channel() { return m_endpoints.bootstrap_channel.ptr(); }
    IWakeSignal* host_to_world_wake() { return m_endpoints.host_to_world_wake.ptr(); }
    IWakeSignal* world_to_host_wake() { return m_endpoints.world_to_host_wake.ptr(); }
    IInputQueue* input_queue() { return m_endpoints.input_queue.ptr(); }
    IFrameMailbox* frame_mailbox() { return m_endpoints.frame_mailbox.ptr(); }
    ISharedMemoryRegion* static_scene_region() { return m_endpoints.static_scene_region.ptr(); }

    IWorldBootstrapChannel const* bootstrap_channel() const { return m_endpoints.bootstrap_channel.ptr(); }
    IWakeSignal const* host_to_world_wake() const { return m_endpoints.host_to_world_wake.ptr(); }
    IWakeSignal const* world_to_host_wake() const { return m_endpoints.world_to_host_wake.ptr(); }
    IInputQueue const* input_queue() const { return m_endpoints.input_queue.ptr(); }
    IFrameMailbox const* frame_mailbox() const { return m_endpoints.frame_mailbox.ptr(); }
    ISharedMemoryRegion const* static_scene_region() const { return m_endpoints.static_scene_region.ptr(); }
    void replace_static_scene_region(OwnPtr<ISharedMemoryRegion> region) { m_endpoints.static_scene_region = move(region); }

    Vector<TransportStatsSnapshot> stats_snapshots() const
    {
        Vector<TransportStatsSnapshot> snapshots;
        auto append = [&](auto const* endpoint) {
            if (!endpoint)
                return;
            if (auto const* provider = dynamic_cast<ITransportStatsProvider const*>(endpoint))
                snapshots.append(provider->snapshot_stats());
        };
        append(m_endpoints.bootstrap_channel.ptr());
        append(m_endpoints.host_to_world_wake.ptr());
        append(m_endpoints.world_to_host_wake.ptr());
        append(m_endpoints.input_queue.ptr());
        append(m_endpoints.frame_mailbox.ptr());
        append(m_endpoints.static_scene_region.ptr());
        return snapshots;
    }

private:
    Endpoints m_endpoints;
};

}
