/*
 * MyceliumVR - a social VR platform built on the Ladybird browser engine
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "WasmRuntime.h"
#include "ScriptRuntime.h"
#include "../Networking/NetworkService.h"

#include <AK/MemoryStream.h>
#include <LibWasm/AbstractMachine/AbstractMachine.h>
#include <SDL3/SDL.h>

namespace MyceliumVR {

WasmRuntime::WasmRuntime(ScriptRuntime& script_runtime)
    : m_script_runtime(script_runtime)
{
}

WasmRuntime::~WasmRuntime() = default;

ErrorOr<void> WasmRuntime::initialize()
{
    return {};
}

ErrorOr<void> WasmRuntime::load_module(ByteBuffer source)
{
    FixedMemoryStream stream(source.bytes());
    auto module_result = Wasm::Module::parse(stream);
    if (module_result.is_error())
        return Error::from_string_literal("WasmRuntime: failed to parse module");
    m_module = module_result.release_value();
    
    return {};
}

void WasmRuntime::update(double)
{
}

void WasmRuntime::bind_imports()
{
}

}
