/*
 * MyceliumVR - a social VR platform built on the Ladybird browser engine
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include "../Support/VirtualFileSystem.h"

#include <AK/String.h>
#include <AK/Vector.h>

namespace MyceliumVR {

struct WorldManifest {
    struct Permissions {
        bool storage { false };
        bool network { false };
        bool wasm { false };
    };

    struct Networking {
        String entry_wasm;
        struct Bootstrap {
            String mode;
            Vector<String> urls;
        } bootstrap;
        Vector<String> transports;
        bool public_listen { false };
        u32 max_peers { 32 };
        u32 protocol_version { 1 };
    };

    String package_name;
    String name;
    String entry_script;
    u32 script_tick_budget_ms { 0 };
    u32 wasm_memory_limit_mb { 64 };
    Permissions permissions;
    Networking networking;
};

struct BootstrapInfo {
    u32 world_id { 0 };
    String package_name;
    String world_name;
    String mode;
    Vector<String> bootstrap_urls;
    Vector<String> transports;
    u32 protocol_version { 1 };
    bool storage_allowed { false };
    bool network_allowed { false };
    bool wasm_allowed { false };
};

ErrorOr<WorldManifest> load_world_manifest(VirtualFileSystem const&, StringView manifest_path);
ErrorOr<String> resolve_world_relative_path(StringView world_root, StringView relative_path);
ErrorOr<String> package_mount_root(StringView package_name);

}
