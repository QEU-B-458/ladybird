/*
 * MyceliumVR - a social VR platform built on the Ladybird browser engine
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include "VirtualFileSystem.h"

#include <AK/String.h>

namespace MyceliumVR {

struct WorldManifest {
    String package_name;
    String name;
    String entry_script;
};

ErrorOr<WorldManifest> load_world_manifest(VirtualFileSystem const&, StringView manifest_path);
ErrorOr<String> resolve_world_relative_path(StringView world_root, StringView relative_path);
ErrorOr<String> package_mount_root(StringView package_name);

}
