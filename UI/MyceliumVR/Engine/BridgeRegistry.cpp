/*
 * MyceliumVR - a social VR platform built on the Ladybird browser engine
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "BridgeRegistry.h"

#include <AK/NeverDestroyed.h>
#include <AK/String.h>

namespace MyceliumVR {

StringView bridge_value_type_name(BridgeValueType type)
{
    switch (type) {
    case BridgeValueType::Void:
        return "void"sv;
    case BridgeValueType::Boolean:
        return "boolean"sv;
    case BridgeValueType::Number:
        return "number"sv;
    case BridgeValueType::String:
        return "string"sv;
    case BridgeValueType::EntityId:
        return "EntityId"sv;
    case BridgeValueType::Float32Array:
        return "Float32Array"sv;
    }
    VERIFY_NOT_REACHED();
}

BridgeRegistry const& BridgeRegistry::the()
{
    static NeverDestroyed<BridgeRegistry> registry;
    return *registry;
}

BridgeRegistry::BridgeRegistry()
{
    m_functions.append({
        .module = "Debug"sv,
        .js_namespace = ""sv,
        .name = "log"sv,
        .description = "Write a message to the native MyceliumVR log."sv,
        .return_type = BridgeValueType::Void,
        .arguments = { { "message"sv, BridgeValueType::String } },
        .hot_path = false,
        .debug = false,
        .js_exposed = true,
        .wasm_exposed = true,
        .throws = ""sv,
    });

    m_functions.append({
        .module = "World"sv,
        .js_namespace = ""sv,
        .name = "spawnEntity"sv,
        .description = "Create a native ECS entity and return its opaque handle."sv,
        .return_type = BridgeValueType::EntityId,
        .arguments = {},
        .hot_path = false,
        .debug = false,
        .js_exposed = true,
        .wasm_exposed = true,
        .throws = ""sv,
    });

    m_functions.append({
        .module = "World"sv,
        .js_namespace = ""sv,
        .name = "destroyEntity"sv,
        .description = "Destroy a native ECS entity. Returns false for stale or invalid handles."sv,
        .return_type = BridgeValueType::Boolean,
        .arguments = { { "entity"sv, BridgeValueType::EntityId } },
        .hot_path = false,
        .debug = false,
        .js_exposed = true,
        .wasm_exposed = true,
        .throws = ""sv,
    });

    m_functions.append({
        .module = "Debug"sv,
        .js_namespace = ""sv,
        .name = "entityCount"sv,
        .description = "Return the number of live native ECS entities."sv,
        .return_type = BridgeValueType::Number,
        .arguments = {},
        .hot_path = false,
        .debug = true,
        .js_exposed = true,
        .wasm_exposed = true,
        .throws = ""sv,
    });

    m_functions.append({
        .module = "Debug"sv,
        .js_namespace = ""sv,
        .name = "dirtyTransformCount"sv,
        .description = "Return the number of live entities with dirty transforms."sv,
        .return_type = BridgeValueType::Number,
        .arguments = {},
        .hot_path = false,
        .debug = true,
        .js_exposed = true,
        .wasm_exposed = true,
        .throws = ""sv,
    });

    m_functions.append({
        .module = "Transform"sv,
        .js_namespace = ""sv,
        .name = "setTransform"sv,
        .description = "Write a transform directly into native ECS storage."sv,
        .return_type = BridgeValueType::Boolean,
        .arguments = {
            { "entity"sv, BridgeValueType::EntityId },
            { "px"sv, BridgeValueType::Number },
            { "py"sv, BridgeValueType::Number },
            { "pz"sv, BridgeValueType::Number },
            { "qx"sv, BridgeValueType::Number },
            { "qy"sv, BridgeValueType::Number },
            { "qz"sv, BridgeValueType::Number },
            { "qw"sv, BridgeValueType::Number },
            { "sx"sv, BridgeValueType::Number },
            { "sy"sv, BridgeValueType::Number },
            { "sz"sv, BridgeValueType::Number },
        },
        .hot_path = true,
        .debug = false,
        .js_exposed = true,
        .wasm_exposed = true,
        .throws = "RangeError for invalid EntityId."sv,
    });

    m_functions.append({
        .module = "Transform"sv,
        .js_namespace = ""sv,
        .name = "acquireTransformBuffer"sv,
        .description = "Acquire a Float32Array for bulk transform writes."sv,
        .return_type = BridgeValueType::Float32Array,
        .arguments = { { "capacity"sv, BridgeValueType::Number } },
        .hot_path = true,
        .debug = false,
        .js_exposed = true,
        .wasm_exposed = false,
        .throws = "RangeError for invalid capacity."sv,
    });

    m_functions.append({
        .module = "Transform"sv,
        .js_namespace = ""sv,
        .name = "commitTransformBuffer"sv,
        .description = "Apply rows from the acquired transform buffer and return the accepted transform count."sv,
        .return_type = BridgeValueType::Number,
        .arguments = { { "count"sv, BridgeValueType::Number } },
        .hot_path = true,
        .debug = false,
        .js_exposed = true,
        .wasm_exposed = false,
        .throws = "RangeError for invalid count."sv,
    });

    m_functions.append({
        .module = "Render"sv,
        .js_namespace = ""sv,
        .name = "setMesh"sv,
        .description = "Assign a mesh asset handle to an entity."sv,
        .return_type = BridgeValueType::Boolean,
        .arguments = {
            { "entity"sv, BridgeValueType::EntityId },
            { "mesh"sv, BridgeValueType::String },
        },
        .hot_path = false,
        .debug = false,
        .js_exposed = true,
        .wasm_exposed = true,
        .throws = "RangeError for invalid EntityId."sv,
    });

    m_functions.append({
        .module = "Render"sv,
        .js_namespace = ""sv,
        .name = "setMaterial"sv,
        .description = "Assign a material asset handle to an entity."sv,
        .return_type = BridgeValueType::Boolean,
        .arguments = {
            { "entity"sv, BridgeValueType::EntityId },
            { "material"sv, BridgeValueType::String },
        },
        .hot_path = false,
        .debug = false,
        .js_exposed = true,
        .wasm_exposed = true,
        .throws = "RangeError for invalid EntityId."sv,
    });

    m_functions.append({
        .module = "Panel"sv,
        .js_namespace = ""sv,
        .name = "createPanel"sv,
        .description = "Attach a WebContent panel definition to an entity."sv,
        .return_type = BridgeValueType::Boolean,
        .arguments = {
            { "entity"sv, BridgeValueType::EntityId },
            { "url"sv, BridgeValueType::String },
            { "width"sv, BridgeValueType::Number },
            { "height"sv, BridgeValueType::Number },
        },
        .hot_path = false,
        .debug = false,
        .js_exposed = true,
        .wasm_exposed = true,
        .throws = "RangeError for invalid EntityId."sv,
    });

    m_functions.append({
        .module = "FileSystem"sv,
        .js_namespace = "fs"sv,
        .name = "readText"sv,
        .description = "Read a UTF-8 text file from a mounted Mycelium virtual filesystem path."sv,
        .return_type = BridgeValueType::String,
        .arguments = { { "path"sv, BridgeValueType::String } },
        .hot_path = false,
        .debug = false,
        .js_exposed = true,
        .wasm_exposed = false,
        .throws = "Error for missing mounts, missing files, or invalid UTF-8."sv,
    });

    m_functions.append({
        .module = "FileSystem"sv,
        .js_namespace = "fs"sv,
        .name = "exists"sv,
        .description = "Return true if a mounted Mycelium virtual filesystem path exists."sv,
        .return_type = BridgeValueType::Boolean,
        .arguments = { { "path"sv, BridgeValueType::String } },
        .hot_path = false,
        .debug = false,
        .js_exposed = true,
        .wasm_exposed = false,
        .throws = ""sv,
    });

    m_functions.append({
        .module = "FileSystem"sv,
        .js_namespace = "fs"sv,
        .name = "writeText"sv,
        .description = "Write UTF-8 text to a mounted writable Mycelium virtual filesystem path."sv,
        .return_type = BridgeValueType::Boolean,
        .arguments = {
            { "path"sv, BridgeValueType::String },
            { "text"sv, BridgeValueType::String },
        },
        .hot_path = false,
        .debug = false,
        .js_exposed = true,
        .wasm_exposed = false,
        .throws = "TypeError for read-only mounts, missing mounts, or write failures."sv,
    });
}

BridgeFunction const* BridgeRegistry::find_function(StringView name) const
{
    for (auto const& function : m_functions) {
        if (function.name == name)
            return &function;
        if (!function.js_namespace.is_empty()) {
            auto qualified_name = MUST(String::formatted("{}.{}", function.js_namespace, function.name));
            if (qualified_name == name)
                return &function;
        }
    }
    return nullptr;
}

}
