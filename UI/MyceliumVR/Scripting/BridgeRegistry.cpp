/*
 * MyceliumVR - a social VR platform built on the Ladybird browser engine
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "BridgeRegistry.h"
#include "BridgeFunctions.h"

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

BridgeRegistry& BridgeRegistry::the()
{
    static NeverDestroyed<BridgeRegistry> registry;
    return *registry;
}


BridgeRegistry::BridgeRegistry()
{
    // Pre-populate from the static metadata table in BridgeFunctions.
    // This makes the registry valid immediately — before any ScriptRuntime exists —
    // so SDKGenerator and api.* queries work without needing a live JS runtime.
    for (auto const& meta : BridgeFunctions::all_metadata())
        m_functions.append(meta);
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
