/*
 * MyceliumVR - a social VR platform built on the Ladybird browser engine
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <AK/NeverDestroyed.h>
#include <AK/StringView.h>
#include <AK/Vector.h>

namespace MyceliumVR {

enum class BridgeValueType {
    Void,
    Boolean,
    Number,
    String,
    EntityId,
    Float32Array,
};

struct BridgeArgument {
    StringView name;
    BridgeValueType type;
};

struct BridgeFunction {
    StringView module;
    StringView js_namespace;
    StringView name;
    StringView description;
    BridgeValueType return_type { BridgeValueType::Void };
    Vector<BridgeArgument> arguments;
    bool hot_path { false };
    bool debug { false };
    bool js_exposed { true };
    bool wasm_exposed { true };
    StringView throws;
};

class BridgeRegistry {
public:
    static BridgeRegistry const& the();

    Vector<BridgeFunction> const& functions() const { return m_functions; }
    BridgeFunction const* find_function(StringView name) const;

private:
    friend class NeverDestroyed<BridgeRegistry>;

    BridgeRegistry();

    Vector<BridgeFunction> m_functions;
};

StringView bridge_value_type_name(BridgeValueType);

}
