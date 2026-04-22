/*
 * MyceliumVR - a social VR platform built on the Ladybird browser engine
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <AK/ByteBuffer.h>
#include <AK/Error.h>
#include <AK/HashMap.h>
#include <AK/NonnullOwnPtr.h>
#include <AK/OwnPtr.h>
#include <AK/RefPtr.h>
#include <AK/String.h>
#include <AK/Vector.h>
#include <LibWasm/AbstractMachine/AbstractMachine.h>

namespace MyceliumVR {

class ScriptRuntime;

class WasmRuntime {
public:
    explicit WasmRuntime(ScriptRuntime&);
    ~WasmRuntime();

    ErrorOr<void> initialize();
    ErrorOr<void> load_module(ByteBuffer source);
    
    void update(double delta_time);
    bool is_faulted() const { return m_faulted; }
    String const& fault_reason() const { return m_fault_reason; }

private:
    void fault(String message);
    void bind_imports();

    ScriptRuntime& m_script_runtime;
    Wasm::AbstractMachine m_machine;
    OwnPtr<Wasm::ModuleInstance> m_instance;
    RefPtr<Wasm::Module> m_module;
    bool m_faulted { false };
    String m_fault_reason;
};

}
