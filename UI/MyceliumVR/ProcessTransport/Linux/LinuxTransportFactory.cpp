/*
 * MyceliumVR - a social VR platform built on the Ladybird browser engine
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "LinuxTransportFactory.h"

#include "LinuxBootstrapChannel.h"
#include "LinuxFrameMailbox.h"
#include "LinuxInputQueue.h"
#include "LinuxSharedMemoryRegion.h"
#include "LinuxSyscalls.h"
#include "LinuxWakeSignal.h"

#include <AK/StringBuilder.h>

namespace MyceliumVR::ProcessTransport {

static size_t valid_handle_count(LinuxTransportFactory::SpawnInfo const& spawn_info)
{
    size_t count = 0;
    if (spawn_info.bootstrap_fd >= 0)
        ++count;
    if (spawn_info.host_to_world_wake_fd >= 0)
        ++count;
    if (spawn_info.world_to_host_wake_fd >= 0)
        ++count;
    if (spawn_info.input_queue_fd >= 0)
        ++count;
    if (spawn_info.frame_mailbox_fd >= 0)
        ++count;
    if (spawn_info.static_scene_region_fd >= 0)
        ++count;
    return count;
}

static ErrorOr<ByteString> build_self_test_payload(StringView side, LinuxTransportFactory::SpawnInfo const& spawn_info, LinuxTransportFactory::BootstrapConfig const& config, size_t stats_snapshot_count)
{
    StringBuilder builder;
    TRY(builder.try_appendff(
        "ok side={} protocol={} handles={} input_capacity={} frame_slots={} frame_slot_bytes={} static_scene_bytes={} stats_snapshots={}",
        side,
        config.protocol_version,
        valid_handle_count(spawn_info),
        config.input_queue_capacity,
        config.frame_slot_count,
        config.frame_slot_capacity_bytes,
        config.static_scene_capacity_bytes,
        stats_snapshot_count));
    return builder.to_byte_string();
}

static ErrorOr<ByteBuffer> serialize_bootstrap_config(LinuxTransportFactory::BootstrapConfig const& config)
{
    auto buffer = TRY(ByteBuffer::create_uninitialized(sizeof(config)));
    __builtin_memcpy(buffer.data(), &config, sizeof(config));
    return buffer;
}

static ErrorOr<LinuxTransportFactory::BootstrapConfig> deserialize_bootstrap_config(ReadonlyBytes bytes)
{
    if (bytes.size() != sizeof(LinuxTransportFactory::BootstrapConfig))
        return Error::from_string_literal("Linux transport bootstrap config has invalid size");

    LinuxTransportFactory::BootstrapConfig config {};
    __builtin_memcpy(&config, bytes.data(), sizeof(config));
    return config;
}

static ErrorOr<ByteBuffer> serialize_control_bus_discovery(LinuxTransportFactory::ControlBusDiscoveryInfo const& info)
{
    auto token_bytes = info.token.bytes();
    if (token_bytes.size() > NumericLimits<u16>::max())
        return Error::from_string_literal("Linux transport control-bus token is too large");

    auto buffer = TRY(ByteBuffer::create_uninitialized(sizeof(u16) + sizeof(u16) + token_bytes.size()));
    auto* data = buffer.data();
    auto token_size = static_cast<u16>(token_bytes.size());
    __builtin_memcpy(data, &info.port, sizeof(u16));
    __builtin_memcpy(data + sizeof(u16), &token_size, sizeof(u16));
    if (token_size > 0)
        __builtin_memcpy(data + sizeof(u16) + sizeof(u16), token_bytes.data(), token_size);
    return buffer;
}

static ErrorOr<LinuxTransportFactory::ControlBusDiscoveryInfo> deserialize_control_bus_discovery(ReadonlyBytes bytes)
{
    if (bytes.size() < sizeof(u16) + sizeof(u16))
        return Error::from_string_literal("Linux transport control-bus discovery payload is too small");

    LinuxTransportFactory::ControlBusDiscoveryInfo info;
    u16 token_size { 0 };
    __builtin_memcpy(&info.port, bytes.data(), sizeof(u16));
    __builtin_memcpy(&token_size, bytes.data() + sizeof(u16), sizeof(u16));
    if (bytes.size() != sizeof(u16) + sizeof(u16) + token_size)
        return Error::from_string_literal("Linux transport control-bus discovery payload has invalid size");
    info.token = ByteString { bytes.slice(sizeof(u16) + sizeof(u16), token_size) };
    return info;
}

static ErrorOr<ByteBuffer> serialize_static_scene_resize(size_t region_bytes)
{
    u64 wire_region_bytes = region_bytes;
    auto buffer = TRY(ByteBuffer::create_uninitialized(sizeof(wire_region_bytes)));
    __builtin_memcpy(buffer.data(), &wire_region_bytes, sizeof(wire_region_bytes));
    return buffer;
}

static ErrorOr<size_t> deserialize_static_scene_resize(ReadonlyBytes bytes)
{
    if (bytes.size() != sizeof(u64))
        return Error::from_string_literal("Linux transport static-scene resize payload has invalid size");
    u64 wire_region_bytes { 0 };
    __builtin_memcpy(&wire_region_bytes, bytes.data(), sizeof(wire_region_bytes));
    return static_cast<size_t>(wire_region_bytes);
}

ErrorOr<LinuxTransportFactory::Pair> LinuxTransportFactory::create_pair()
{
    return create_pair(Options {});
}

ErrorOr<LinuxTransportFactory::Pair> LinuxTransportFactory::create_pair(Options const& options)
{
    int fds[2];
    TRY(LinuxSyscalls::create_local_socket_pair(fds));

    auto host_bootstrap = TRY(LinuxBootstrapChannel::create_from_fd(fds[0]));
    auto client_bootstrap = TRY(LinuxBootstrapChannel::create_from_fd(fds[1]));

    auto host_to_world = TRY(LinuxWakeSignal::create());
    auto world_to_host = TRY(LinuxWakeSignal::create());
    auto client_host_to_world = TRY(LinuxWakeSignal::create_from_fd(TRY(host_to_world->duplicate_fd())));
    auto host_world_to_host = TRY(LinuxWakeSignal::create_from_fd(TRY(world_to_host->duplicate_fd())));

    auto host_input_queue = TRY(LinuxInputQueue::create(options.input_queue_capacity));
    auto client_input_queue = TRY(LinuxInputQueue::create_from_fd(TRY(host_input_queue->duplicate_fd()), options.input_queue_capacity));

    auto host_frame_mailbox = TRY(LinuxFrameMailbox::create(options.frame_slot_capacity_bytes, options.frame_slot_count));
    auto client_frame_mailbox = TRY(LinuxFrameMailbox::create_from_fd(TRY(host_frame_mailbox->duplicate_fd()), options.frame_slot_capacity_bytes, options.frame_slot_count));

    auto host_static_scene = TRY(LinuxSharedMemoryRegion::create(options.static_scene_capacity_bytes));
    auto client_static_scene = TRY(LinuxSharedMemoryRegion::create_from_fd(TRY(host_static_scene->duplicate_fd()), options.static_scene_capacity_bytes));

    WorldProcessTransportHost::Endpoints host_endpoints;
    host_endpoints.bootstrap_channel = move(host_bootstrap);
    host_endpoints.host_to_world_wake = move(host_to_world);
    host_endpoints.world_to_host_wake = move(host_world_to_host);
    host_endpoints.input_queue = move(host_input_queue);
    host_endpoints.frame_mailbox = move(host_frame_mailbox);
    host_endpoints.static_scene_region = move(host_static_scene);

    WorldProcessTransportClient::Endpoints client_endpoints;
    client_endpoints.bootstrap_channel = move(client_bootstrap);
    client_endpoints.host_to_world_wake = move(client_host_to_world);
    client_endpoints.world_to_host_wake = move(world_to_host);
    client_endpoints.input_queue = move(client_input_queue);
    client_endpoints.frame_mailbox = move(client_frame_mailbox);
    client_endpoints.static_scene_region = move(client_static_scene);

    return Pair {
        .host = WorldProcessTransportHost(move(host_endpoints)),
        .client = WorldProcessTransportClient(move(client_endpoints)),
    };
}

ErrorOr<LinuxTransportFactory::SpawnBundle> LinuxTransportFactory::create_spawn_bundle()
{
    return create_spawn_bundle(Options {});
}

ErrorOr<LinuxTransportFactory::SpawnBundle> LinuxTransportFactory::create_spawn_bundle(Options const& options)
{
    int fds[2];
    TRY(LinuxSyscalls::create_local_socket_pair(fds));

    auto host_bootstrap = TRY(LinuxBootstrapChannel::create_from_fd(fds[0]));

    auto host_to_world = TRY(LinuxWakeSignal::create());
    auto world_to_host = TRY(LinuxWakeSignal::create());
    auto host_world_to_host = TRY(LinuxWakeSignal::create_from_fd(TRY(world_to_host->duplicate_fd())));

    auto host_input_queue = TRY(LinuxInputQueue::create(options.input_queue_capacity));
    auto host_frame_mailbox = TRY(LinuxFrameMailbox::create(options.frame_slot_capacity_bytes, options.frame_slot_count));
    auto host_static_scene = TRY(LinuxSharedMemoryRegion::create(options.static_scene_capacity_bytes));

    SpawnInfo spawn_info;
    spawn_info.bootstrap_fd = fds[1];
    spawn_info.host_to_world_wake_fd = TRY(host_to_world->duplicate_fd());
    spawn_info.world_to_host_wake_fd = TRY(world_to_host->release_fd());
    spawn_info.input_queue_fd = TRY(host_input_queue->duplicate_fd());
    spawn_info.frame_mailbox_fd = TRY(host_frame_mailbox->duplicate_fd());
    spawn_info.static_scene_region_fd = TRY(host_static_scene->duplicate_fd());

    WorldProcessTransportHost::Endpoints host_endpoints;
    host_endpoints.bootstrap_channel = move(host_bootstrap);
    host_endpoints.host_to_world_wake = move(host_to_world);
    host_endpoints.world_to_host_wake = move(host_world_to_host);
    host_endpoints.input_queue = move(host_input_queue);
    host_endpoints.frame_mailbox = move(host_frame_mailbox);
    host_endpoints.static_scene_region = move(host_static_scene);

    BootstrapConfig bootstrap_config;
    bootstrap_config.input_queue_capacity = options.input_queue_capacity;
    bootstrap_config.frame_slot_count = options.frame_slot_count;
    bootstrap_config.frame_slot_capacity_bytes = options.frame_slot_capacity_bytes;
    bootstrap_config.static_scene_capacity_bytes = options.static_scene_capacity_bytes;

    return SpawnBundle {
        .host = WorldProcessTransportHost(move(host_endpoints)),
        .spawn_info = spawn_info,
        .bootstrap_config = bootstrap_config,
    };
}

ErrorOr<void> LinuxTransportFactory::send_bootstrap_config(IWorldBootstrapChannel& channel, BootstrapConfig const& config)
{
    return channel.send(BootstrapMessage {
        .kind = BootstrapMessageKind::ResourcesReady,
        .payload = TRY(serialize_bootstrap_config(config)),
    });
}

ErrorOr<LinuxTransportFactory::BootstrapConfig> LinuxTransportFactory::receive_bootstrap_config(LinuxBootstrapChannel& channel)
{
    auto message = TRY(channel.receive_blocking());
    if (message.kind != BootstrapMessageKind::ResourcesReady)
        return Error::from_string_literal("Expected Linux transport resources bootstrap message");
    return deserialize_bootstrap_config(message.payload.bytes());
}

ErrorOr<void> LinuxTransportFactory::send_control_bus_discovery(IWorldBootstrapChannel& channel, ControlBusDiscoveryInfo const& info)
{
    return channel.send(BootstrapMessage {
        .kind = BootstrapMessageKind::ControlBusDiscovery,
        .payload = TRY(serialize_control_bus_discovery(info)),
    });
}

ErrorOr<LinuxTransportFactory::ControlBusDiscoveryInfo> LinuxTransportFactory::receive_control_bus_discovery(LinuxBootstrapChannel& channel)
{
    auto message = TRY(channel.receive_blocking());
    if (message.kind != BootstrapMessageKind::ControlBusDiscovery)
        return Error::from_string_literal("Expected Linux transport control-bus discovery bootstrap message");
    return deserialize_control_bus_discovery(message.payload.bytes());
}

ErrorOr<void> LinuxTransportFactory::send_static_scene_resize(IWorldBootstrapChannel& channel, int region_fd, size_t region_bytes)
{
    auto* linux_channel = dynamic_cast<LinuxBootstrapChannel*>(&channel);
    if (!linux_channel)
        return Error::from_string_literal("Linux transport static-scene resize requires a Linux bootstrap channel");

    Vector<int, 1> fds;
    TRY(fds.try_append(region_fd));
    return linux_channel->send_with_fds(BootstrapMessage {
        .kind = BootstrapMessageKind::StaticSceneRegionResized,
        .payload = TRY(serialize_static_scene_resize(region_bytes)),
    }, fds);
}

ErrorOr<Optional<LinuxTransportFactory::StaticSceneResizeInfo>> LinuxTransportFactory::receive_static_scene_resize(LinuxBootstrapChannel& channel)
{
    auto message = TRY(channel.receive_with_fds());
    if (!message.has_value())
        return Optional<StaticSceneResizeInfo> {};
    if (message->message.kind != BootstrapMessageKind::StaticSceneRegionResized)
        return Error::from_string_literal("Expected Linux transport static-scene resize bootstrap message");
    if (message->fds.size() != 1)
        return Error::from_string_literal("Linux transport static-scene resize bootstrap message requires exactly one file descriptor");

    StaticSceneResizeInfo info;
    info.region_bytes = TRY(deserialize_static_scene_resize(message->message.payload.bytes()));
    info.region_fd = message->fds.take_first();
    return info;
}

ErrorOr<WorldProcessTransportClient> LinuxTransportFactory::create_client_transport(NonnullOwnPtr<LinuxBootstrapChannel> bootstrap_channel, SpawnInfo const& spawn_info, BootstrapConfig const& config)
{
    WorldProcessTransportClient::Endpoints endpoints;
    endpoints.bootstrap_channel = move(bootstrap_channel);
    endpoints.host_to_world_wake = TRY(LinuxWakeSignal::create_from_fd(spawn_info.host_to_world_wake_fd));
    endpoints.world_to_host_wake = TRY(LinuxWakeSignal::create_from_fd(spawn_info.world_to_host_wake_fd));
    endpoints.input_queue = TRY(LinuxInputQueue::create_from_fd(spawn_info.input_queue_fd, config.input_queue_capacity));
    endpoints.frame_mailbox = TRY(LinuxFrameMailbox::create_from_fd(spawn_info.frame_mailbox_fd, config.frame_slot_capacity_bytes, config.frame_slot_count));
    endpoints.static_scene_region = TRY(LinuxSharedMemoryRegion::create_from_fd(spawn_info.static_scene_region_fd, config.static_scene_capacity_bytes));
    return WorldProcessTransportClient(move(endpoints));
}

void LinuxTransportFactory::log_bootstrap_validation(StringView side, SpawnInfo const& spawn_info, BootstrapConfig const& config)
{
    outln(
        "LinuxTransportFactory[{}]: bootstrap validation protocol={} handles={}/6 input_capacity={} frame_slots={} frame_slot_bytes={} static_scene_bytes={}",
        side,
        config.protocol_version,
        valid_handle_count(spawn_info),
        config.input_queue_capacity,
        config.frame_slot_count,
        config.frame_slot_capacity_bytes,
        config.static_scene_capacity_bytes);
}

ErrorOr<void> LinuxTransportFactory::run_bootstrap_self_test_host(WorldProcessTransportHost& host, SpawnInfo const& spawn_info, BootstrapConfig const& config)
{
    if (!host.is_configured())
        return Error::from_string_literal("Linux transport host self-test requires a configured host transport");

    auto snapshots = host.stats_snapshots();
    outln("LinuxTransportFactory[host]: bootstrap self-test waiting for child ack (stats_snapshots={})", snapshots.size());

    auto* bootstrap_channel = dynamic_cast<LinuxBootstrapChannel*>(host.bootstrap_channel());
    if (!bootstrap_channel)
        return Error::from_string_literal("Linux transport host self-test requires a Linux bootstrap channel");

    auto message = TRY(bootstrap_channel->receive_blocking());
    if (message.kind != BootstrapMessageKind::Hello)
        return Error::from_string_literal("Linux transport host self-test expected Hello ack from child");

    auto payload = TRY(build_self_test_payload("host"sv, spawn_info, config, snapshots.size()));
    outln("LinuxTransportFactory[host]: bootstrap self-test local {}", payload);
    outln("LinuxTransportFactory[host]: bootstrap self-test child {}", StringView(message.payload));
    return {};
}

ErrorOr<void> LinuxTransportFactory::run_bootstrap_self_test_client(WorldProcessTransportClient& client, SpawnInfo const& spawn_info, BootstrapConfig const& config)
{
    if (!client.is_configured())
        return Error::from_string_literal("Linux transport client self-test requires a configured client transport");
    if (config.protocol_version != WorldProcessTransportProtocolVersion)
        return Error::from_string_literal("Linux transport client self-test found protocol version mismatch");
    if (valid_handle_count(spawn_info) != 6)
        return Error::from_string_literal("Linux transport client self-test expected 6 valid transport handles");
    if (client.input_queue()->capacity() != config.input_queue_capacity)
        return Error::from_string_literal("Linux transport client self-test found input queue capacity mismatch");
    if (client.frame_mailbox()->slot_count() != config.frame_slot_count)
        return Error::from_string_literal("Linux transport client self-test found frame mailbox slot count mismatch");
    if (client.frame_mailbox()->slot_capacity_bytes() != config.frame_slot_capacity_bytes)
        return Error::from_string_literal("Linux transport client self-test found frame mailbox slot size mismatch");
    if (client.static_scene_region()->size() != config.static_scene_capacity_bytes)
        return Error::from_string_literal("Linux transport client self-test found static scene region size mismatch");

    auto snapshots = client.stats_snapshots();
    auto payload = TRY(build_self_test_payload("client"sv, spawn_info, config, snapshots.size()));
    outln("LinuxTransportFactory[client]: bootstrap self-test {}", payload);
    TRY(client.bootstrap_channel()->send(BootstrapMessage {
        .kind = BootstrapMessageKind::Hello,
        .payload = TRY(ByteBuffer::copy(payload.bytes())),
    }));
    return {};
}

}
