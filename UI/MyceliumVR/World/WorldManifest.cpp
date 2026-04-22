/*
 * MyceliumVR - a social VR platform built on the Ladybird browser engine
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "WorldManifest.h"

#include <AK/CharacterTypes.h>
#include <AK/JsonObject.h>
#include <AK/JsonValue.h>
#include <AK/StringBuilder.h>

namespace MyceliumVR {

static Optional<String> get_string_property(JsonObject const& object, StringView property_name)
{
    auto value = object.get(property_name);
    if (!value.has_value() || !value->is_string())
        return {};
    return value->as_string();
}

static ErrorOr<String> sanitize_package_name(StringView package_name)
{
    StringBuilder builder;
    bool previous_was_separator = false;

    for (auto ch : package_name) {
        if (is_ascii_alphanumeric(ch)) {
            builder.append_code_point(to_ascii_lowercase(ch));
            previous_was_separator = false;
            continue;
        }

        if (ch == '.' || ch == '_' || ch == '-') {
            builder.append_code_point(ch);
            previous_was_separator = false;
            continue;
        }

        if (!previous_was_separator) {
            builder.append('-');
            previous_was_separator = true;
        }
    }

    auto sanitized = TRY(builder.to_string());
    if (sanitized.is_empty() || sanitized == "-"sv)
        return Error::from_string_literal("World package name is empty after sanitization");
    return sanitized;
}

static WorldManifest::Permissions parse_permissions(JsonObject const& root)
{
    WorldManifest::Permissions permissions;
    auto perms_value = root.get("permissions"sv);
    if (perms_value.has_value() && perms_value->is_object()) {
        auto const& perms = perms_value->as_object();
        permissions.storage = perms.get_bool("storage"sv).value_or(false);
        permissions.network = perms.get_bool("network"sv).value_or(false);
        permissions.wasm = perms.get_bool("wasm"sv).value_or(false);
    }
    return permissions;
}

static ErrorOr<WorldManifest::Networking> parse_networking(JsonObject const& root)
{
    WorldManifest::Networking networking;
    auto net_value = root.get("networking"sv);
    if (!net_value.has_value() || !net_value->is_object())
        return networking;

    auto const& net = net_value->as_object();
    networking.entry_wasm = get_string_property(net, "entry_wasm"sv).value_or(String {});
    
    auto bootstrap_value = net.get("bootstrap"sv);
    if (bootstrap_value.has_value() && bootstrap_value->is_object()) {
        auto const& bootstrap = bootstrap_value->as_object();
        networking.bootstrap.mode = get_string_property(bootstrap, "mode"sv).value_or("none"_string);
        auto urls_value = bootstrap.get("urls"sv);
        if (urls_value.has_value() && urls_value->is_array()) {
            TRY(urls_value->as_array().try_for_each([&](JsonValue const& url) -> ErrorOr<void> {
                if (url.is_string())
                    TRY(networking.bootstrap.urls.try_append(url.as_string()));
                return {};
            }));
        }
    }

    auto transports_value = net.get("transports"sv);
    if (transports_value.has_value() && transports_value->is_array()) {
        TRY(transports_value->as_array().try_for_each([&](JsonValue const& transport) -> ErrorOr<void> {
            if (transport.is_string())
                TRY(networking.transports.try_append(transport.as_string()));
            return {};
        }));
    }

    networking.public_listen = net.get_bool("public_listen"sv).value_or(false);
    networking.max_peers = net.get_u32("max_peers"sv).value_or(32);
    networking.protocol_version = net.get_u32("protocol_version"sv).value_or(1);

    return networking;
}

ErrorOr<WorldManifest> load_world_manifest(VirtualFileSystem const& file_system, StringView manifest_path)
{
    auto manifest_source = TRY(file_system.read_file(manifest_path));
    auto json = TRY(JsonValue::from_string(manifest_source));
    if (!json.is_object())
        return Error::from_string_literal("World manifest must be a JSON object");

    auto const& root = json.as_object();
    auto entry_value = root.get("entry"sv);
    if (!entry_value.has_value() || !entry_value->is_object())
        return Error::from_string_literal("World manifest must contain an 'entry' object");

    auto const& entry = entry_value->as_object();
    auto script = get_string_property(entry, "script"sv);
    if (!script.has_value() || script->is_empty())
        return Error::from_string_literal("World manifest entry must contain a script path");

    auto name = get_string_property(root, "name"sv).value_or("Untitled World"_string);
    auto package_name = get_string_property(root, "package"sv).value_or(name);
    package_name = TRY(sanitize_package_name(package_name));

    return WorldManifest {
        .package_name = move(package_name),
        .name = move(name),
        .entry_script = script.release_value(),
        .script_tick_budget_ms = root.get_u32("script_tick_budget_ms"sv).value_or(0),
        .wasm_memory_limit_mb = root.get_u32("wasm_memory_limit_mb"sv).value_or(64),
        .permissions = parse_permissions(root),
        .networking = TRY(parse_networking(root)),
    };
}

ErrorOr<String> resolve_world_relative_path(StringView world_root, StringView relative_path)
{
    if (relative_path.contains("://"sv))
        return String::from_utf8(relative_path);
    if (relative_path.starts_with('/'))
        return Error::from_string_literal("World-relative path must not start with /");
    if (!world_root.ends_with('/'))
        return MUST(String::formatted("{}/{}", world_root, relative_path));
    return MUST(String::formatted("{}{}", world_root, relative_path));
}

ErrorOr<String> package_mount_root(StringView package_name)
{
    auto sanitized_package_name = TRY(sanitize_package_name(package_name));
    return MUST(String::formatted("world://{}/", sanitized_package_name));
}

}
