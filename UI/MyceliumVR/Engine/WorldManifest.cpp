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
