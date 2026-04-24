/*
 * MyceliumVR - a social VR platform built on the Ladybird browser engine
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include "LinuxBootstrapChannel.h"
#include "LinuxFrameMailbox.h"
#include "LinuxInputQueue.h"
#include "../WorldProcessTransportClient.h"
#include "../WorldProcessTransportHost.h"

#include <AK/StringView.h>

namespace MyceliumVR::ProcessTransport {

class LinuxTransportFactory {
public:
    struct Options {
        size_t input_queue_capacity { LinuxInputQueue::DefaultCapacity };
        size_t frame_slot_count { LinuxFrameMailbox::DefaultSlotCount };
        size_t frame_slot_capacity_bytes { 64 * 1024 * 1024 };
        size_t static_scene_capacity_bytes { 4 * 1024 * 1024 };
    };

    struct Pair {
        WorldProcessTransportHost host;
        WorldProcessTransportClient client;
    };

    struct BootstrapConfig {
        u32 protocol_version { WorldProcessTransportProtocolVersion };
        u32 input_queue_capacity { LinuxInputQueue::DefaultCapacity };
        u32 frame_slot_count { LinuxFrameMailbox::DefaultSlotCount };
        u64 frame_slot_capacity_bytes { 64 * 1024 * 1024 };
        u64 static_scene_capacity_bytes { 4 * 1024 * 1024 };
    };

    struct ControlBusDiscoveryInfo {
        u16 port { 0 };
        ByteString token;
    };

    struct StaticSceneResizeInfo {
        size_t region_bytes { 0 };
        int region_fd { -1 };
    };

    struct SpawnInfo {
        int bootstrap_fd { -1 };
        int host_to_world_wake_fd { -1 };
        int world_to_host_wake_fd { -1 };
        int input_queue_fd { -1 };
        int frame_mailbox_fd { -1 };
        int static_scene_region_fd { -1 };
    };

    struct SpawnBundle {
        WorldProcessTransportHost host;
        SpawnInfo spawn_info;
        BootstrapConfig bootstrap_config;
    };

    static ErrorOr<Pair> create_pair();
    static ErrorOr<Pair> create_pair(Options const&);
    static ErrorOr<SpawnBundle> create_spawn_bundle();
    static ErrorOr<SpawnBundle> create_spawn_bundle(Options const&);
    static ErrorOr<void> send_bootstrap_config(IWorldBootstrapChannel&, BootstrapConfig const&);
    static ErrorOr<BootstrapConfig> receive_bootstrap_config(LinuxBootstrapChannel&);
    static ErrorOr<void> send_control_bus_discovery(IWorldBootstrapChannel&, ControlBusDiscoveryInfo const&);
    static ErrorOr<ControlBusDiscoveryInfo> receive_control_bus_discovery(LinuxBootstrapChannel&);
    static ErrorOr<void> send_static_scene_resize(IWorldBootstrapChannel&, int region_fd, size_t region_bytes);
    static ErrorOr<Optional<StaticSceneResizeInfo>> receive_static_scene_resize(LinuxBootstrapChannel&);
    static ErrorOr<WorldProcessTransportClient> create_client_transport(NonnullOwnPtr<LinuxBootstrapChannel>, SpawnInfo const&, BootstrapConfig const&);
    static void log_bootstrap_validation(StringView side, SpawnInfo const&, BootstrapConfig const&);
    static ErrorOr<void> run_bootstrap_self_test_host(WorldProcessTransportHost&, SpawnInfo const&, BootstrapConfig const&);
    static ErrorOr<void> run_bootstrap_self_test_client(WorldProcessTransportClient&, SpawnInfo const&, BootstrapConfig const&);
};

}
