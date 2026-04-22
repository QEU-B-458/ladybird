/*
 * MyceliumVR - a social VR platform built on the Ladybird browser engine
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "ScriptRuntime.h"

#include "BridgeFunctions.h"

#include "../World/WorldRuntime.h"

#include <UI/MyceliumVR/Support/Profiling.h>

#include <AK/Format.h>
#include <AK/JsonArray.h>
#include <AK/JsonObject.h>
#include <LibCore/File.h>
#include <LibJS/Bytecode/Interpreter.h>
#include <LibJS/Runtime/AbstractOperations.h>
#include <LibJS/Runtime/GlobalObject.h>
#include <LibJS/Runtime/Object.h>
#include <LibJS/Runtime/Realm.h>
#include <LibJS/Runtime/TypedArray.h>
#include <LibJS/Runtime/ValueInlines.h>
#include <LibJS/Script.h>

namespace MyceliumVR {

// All bridge function definitions — metadata and impls — live in BridgeFunctions.cpp.

class MyceliumGlobalObject final : public JS::GlobalObject {
    JS_OBJECT(MyceliumGlobalObject, JS::GlobalObject);
    GC_DECLARE_ALLOCATOR(MyceliumGlobalObject);

public:
    MyceliumGlobalObject(JS::Realm& realm, ScriptRuntime& runtime)
        : JS::GlobalObject(realm)
        , m_runtime(runtime)
    {
    }

    virtual void initialize(JS::Realm& realm) override
    {
        Base::initialize(realm);

        define_direct_property("global"_utf16_fly_string, this, JS::default_attributes);

        auto mycelium = JS::Object::create(realm, realm.intrinsics().object_prototype());
        auto api      = JS::Object::create(realm, realm.intrinsics().object_prototype());
        auto fs       = JS::Object::create(realm, realm.intrinsics().object_prototype());
        auto input    = JS::Object::create(realm, realm.intrinsics().object_prototype());
        auto net      = JS::Object::create(realm, realm.intrinsics().object_prototype());

        // Populate registry metadata and bind all impls in one call.
        // To add a function, edit BridgeFunctions.cpp only.
        BridgeFunctions::bind_all(m_runtime, realm, *mycelium, *api, *fs, *input, *net);

        mycelium->define_direct_property("api"_utf16_fly_string,   api,   JS::default_attributes);
        mycelium->define_direct_property("fs"_utf16_fly_string,    fs,    JS::default_attributes);
        mycelium->define_direct_property("input"_utf16_fly_string, input, JS::default_attributes);
        mycelium->define_direct_property("net"_utf16_fly_string,   net,   JS::default_attributes);
        define_direct_property("mycelium"_utf16_fly_string, mycelium, JS::default_attributes);
    }

private:
    ScriptRuntime& m_runtime;
};

GC_DEFINE_ALLOCATOR(MyceliumGlobalObject);

ScriptRuntime::ScriptRuntime(ScriptHost& script_host, World& world, NetworkService& network_service, VirtualFileSystem* virtual_file_system, InputState* input_state, WorldRuntimeHost* runtime_host, WorldRuntime* world_runtime)
    : m_script_host(script_host)
    , m_world(world)
    , m_network_service(network_service)
    , m_bridge_backend(world, runtime_host)
    , m_virtual_file_system(virtual_file_system)
    , m_input_state_source(input_state)
    , m_world_runtime(world_runtime)
{
    if (m_input_state_source)
        m_input_state_snapshot = m_input_state_source->snapshot();
}

ScriptRuntime::~ScriptRuntime() = default;

ErrorOr<void> ScriptRuntime::initialize()
{
    auto& vm = m_script_host.vm();

    GC::Ptr<JS::Realm> realm;
    auto context_or_error = JS::Realm::initialize_host_defined_realm(
        vm,
        [&](JS::Realm& new_realm) -> JS::Object* {
            realm = &new_realm;
            return new_realm.create<MyceliumGlobalObject>(new_realm, *this);
        },
        nullptr);
    if (context_or_error.is_error()) {
        report_exception(context_or_error.release_error());
        return Error::from_string_literal("Unable to initialize MyceliumVR script realm");
    }

    m_global_execution_context = context_or_error.release_value();
    vm.pop_execution_context();
    m_realm = GC::Root<JS::Realm>(*realm);
    return {};
}

ErrorOr<void> ScriptRuntime::load_script(ByteString const& path)
{
    auto file = Core::File::open(path, Core::File::OpenMode::Read);
    if (file.is_error()) {
        warnln("MyceliumVR: script '{}' not found, using built-in demo script", path);
        auto source = TRY(ByteBuffer::copy(
            "let entity;\n"
            "let time = 0;\n"
            "function start() {\n"
            "  mycelium.log('built-in demo script started');\n"
            "  entity = mycelium.spawnEntity();\n"
            "  mycelium.setMesh(entity, 'cube');\n"
            "  mycelium.setMaterial(entity, 'accent');\n"
            "}\n"
            "function update(deltaTime) {\n"
            "  time += deltaTime;\n"
            "  mycelium.setTransform(entity, Math.sin(time) * 1.5, Math.cos(time * 0.7) * 0.7, 0, 0, 0, 0, 1, 1, 1, 1);\n"
            "}\n"sv.bytes()));
        TRY(load_script_source(move(source), path, { "start"_utf16_fly_string, "update"_utf16_fly_string }));
        return {};
    }

    TRY(load_script_source(TRY(file.value()->read_until_eof()), path, { "start"_utf16_fly_string, "update"_utf16_fly_string }));
    return {};
}

ErrorOr<void> ScriptRuntime::load_control_script(ByteString const& path)
{
    auto file = Core::File::open(path, Core::File::OpenMode::Read);
    if (file.is_error()) {
        warnln("MyceliumVR: control script '{}' not found, skipping", path);
        return {};
    }

    TRY(load_script_source(TRY(file.value()->read_until_eof()), path, { "controlStart"_utf16_fly_string, "controlUpdate"_utf16_fly_string }));
    return {};
}

ErrorOr<void> ScriptRuntime::load_script_source(ByteBuffer source, StringView filename)
{
    return load_script_source(move(source), filename, { "start"_utf16_fly_string, "update"_utf16_fly_string });
}

ErrorOr<void> ScriptRuntime::load_script_source(ByteBuffer source, StringView filename, ScriptEntrypoints entrypoints)
{
    VERIFY(m_realm);
    auto& vm = m_script_host.vm();

    auto script_or_errors = JS::Script::parse(source, *m_realm, filename);
    if (script_or_errors.is_error()) {
        auto errors = script_or_errors.release_error();
        warnln("MyceliumVR: script parse error: {}", errors[0].to_string());
        return Error::from_string_literal("Unable to parse MyceliumVR script");
    }

    vm.push_execution_context(*m_global_execution_context);
    auto result = vm.bytecode_interpreter().run(script_or_errors.release_value());
    vm.pop_execution_context();

    if (result.is_error()) {
        report_exception(result.release_error());
        return {};
    }

    m_loaded_scripts.append(entrypoints);

    vm.push_execution_context(*m_global_execution_context);
    auto start_result = call_if_function(m_loaded_scripts.last(), m_loaded_scripts.last().start_name);
    vm.pop_execution_context();

    if (start_result.is_error())
        report_exception(start_result.release_error());

    return {};
}

bool ScriptRuntime::update(double delta_time)
{
#if defined(TRACY_ENABLE)
    ZoneScopedN("Simulation/ScriptUpdate");
#endif
    if (m_loaded_scripts.is_empty())
        return true;

    clear_exception_state();

    auto& vm = m_script_host.vm();
    for (auto& script : m_loaded_scripts) {
        vm.push_execution_context(*m_global_execution_context);
        auto result = call_if_function(script, script.update_name, JS::Value(delta_time));
        vm.pop_execution_context();

        if (result.is_error())
            report_exception(result.release_error());
    }
    return !m_had_uncaught_exception;
}

JS::ThrowCompletionOr<JS::Value> ScriptRuntime::acquire_transform_buffer(JS::Realm& realm, u32 capacity)
{
    constexpr u32 transform_stride = 11;
    constexpr u32 max_transform_buffer_capacity = 1'000'000;

    if (capacity == 0 || capacity > max_transform_buffer_capacity)
        return realm.vm().throw_completion<JS::RangeError>("Invalid transform buffer capacity"sv);

    if (capacity > NumericLimits<u32>::max() / transform_stride)
        return realm.vm().throw_completion<JS::RangeError>("Transform buffer capacity is too large"sv);

    auto buffer = TRY(JS::Float32Array::create(realm, capacity * transform_stride));
    m_transform_buffer = GC::Root<JS::Float32Array> { buffer };
    m_transform_buffer_capacity = capacity;
    return JS::Value(buffer);
}

JS::ThrowCompletionOr<JS::Value> ScriptRuntime::commit_transform_buffer(JS::VM& vm, u32 count)
{
    if (!m_transform_buffer)
        return vm.throw_completion<JS::RangeError>("No transform buffer has been acquired"sv);
    if (count > m_transform_buffer_capacity)
        return vm.throw_completion<JS::RangeError>("Transform buffer commit count exceeds capacity"sv);

    auto applied_count = m_bridge_backend.commit_transform_buffer(m_transform_buffer->data(), count);
    return JS::Value(applied_count);
}

JS::ThrowCompletionOr<JS::Value> ScriptRuntime::read_text(JS::VM& vm, StringView path)
{
    if (!m_virtual_file_system)
        return vm.throw_completion<JS::TypeError>("mycelium.fs.readText: no virtual filesystem is mounted"sv);

    auto source = m_virtual_file_system->read_file(path);
    if (source.is_error())
        return vm.throw_completion<JS::TypeError>(MUST(String::formatted("mycelium.fs.readText: unable to read '{}': {}", path, source.error())));

    auto text = String::from_utf8(StringView { source.value() });
    if (text.is_error())
        return vm.throw_completion<JS::TypeError>(MUST(String::formatted("mycelium.fs.readText: '{}' is not valid UTF-8", path)));

    return JS::PrimitiveString::create(vm, text.release_value());
}

bool ScriptRuntime::file_exists(StringView path) const
{
    if (!m_virtual_file_system)
        return false;
    return m_virtual_file_system->exists(path);
}

ErrorOr<String> ScriptRuntime::network_bootstrap_info_json() const
{
    JsonObject root;
    if (!m_world_runtime || !m_world_runtime->bootstrap_info().has_value())
        return root.serialized();

    auto const& info = m_world_runtime->bootstrap_info().value();
    root.set("world_id"sv, info.world_id);
    root.set("package_name"sv, info.package_name);
    root.set("world_name"sv, info.world_name);
    root.set("mode"sv, info.mode);
    root.set("protocol_version"sv, info.protocol_version);

    JsonArray urls;
    for (auto const& url : info.bootstrap_urls)
        urls.must_append(url);
    root.set("bootstrap_urls"sv, move(urls));

    JsonArray transports;
    for (auto const& transport : info.transports)
        transports.must_append(transport);
    root.set("transports"sv, move(transports));

    JsonObject permissions;
    permissions.set("storage"sv, info.storage_allowed);
    permissions.set("network"sv, info.network_allowed);
    permissions.set("wasm"sv, info.wasm_allowed);
    root.set("permissions"sv, move(permissions));
    return root.serialized();
}

ErrorOr<u32> ScriptRuntime::network_connect(StringView kind, StringView address)
{
    if (!m_world_runtime)
        return Error::from_string_literal("Networking is unavailable outside a world runtime");
    return m_network_service.connect(m_world_runtime->id(), kind, address);
}

ErrorOr<void> ScriptRuntime::network_send(u32 connection_id, ReadonlyBytes payload, u32 flags)
{
    if (!m_world_runtime)
        return Error::from_string_literal("Networking is unavailable outside a world runtime");
    return m_network_service.send(m_world_runtime->id(), connection_id, payload, flags);
}

void ScriptRuntime::network_close(u32 connection_id)
{
    if (!m_world_runtime)
        return;
    m_network_service.close(m_world_runtime->id(), connection_id);
}

Optional<NetworkEvent> ScriptRuntime::network_poll_event()
{
    if (!m_world_runtime)
        return {};
    return m_world_runtime->dequeue_network_event();
}

Optional<String> ScriptRuntime::network_receive_text(u32 connection_id)
{
    if (!m_world_runtime)
        return {};
    return m_world_runtime->dequeue_connection_payload_as_utf8(connection_id);
}

void ScriptRuntime::clear_exception_state()
{
    m_had_uncaught_exception = false;
    m_last_exception_message = {};
}

JS::ThrowCompletionOr<JS::Value> ScriptRuntime::write_text(JS::VM& vm, StringView path, StringView text)
{
    if (!m_virtual_file_system)
        return vm.throw_completion<JS::TypeError>("mycelium.fs.writeText: no virtual filesystem is mounted"sv);

    auto result = m_virtual_file_system->write_file(path, text.bytes());
    if (result.is_error())
        return vm.throw_completion<JS::TypeError>(MUST(String::formatted("mycelium.fs.writeText: unable to write '{}': {}", path, result.error())));

    return JS::Value(true);
}

JS::ThrowCompletionOr<JS::Value> ScriptRuntime::call_if_function(ScriptEntrypoints& script, Utf16FlyString const& name, JS::Value argument)
{
    auto& vm = m_script_host.vm();
    auto value = TRY(m_realm->global_object().get(name));
    if (value.is_undefined()) {
        if (name == script.update_name && !script.reported_missing_update) {
            warnln("MyceliumVR: script has no update(deltaTime) function");
            script.reported_missing_update = true;
        }
        return JS::js_undefined();
    }

    if (!value.is_function())
        return vm.throw_completion<JS::TypeError>("Expected script entrypoint to be a function"sv);

    if (argument.is_undefined())
        return JS::call(vm, value.as_function(), JS::js_undefined());

    return JS::call(vm, value.as_function(), JS::js_undefined(), argument);
}

void ScriptRuntime::report_exception(JS::Completion const& completion)
{
    auto value = completion.value();
    auto message = value.to_string_without_side_effects();
    m_had_uncaught_exception = true;
    m_last_exception_message = MUST(String::from_utf8(message.bytes()));
    warnln("MyceliumVR script exception: {}", message);
    if (m_log_callback)
        m_log_callback("error"sv, "script"sv, message.bytes_as_string_view());
}

}
