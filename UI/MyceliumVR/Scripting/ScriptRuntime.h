/*
 * MyceliumVR - a social VR platform built on the Ladybird browser engine
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include "BridgeBackend.h"
#include "../Support/InputState.h"
#include "../Support/VirtualFileSystem.h"
#include "../World/World.h"

#include <AK/ByteString.h>
#include <AK/Function.h>
#include <AK/OwnPtr.h>
#include <AK/RefPtr.h>
#include <AK/Vector.h>
#include <LibGC/Root.h>
#include <LibJS/Runtime/Completion.h>
#include <LibJS/Forward.h>
#include <LibJS/Runtime/ExecutionContext.h>

namespace MyceliumVR {

class ScriptRuntime {
public:
    explicit ScriptRuntime(World&, VirtualFileSystem* = nullptr, InputState* = nullptr,
        Function<void(VulkanRenderer::CameraState const&)> = {},
        Function<VulkanRenderer::CameraState()> = {},
        Function<void(VulkanRenderer::SceneLightData const&)> = {},
        Function<VulkanRenderer::SceneLightData()> = {});
    ~ScriptRuntime();

    ErrorOr<void> initialize();
    ErrorOr<void> load_script(ByteString const& path);
    ErrorOr<void> load_control_script(ByteString const& path);
    ErrorOr<void> load_script_source(ByteBuffer source, StringView filename);
    void update(double delta_time);

    World& world() { return m_world; }
    BridgeBackend& bridge_backend() { return m_bridge_backend; }
    VirtualFileSystem* virtual_file_system() { return m_virtual_file_system; }
    InputState* input_state() { return m_input_state; }
    JS::ThrowCompletionOr<JS::Value> acquire_transform_buffer(JS::Realm&, u32 capacity);
    JS::ThrowCompletionOr<JS::Value> commit_transform_buffer(JS::VM&, u32 count);
    JS::ThrowCompletionOr<JS::Value> read_text(JS::VM&, StringView path);
    JS::ThrowCompletionOr<JS::Value> write_text(JS::VM&, StringView path, StringView text);
    bool file_exists(StringView path) const;

private:
    struct ScriptEntrypoints {
        Utf16FlyString start_name;
        Utf16FlyString update_name;
        bool reported_missing_update { false };
    };

    void report_exception(JS::Completion const&);
    ErrorOr<void> load_script_source(ByteBuffer source, StringView filename, ScriptEntrypoints entrypoints);
    JS::ThrowCompletionOr<JS::Value> call_if_function(ScriptEntrypoints&, Utf16FlyString const& name, JS::Value argument = JS::js_undefined());

    World& m_world;
    BridgeBackend m_bridge_backend;
    VirtualFileSystem* m_virtual_file_system { nullptr };
    InputState* m_input_state { nullptr };
    RefPtr<JS::VM> m_vm;
    GC::Root<JS::Realm> m_realm;
    OwnPtr<JS::ExecutionContext> m_global_execution_context;
    Vector<ScriptEntrypoints> m_loaded_scripts;
    GC::Root<JS::Float32Array> m_transform_buffer;
    u32 m_transform_buffer_capacity { 0 };
};

}
