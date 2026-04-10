/*
 * MyceliumVR - a social VR platform built on the Ladybird browser engine
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "ScriptRuntime.h"

#include "BridgeRegistry.h"

#include <AK/Format.h>
#include <AK/StringBuilder.h>
#include <LibCore/File.h>
#include <LibJS/Bytecode/Interpreter.h>
#include <LibJS/Runtime/Array.h>
#include <LibJS/Runtime/AbstractOperations.h>
#include <LibJS/Runtime/FunctionObject.h>
#include <LibJS/Runtime/GlobalObject.h>
#include <LibJS/Runtime/Object.h>
#include <LibJS/Runtime/PrimitiveString.h>
#include <LibJS/Runtime/Realm.h>
#include <LibJS/Runtime/TypedArray.h>
#include <LibJS/Runtime/ValueInlines.h>
#include <LibJS/Script.h>

namespace MyceliumVR {

static JS::ThrowCompletionOr<EntityId> validated_entity_argument(JS::VM& vm, BridgeBackend& backend, size_t argument_index, StringView operation)
{
    auto entity = TRY(vm.argument(argument_index).to_u32(vm));
    if (!backend.world().entity(entity))
        return vm.throw_completion<JS::RangeError>(MUST(String::formatted("mycelium.{}: invalid EntityId {}", operation, entity)));
    return entity;
}

static String function_signature(BridgeFunction const& function)
{
    StringBuilder builder;
    builder.appendff("{}.{}(", function.module, function.name);
    for (size_t i = 0; i < function.arguments.size(); ++i) {
        auto const& argument = function.arguments[i];
        if (i > 0)
            builder.append(", "sv);
        builder.appendff("{}: {}", argument.name, bridge_value_type_name(argument.type));
    }
    builder.appendff(") -> {}", bridge_value_type_name(function.return_type));
    return MUST(builder.to_string());
}

static String js_function_name(BridgeFunction const& function)
{
    if (function.js_namespace.is_empty())
        return MUST(String::from_utf8(function.name));
    return MUST(String::formatted("{}.{}", function.js_namespace, function.name));
}

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

        auto& vm = realm.vm();
        define_direct_property("global"_utf16_fly_string, this, JS::default_attributes);

        auto mycelium = JS::Object::create(realm, realm.intrinsics().object_prototype());

        auto api = JS::Object::create(realm, realm.intrinsics().object_prototype());
        api->define_native_function(realm, "functions"_utf16_fly_string, [](JS::VM& vm) -> JS::ThrowCompletionOr<JS::Value> {
            auto& realm = *vm.current_realm();
            auto const& functions = BridgeRegistry::the().functions();
            auto array = TRY(JS::Array::create(realm, functions.size()));
            for (u32 i = 0; i < functions.size(); ++i)
                TRY(array->create_data_property(JS::PropertyKey(i), JS::PrimitiveString::create(vm, js_function_name(functions[i]))));
            return JS::Value(array);
        }, 0, JS::default_attributes);
        api->define_native_function(realm, "hasFunction"_utf16_fly_string, [](JS::VM& vm) -> JS::ThrowCompletionOr<JS::Value> {
            auto name = TRY(vm.argument(0).to_string(vm));
            return JS::Value(BridgeRegistry::the().find_function(name) != nullptr);
        }, 1, JS::default_attributes);
        api->define_native_function(realm, "describeFunction"_utf16_fly_string, [](JS::VM& vm) -> JS::ThrowCompletionOr<JS::Value> {
            auto name = TRY(vm.argument(0).to_string(vm));
            auto const* function = BridgeRegistry::the().find_function(name);
            if (!function)
                return JS::js_undefined();

            auto description = MUST(String::formatted("{} - {}", function_signature(*function), function->description));
            return JS::PrimitiveString::create(vm, description);
        }, 1, JS::default_attributes);
        mycelium->define_direct_property("api"_utf16_fly_string, api, JS::default_attributes);

        auto fs = JS::Object::create(realm, realm.intrinsics().object_prototype());
        fs->define_native_function(realm, "readText"_utf16_fly_string, [this](JS::VM& vm) -> JS::ThrowCompletionOr<JS::Value> {
            auto path = TRY(vm.argument(0).to_string(vm));
            return TRY(m_runtime.read_text(vm, path));
        }, 1, JS::default_attributes);
        fs->define_native_function(realm, "exists"_utf16_fly_string, [this](JS::VM& vm) -> JS::ThrowCompletionOr<JS::Value> {
            auto path = TRY(vm.argument(0).to_string(vm));
            return JS::Value(m_runtime.file_exists(path));
        }, 1, JS::default_attributes);
        fs->define_native_function(realm, "writeText"_utf16_fly_string, [this](JS::VM& vm) -> JS::ThrowCompletionOr<JS::Value> {
            auto path = TRY(vm.argument(0).to_string(vm));
            auto text = TRY(vm.argument(1).to_string(vm));
            return TRY(m_runtime.write_text(vm, path, text));
        }, 2, JS::default_attributes);
        mycelium->define_direct_property("fs"_utf16_fly_string, fs, JS::default_attributes);

        mycelium->define_native_function(realm, "log"_utf16_fly_string, [this](JS::VM& vm) -> JS::ThrowCompletionOr<JS::Value> {
            auto message = TRY(vm.argument(0).to_string(vm));
            m_runtime.bridge_backend().log(move(message));
            return JS::js_undefined();
        }, 1, JS::default_attributes);

        mycelium->define_native_function(realm, "spawnEntity"_utf16_fly_string, [this](JS::VM&) -> JS::ThrowCompletionOr<JS::Value> {
            return JS::Value(m_runtime.bridge_backend().spawn_entity());
        }, 0, JS::default_attributes);

        mycelium->define_native_function(realm, "destroyEntity"_utf16_fly_string, [this](JS::VM& vm) -> JS::ThrowCompletionOr<JS::Value> {
            auto entity = TRY(vm.argument(0).to_u32(vm));
            return JS::Value(m_runtime.bridge_backend().destroy_entity(entity));
        }, 1, JS::default_attributes);

        mycelium->define_native_function(realm, "entityCount"_utf16_fly_string, [this](JS::VM&) -> JS::ThrowCompletionOr<JS::Value> {
            return JS::Value(m_runtime.bridge_backend().entity_count());
        }, 0, JS::default_attributes);

        mycelium->define_native_function(realm, "dirtyTransformCount"_utf16_fly_string, [this](JS::VM&) -> JS::ThrowCompletionOr<JS::Value> {
            return JS::Value(m_runtime.bridge_backend().dirty_transform_count());
        }, 0, JS::default_attributes);

        mycelium->define_native_function(realm, "setTransform"_utf16_fly_string, [this](JS::VM& vm) -> JS::ThrowCompletionOr<JS::Value> {
            auto entity = TRY(validated_entity_argument(vm, m_runtime.bridge_backend(), 0, "setTransform"sv));
            auto px = TRY(vm.argument(1).to_double(vm));
            auto py = TRY(vm.argument(2).to_double(vm));
            auto pz = TRY(vm.argument(3).to_double(vm));
            auto qx = TRY(vm.argument(4).to_double(vm));
            auto qy = TRY(vm.argument(5).to_double(vm));
            auto qz = TRY(vm.argument(6).to_double(vm));
            auto qw = TRY(vm.argument(7).to_double(vm));
            auto sx = TRY(vm.argument(8).to_double(vm));
            auto sy = TRY(vm.argument(9).to_double(vm));
            auto sz = TRY(vm.argument(10).to_double(vm));
            return JS::Value(m_runtime.bridge_backend().set_transform(entity, px, py, pz, qx, qy, qz, qw, sx, sy, sz));
        }, 11, JS::default_attributes);

        mycelium->define_native_function(realm, "acquireTransformBuffer"_utf16_fly_string, [this, &realm](JS::VM& vm) -> JS::ThrowCompletionOr<JS::Value> {
            auto capacity = TRY(vm.argument(0).to_u32(vm));
            return TRY(m_runtime.acquire_transform_buffer(realm, capacity));
        }, 1, JS::default_attributes);

        mycelium->define_native_function(realm, "commitTransformBuffer"_utf16_fly_string, [this](JS::VM& vm) -> JS::ThrowCompletionOr<JS::Value> {
            auto count = TRY(vm.argument(0).to_u32(vm));
            return TRY(m_runtime.commit_transform_buffer(vm, count));
        }, 1, JS::default_attributes);

        mycelium->define_native_function(realm, "setMesh"_utf16_fly_string, [this](JS::VM& vm) -> JS::ThrowCompletionOr<JS::Value> {
            auto entity = TRY(validated_entity_argument(vm, m_runtime.bridge_backend(), 0, "setMesh"sv));
            auto mesh = TRY(vm.argument(1).to_string(vm));
            return JS::Value(m_runtime.bridge_backend().set_mesh(entity, move(mesh)));
        }, 2, JS::default_attributes);

        mycelium->define_native_function(realm, "setMaterial"_utf16_fly_string, [this](JS::VM& vm) -> JS::ThrowCompletionOr<JS::Value> {
            auto entity = TRY(validated_entity_argument(vm, m_runtime.bridge_backend(), 0, "setMaterial"sv));
            auto material = TRY(vm.argument(1).to_string(vm));
            return JS::Value(m_runtime.bridge_backend().set_material(entity, move(material)));
        }, 2, JS::default_attributes);

        mycelium->define_native_function(realm, "createPanel"_utf16_fly_string, [this](JS::VM& vm) -> JS::ThrowCompletionOr<JS::Value> {
            auto entity = TRY(validated_entity_argument(vm, m_runtime.bridge_backend(), 0, "createPanel"sv));
            auto url = TRY(vm.argument(1).to_string(vm));
            auto width = TRY(vm.argument(2).to_double(vm));
            auto height = TRY(vm.argument(3).to_double(vm));
            return JS::Value(m_runtime.bridge_backend().create_panel(entity, move(url), width, height));
        }, 4, JS::default_attributes);

        define_direct_property("mycelium"_utf16_fly_string, mycelium, JS::default_attributes);
        (void)vm;
    }

private:
    ScriptRuntime& m_runtime;
};

GC_DEFINE_ALLOCATOR(MyceliumGlobalObject);

ScriptRuntime::ScriptRuntime(World& world, VirtualFileSystem* virtual_file_system)
    : m_world(world)
    , m_bridge_backend(world)
    , m_virtual_file_system(virtual_file_system)
{
}

ScriptRuntime::~ScriptRuntime() = default;

ErrorOr<void> ScriptRuntime::initialize()
{
    m_vm = JS::VM::create();
    m_vm->set_dynamic_imports_allowed(false);

    GC::Ptr<JS::Realm> realm;
    auto context_or_error = JS::Realm::initialize_host_defined_realm(
        *m_vm,
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
    m_vm->pop_execution_context();
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
        TRY(load_script_source(move(source), path));
        return {};
    }

    TRY(load_script_source(TRY(file.value()->read_until_eof()), path));
    return {};
}

ErrorOr<void> ScriptRuntime::load_script_source(ByteBuffer source, StringView filename)
{
    VERIFY(m_vm);
    VERIFY(m_realm);

    auto script_or_errors = JS::Script::parse(source, *m_realm, filename);
    if (script_or_errors.is_error()) {
        auto errors = script_or_errors.release_error();
        warnln("MyceliumVR: script parse error: {}", errors[0].to_string());
        return Error::from_string_literal("Unable to parse MyceliumVR script");
    }

    m_vm->push_execution_context(*m_global_execution_context);
    auto result = m_vm->bytecode_interpreter().run(script_or_errors.release_value());
    m_vm->pop_execution_context();

    if (result.is_error()) {
        report_exception(result.release_error());
        return {};
    }

    m_script_loaded = true;

    m_vm->push_execution_context(*m_global_execution_context);
    auto start_result = call_if_function("start"_utf16_fly_string);
    m_vm->pop_execution_context();

    if (start_result.is_error())
        report_exception(start_result.release_error());

    return {};
}

void ScriptRuntime::update(double delta_time)
{
    if (!m_script_loaded)
        return;

    m_vm->push_execution_context(*m_global_execution_context);
    auto result = call_if_function("update"_utf16_fly_string, JS::Value(delta_time));
    m_vm->pop_execution_context();

    if (result.is_error())
        report_exception(result.release_error());
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

JS::ThrowCompletionOr<JS::Value> ScriptRuntime::write_text(JS::VM& vm, StringView path, StringView text)
{
    if (!m_virtual_file_system)
        return vm.throw_completion<JS::TypeError>("mycelium.fs.writeText: no virtual filesystem is mounted"sv);

    auto result = m_virtual_file_system->write_file(path, text.bytes());
    if (result.is_error())
        return vm.throw_completion<JS::TypeError>(MUST(String::formatted("mycelium.fs.writeText: unable to write '{}': {}", path, result.error())));

    return JS::Value(true);
}

JS::ThrowCompletionOr<JS::Value> ScriptRuntime::call_if_function(Utf16FlyString const& name, JS::Value argument)
{
    auto value = TRY(m_realm->global_object().get(name));
    if (value.is_undefined()) {
        if (name == "update"_utf16_fly_string && !m_reported_missing_update) {
            warnln("MyceliumVR: script has no update(deltaTime) function");
            m_reported_missing_update = true;
        }
        return JS::js_undefined();
    }

    if (!value.is_function())
        return m_vm->throw_completion<JS::TypeError>("Expected script entrypoint to be a function"sv);

    if (argument.is_undefined())
        return JS::call(*m_vm, value.as_function(), JS::js_undefined());

    return JS::call(*m_vm, value.as_function(), JS::js_undefined(), argument);
}

void ScriptRuntime::report_exception(JS::Completion const& completion)
{
    auto value = completion.value();
    warnln("MyceliumVR script exception: {}", value.to_string_without_side_effects());
}

}
