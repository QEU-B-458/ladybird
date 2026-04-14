/*
 * MyceliumVR - a social VR platform built on the Ladybird browser engine
 * SPDX-License-Identifier: BSD-2-Clause
 */

// ============================================================
// SINGLE SOURCE OF TRUTH for all mycelium JS bridge functions.
//
// To add a function:
//   1. Add a BridgeFunction entry to s_metadata[] below.
//   2. Add the matching impl block inside bind_all(), in the same order.
//   3. Update BRIDGE_FUNCTION_COUNT.
//   That's it — registry, SDK generation, and JS binding all update automatically.
// ============================================================

#include "BridgeFunctions.h"
#include "BridgeRegistry.h"
#include "ScriptRuntime.h"

#include <AK/Format.h>
#include <LibJS/Runtime/Array.h>
#include <LibJS/Runtime/Object.h>
#include <LibJS/Runtime/PrimitiveString.h>
#include <LibJS/Runtime/TypedArray.h>
#include <LibJS/Runtime/Value.h>
#include <LibJS/Runtime/ValueInlines.h>
#include <SDL3/SDL.h>

namespace MyceliumVR::BridgeFunctions {

// ============================================================
// SECTION 1: Metadata table
// Pure data — no LibJS types, no ScriptRuntime.
// Consumed by BridgeRegistry at static-init time and by SDKGenerator.
// ============================================================

// Must match the number of impl blocks in bind_all().
static constexpr size_t BRIDGE_FUNCTION_COUNT = 22;

static BridgeFunction const s_metadata[BRIDGE_FUNCTION_COUNT] = {
    // -- Debug --
    {
        .module = "Debug"sv, .js_namespace = ""sv, .name = "log"sv,
        .description = "Write a message to the native MyceliumVR log."sv,
        .return_type = BridgeValueType::Void,
        .arguments = { { "message"sv, BridgeValueType::String } },
        .hot_path = false, .debug = false, .js_exposed = true, .wasm_exposed = true,
        .throws = ""sv,
    },
    {
        .module = "Debug"sv, .js_namespace = ""sv, .name = "entityCount"sv,
        .description = "Return the number of live native ECS entities."sv,
        .return_type = BridgeValueType::Number,
        .arguments = {},
        .hot_path = false, .debug = true, .js_exposed = true, .wasm_exposed = true,
        .throws = ""sv,
    },
    {
        .module = "Debug"sv, .js_namespace = ""sv, .name = "dirtyTransformCount"sv,
        .description = "Return the number of live entities with dirty transforms."sv,
        .return_type = BridgeValueType::Number,
        .arguments = {},
        .hot_path = false, .debug = true, .js_exposed = true, .wasm_exposed = true,
        .throws = ""sv,
    },
    // -- World --
    {
        .module = "World"sv, .js_namespace = ""sv, .name = "spawnEntity"sv,
        .description = "Create a native ECS entity and return its opaque handle."sv,
        .return_type = BridgeValueType::EntityId,
        .arguments = {},
        .hot_path = false, .debug = false, .js_exposed = true, .wasm_exposed = true,
        .throws = ""sv,
    },
    {
        .module = "World"sv, .js_namespace = ""sv, .name = "destroyEntity"sv,
        .description = "Destroy a native ECS entity. Returns false for stale or invalid handles."sv,
        .return_type = BridgeValueType::Boolean,
        .arguments = { { "entity"sv, BridgeValueType::EntityId } },
        .hot_path = false, .debug = false, .js_exposed = true, .wasm_exposed = true,
        .throws = ""sv,
    },
    {
        .module = "World"sv, .js_namespace = ""sv, .name = "entityIdAt"sv,
        .description = "Return the EntityId at a given dense-array index, or null if out of range."sv,
        .return_type = BridgeValueType::EntityId,
        .arguments = { { "index"sv, BridgeValueType::Number } },
        .hot_path = false, .debug = true, .js_exposed = true, .wasm_exposed = false,
        .throws = ""sv,
    },
    // -- Transform --
    {
        .module = "Transform"sv, .js_namespace = ""sv, .name = "setTransform"sv,
        .description = "Write a transform directly into native ECS storage."sv,
        .return_type = BridgeValueType::Boolean,
        .arguments = {
            { "entity"sv, BridgeValueType::EntityId },
            { "px"sv, BridgeValueType::Number }, { "py"sv, BridgeValueType::Number }, { "pz"sv, BridgeValueType::Number },
            { "qx"sv, BridgeValueType::Number }, { "qy"sv, BridgeValueType::Number }, { "qz"sv, BridgeValueType::Number }, { "qw"sv, BridgeValueType::Number },
            { "sx"sv, BridgeValueType::Number }, { "sy"sv, BridgeValueType::Number }, { "sz"sv, BridgeValueType::Number },
        },
        .hot_path = true, .debug = false, .js_exposed = true, .wasm_exposed = true,
        .throws = "RangeError for invalid EntityId."sv,
    },
    {
        .module = "Transform"sv, .js_namespace = ""sv, .name = "getTransform"sv,
        .description = "Return the current transform of an entity as an object {px,py,pz,qx,qy,qz,qw,sx,sy,sz}, or null."sv,
        .return_type = BridgeValueType::Number, // object, approximated
        .arguments = { { "entity"sv, BridgeValueType::EntityId } },
        .hot_path = false, .debug = false, .js_exposed = true, .wasm_exposed = false,
        .throws = "RangeError for invalid EntityId."sv,
    },
    {
        .module = "Transform"sv, .js_namespace = ""sv, .name = "acquireTransformBuffer"sv,
        .description = "Acquire a Float32Array for bulk transform writes."sv,
        .return_type = BridgeValueType::Float32Array,
        .arguments = { { "capacity"sv, BridgeValueType::Number } },
        .hot_path = true, .debug = false, .js_exposed = true, .wasm_exposed = false,
        .throws = "RangeError for invalid capacity."sv,
    },
    {
        .module = "Transform"sv, .js_namespace = ""sv, .name = "commitTransformBuffer"sv,
        .description = "Apply rows from the acquired transform buffer and return the accepted transform count."sv,
        .return_type = BridgeValueType::Number,
        .arguments = { { "count"sv, BridgeValueType::Number } },
        .hot_path = true, .debug = false, .js_exposed = true, .wasm_exposed = false,
        .throws = "RangeError for invalid count."sv,
    },
    // -- Render --
    {
        .module = "Render"sv, .js_namespace = ""sv, .name = "setMesh"sv,
        .description = "Assign a mesh asset handle to an entity."sv,
        .return_type = BridgeValueType::Boolean,
        .arguments = { { "entity"sv, BridgeValueType::EntityId }, { "mesh"sv, BridgeValueType::String } },
        .hot_path = false, .debug = false, .js_exposed = true, .wasm_exposed = true,
        .throws = "RangeError for invalid EntityId."sv,
    },
    {
        .module = "Render"sv, .js_namespace = ""sv, .name = "setMaterial"sv,
        .description = "Assign a material asset handle to an entity."sv,
        .return_type = BridgeValueType::Boolean,
        .arguments = { { "entity"sv, BridgeValueType::EntityId }, { "material"sv, BridgeValueType::String } },
        .hot_path = false, .debug = false, .js_exposed = true, .wasm_exposed = true,
        .throws = "RangeError for invalid EntityId."sv,
    },
    {
        .module = "Render"sv, .js_namespace = ""sv, .name = "setNormalMap"sv,
        .description = "Assign a normal map texture path to an entity."sv,
        .return_type = BridgeValueType::Boolean,
        .arguments = { { "entity"sv, BridgeValueType::EntityId }, { "path"sv, BridgeValueType::String } },
        .hot_path = false, .debug = false, .js_exposed = true, .wasm_exposed = true,
        .throws = "RangeError for invalid EntityId."sv,
    },
    // -- Camera --
    {
        .module = "Camera"sv, .js_namespace = ""sv, .name = "setCamera"sv,
        .description = "Set camera position and Euler orientation (yaw/pitch in degrees)."sv,
        .return_type = BridgeValueType::Void,
        .arguments = {
            { "px"sv, BridgeValueType::Number }, { "py"sv, BridgeValueType::Number }, { "pz"sv, BridgeValueType::Number },
            { "yaw"sv, BridgeValueType::Number }, { "pitch"sv, BridgeValueType::Number },
        },
        .hot_path = false, .debug = false, .js_exposed = true, .wasm_exposed = true,
        .throws = ""sv,
    },
    {
        .module = "Camera"sv, .js_namespace = ""sv, .name = "getCamera"sv,
        .description = "Return current camera state as {px,py,pz,yaw,pitch}, or null if no camera."sv,
        .return_type = BridgeValueType::Number, // object, approximated
        .arguments = {},
        .hot_path = false, .debug = false, .js_exposed = true, .wasm_exposed = false,
        .throws = ""sv,
    },
    // -- Lighting --
    {
        .module = "Lighting"sv, .js_namespace = ""sv, .name = "setAmbientLight"sv,
        .description = "Set scene ambient light color and intensity."sv,
        .return_type = BridgeValueType::Void,
        .arguments = {
            { "r"sv, BridgeValueType::Number }, { "g"sv, BridgeValueType::Number },
            { "b"sv, BridgeValueType::Number }, { "intensity"sv, BridgeValueType::Number },
        },
        .hot_path = false, .debug = false, .js_exposed = true, .wasm_exposed = true,
        .throws = ""sv,
    },
    {
        .module = "Lighting"sv, .js_namespace = ""sv, .name = "setDirectionalLight"sv,
        .description = "Set scene directional (sun) light direction, color, and intensity."sv,
        .return_type = BridgeValueType::Void,
        .arguments = {
            { "toX"sv, BridgeValueType::Number }, { "toY"sv, BridgeValueType::Number }, { "toZ"sv, BridgeValueType::Number },
            { "r"sv, BridgeValueType::Number }, { "g"sv, BridgeValueType::Number }, { "b"sv, BridgeValueType::Number },
            { "intensity"sv, BridgeValueType::Number },
        },
        .hot_path = false, .debug = false, .js_exposed = true, .wasm_exposed = true,
        .throws = ""sv,
    },
    {
        .module = "Lighting"sv, .js_namespace = ""sv, .name = "setPointLight"sv,
        .description = "Set a point light by index (0–7): position, color, intensity, radius."sv,
        .return_type = BridgeValueType::Void,
        .arguments = {
            { "index"sv, BridgeValueType::Number },
            { "x"sv, BridgeValueType::Number }, { "y"sv, BridgeValueType::Number }, { "z"sv, BridgeValueType::Number },
            { "r"sv, BridgeValueType::Number }, { "g"sv, BridgeValueType::Number }, { "b"sv, BridgeValueType::Number },
            { "intensity"sv, BridgeValueType::Number }, { "radius"sv, BridgeValueType::Number },
        },
        .hot_path = false, .debug = false, .js_exposed = true, .wasm_exposed = true,
        .throws = ""sv,
    },
    {
        .module = "Lighting"sv, .js_namespace = ""sv, .name = "setSpotLight"sv,
        .description = "Set a spot light by index (0–7): position, direction, cone angles, color, intensity, radius."sv,
        .return_type = BridgeValueType::Void,
        .arguments = {
            { "index"sv, BridgeValueType::Number },
            { "x"sv, BridgeValueType::Number }, { "y"sv, BridgeValueType::Number }, { "z"sv, BridgeValueType::Number },
            { "dirX"sv, BridgeValueType::Number }, { "dirY"sv, BridgeValueType::Number }, { "dirZ"sv, BridgeValueType::Number },
            { "innerDeg"sv, BridgeValueType::Number }, { "outerDeg"sv, BridgeValueType::Number },
            { "r"sv, BridgeValueType::Number }, { "g"sv, BridgeValueType::Number }, { "b"sv, BridgeValueType::Number },
            { "intensity"sv, BridgeValueType::Number }, { "radius"sv, BridgeValueType::Number },
        },
        .hot_path = false, .debug = false, .js_exposed = true, .wasm_exposed = true,
        .throws = ""sv,
    },
    {
        .module = "Lighting"sv, .js_namespace = ""sv, .name = "clearPointLights"sv,
        .description = "Remove all active point and spot lights from the scene."sv,
        .return_type = BridgeValueType::Void,
        .arguments = {},
        .hot_path = false, .debug = false, .js_exposed = true, .wasm_exposed = true,
        .throws = ""sv,
    },
    {
        .module = "Lighting"sv, .js_namespace = ""sv, .name = "getSceneLight"sv,
        .description = "Return current scene light state as an object, or null if not set."sv,
        .return_type = BridgeValueType::Number, // object, approximated
        .arguments = {},
        .hot_path = false, .debug = false, .js_exposed = true, .wasm_exposed = false,
        .throws = ""sv,
    },
    // -- Panel --
    {
        .module = "Panel"sv, .js_namespace = ""sv, .name = "createPanel"sv,
        .description = "Attach a WebContent panel definition to an entity."sv,
        .return_type = BridgeValueType::Boolean,
        .arguments = {
            { "entity"sv, BridgeValueType::EntityId },
            { "url"sv, BridgeValueType::String },
            { "width"sv, BridgeValueType::Number },
            { "height"sv, BridgeValueType::Number },
        },
        .hot_path = false, .debug = false, .js_exposed = true, .wasm_exposed = true,
        .throws = "RangeError for invalid EntityId."sv,
    },
    // fs.*, input.*, and api.* are bound as namespaced sub-objects but not
    // individually listed here — they are implicitly covered by the sub-object
    // binding in bind_all() below. They are not individually discoverable via
    // api.functions() which is intentional (they are infrastructure, not world API).
};

static_assert(std::size(s_metadata) == BRIDGE_FUNCTION_COUNT);

ReadonlySpan<BridgeFunction> all_metadata()
{
    return ReadonlySpan<BridgeFunction>(s_metadata, BRIDGE_FUNCTION_COUNT);
}

// ============================================================
// SECTION 2: Implementation bindings
// Must be in the same order as s_metadata[].
// Update BRIDGE_FUNCTION_COUNT when adding entries to both sections.
// ============================================================

static JS::ThrowCompletionOr<EntityId> validated_entity(JS::VM& vm, BridgeBackend& backend, size_t arg_index, StringView op)
{
    auto entity = TRY(vm.argument(arg_index).to_u32(vm));
    if (!backend.world().entity(entity))
        return vm.throw_completion<JS::RangeError>(MUST(String::formatted("mycelium.{}: invalid EntityId {}", op, entity)));
    return entity;
}

void bind_all(
    ScriptRuntime& runtime,
    JS::Realm& realm,
    JS::Object& mycelium,
    JS::Object& api,
    JS::Object& fs,
    JS::Object& input)
{
    // Registry is pre-populated from s_metadata[] in BridgeRegistry's constructor.
    // bind_all only needs to bind the impl lambdas to JS objects.
    size_t metadata_index = 0;
    auto reg = [&](JS::Object& target, auto&& impl, u32 arg_count) {
        VERIFY(metadata_index < BRIDGE_FUNCTION_COUNT);
        auto const& meta = s_metadata[metadata_index++];
        target.define_native_function(realm, Utf16FlyString::from_utf8(meta.name), move(impl), arg_count, JS::default_attributes);
    };

    // -- Debug --
    reg(mycelium, [&runtime](JS::VM& vm) -> JS::ThrowCompletionOr<JS::Value> {
        auto message = TRY(vm.argument(0).to_string(vm));
        runtime.bridge_backend().log(move(message));
        return JS::js_undefined();
    }, 1); // log

    reg(mycelium, [&runtime](JS::VM&) -> JS::ThrowCompletionOr<JS::Value> {
        return JS::Value(runtime.bridge_backend().entity_count());
    }, 0); // entityCount

    reg(mycelium, [&runtime](JS::VM&) -> JS::ThrowCompletionOr<JS::Value> {
        return JS::Value(runtime.bridge_backend().dirty_transform_count());
    }, 0); // dirtyTransformCount

    // -- World --
    reg(mycelium, [&runtime](JS::VM&) -> JS::ThrowCompletionOr<JS::Value> {
        return JS::Value(runtime.bridge_backend().spawn_entity());
    }, 0); // spawnEntity

    reg(mycelium, [&runtime](JS::VM& vm) -> JS::ThrowCompletionOr<JS::Value> {
        auto entity = TRY(vm.argument(0).to_u32(vm));
        return JS::Value(runtime.bridge_backend().destroy_entity(entity));
    }, 1); // destroyEntity

    reg(mycelium, [&runtime](JS::VM& vm) -> JS::ThrowCompletionOr<JS::Value> {
        auto index = TRY(vm.argument(0).to_u32(vm));
        auto entity_id = runtime.bridge_backend().entity_id_at(index);
        if (!entity_id.has_value())
            return JS::js_null();
        return JS::Value(*entity_id);
    }, 1); // entityIdAt

    // -- Transform --
    reg(mycelium, [&runtime](JS::VM& vm) -> JS::ThrowCompletionOr<JS::Value> {
        auto entity = TRY(validated_entity(vm, runtime.bridge_backend(), 0, "setTransform"sv));
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
        return JS::Value(runtime.bridge_backend().set_transform(entity, px, py, pz, qx, qy, qz, qw, sx, sy, sz));
    }, 11); // setTransform

    reg(mycelium, [&runtime, &realm](JS::VM& vm) -> JS::ThrowCompletionOr<JS::Value> {
        auto entity = TRY(validated_entity(vm, runtime.bridge_backend(), 0, "getTransform"sv));
        auto transform = runtime.bridge_backend().transform_for_entity(entity);
        if (!transform.has_value())
            return JS::js_null();
        auto object = JS::Object::create(realm, realm.intrinsics().object_prototype());
        TRY(object->create_data_property("px"_utf16_fly_string, JS::Value(transform->position[0])));
        TRY(object->create_data_property("py"_utf16_fly_string, JS::Value(transform->position[1])));
        TRY(object->create_data_property("pz"_utf16_fly_string, JS::Value(transform->position[2])));
        TRY(object->create_data_property("qx"_utf16_fly_string, JS::Value(transform->rotation[0])));
        TRY(object->create_data_property("qy"_utf16_fly_string, JS::Value(transform->rotation[1])));
        TRY(object->create_data_property("qz"_utf16_fly_string, JS::Value(transform->rotation[2])));
        TRY(object->create_data_property("qw"_utf16_fly_string, JS::Value(transform->rotation[3])));
        TRY(object->create_data_property("sx"_utf16_fly_string, JS::Value(transform->scale[0])));
        TRY(object->create_data_property("sy"_utf16_fly_string, JS::Value(transform->scale[1])));
        TRY(object->create_data_property("sz"_utf16_fly_string, JS::Value(transform->scale[2])));
        return JS::Value(object);
    }, 1); // getTransform

    reg(mycelium, [&runtime, &realm](JS::VM& vm) -> JS::ThrowCompletionOr<JS::Value> {
        auto capacity = TRY(vm.argument(0).to_u32(vm));
        return TRY(runtime.acquire_transform_buffer(realm, capacity));
    }, 1); // acquireTransformBuffer

    reg(mycelium, [&runtime](JS::VM& vm) -> JS::ThrowCompletionOr<JS::Value> {
        auto count = TRY(vm.argument(0).to_u32(vm));
        return TRY(runtime.commit_transform_buffer(vm, count));
    }, 1); // commitTransformBuffer

    // -- Render --
    reg(mycelium, [&runtime](JS::VM& vm) -> JS::ThrowCompletionOr<JS::Value> {
        auto entity = TRY(validated_entity(vm, runtime.bridge_backend(), 0, "setMesh"sv));
        auto mesh = TRY(vm.argument(1).to_string(vm));
        return JS::Value(runtime.bridge_backend().set_mesh(entity, move(mesh)));
    }, 2); // setMesh

    reg(mycelium, [&runtime](JS::VM& vm) -> JS::ThrowCompletionOr<JS::Value> {
        auto entity = TRY(validated_entity(vm, runtime.bridge_backend(), 0, "setMaterial"sv));
        auto material = TRY(vm.argument(1).to_string(vm));
        return JS::Value(runtime.bridge_backend().set_material(entity, move(material)));
    }, 2); // setMaterial

    reg(mycelium, [&runtime](JS::VM& vm) -> JS::ThrowCompletionOr<JS::Value> {
        auto entity = TRY(validated_entity(vm, runtime.bridge_backend(), 0, "setNormalMap"sv));
        auto normal_map = TRY(vm.argument(1).to_string(vm));
        return JS::Value(runtime.bridge_backend().set_normal_map(entity, move(normal_map)));
    }, 2); // setNormalMap

    // -- Camera --
    reg(mycelium, [&runtime](JS::VM& vm) -> JS::ThrowCompletionOr<JS::Value> {
        auto px    = TRY(vm.argument(0).to_double(vm));
        auto py    = TRY(vm.argument(1).to_double(vm));
        auto pz    = TRY(vm.argument(2).to_double(vm));
        auto yaw   = TRY(vm.argument(3).to_double(vm));
        auto pitch = TRY(vm.argument(4).to_double(vm));
        runtime.bridge_backend().set_camera(px, py, pz, yaw, pitch);
        return JS::js_undefined();
    }, 5); // setCamera

    reg(mycelium, [&runtime, &realm](JS::VM&) -> JS::ThrowCompletionOr<JS::Value> {
        auto camera_state = runtime.bridge_backend().camera_state();
        if (!camera_state.has_value())
            return JS::js_null();
        auto object = JS::Object::create(realm, realm.intrinsics().object_prototype());
        TRY(object->create_data_property("px"_utf16_fly_string,    JS::Value(camera_state->position[0])));
        TRY(object->create_data_property("py"_utf16_fly_string,    JS::Value(camera_state->position[1])));
        TRY(object->create_data_property("pz"_utf16_fly_string,    JS::Value(camera_state->position[2])));
        TRY(object->create_data_property("yaw"_utf16_fly_string,   JS::Value(camera_state->yaw_degrees)));
        TRY(object->create_data_property("pitch"_utf16_fly_string, JS::Value(camera_state->pitch_degrees)));
        return JS::Value(object);
    }, 0); // getCamera

    // -- Lighting --
    reg(mycelium, [&runtime](JS::VM& vm) -> JS::ThrowCompletionOr<JS::Value> {
        auto r         = TRY(vm.argument(0).to_double(vm));
        auto g         = TRY(vm.argument(1).to_double(vm));
        auto b         = TRY(vm.argument(2).to_double(vm));
        auto intensity = TRY(vm.argument(3).to_double(vm));
        runtime.bridge_backend().set_ambient_light(r, g, b, intensity);
        return JS::js_undefined();
    }, 4); // setAmbientLight

    reg(mycelium, [&runtime](JS::VM& vm) -> JS::ThrowCompletionOr<JS::Value> {
        auto to_x      = TRY(vm.argument(0).to_double(vm));
        auto to_y      = TRY(vm.argument(1).to_double(vm));
        auto to_z      = TRY(vm.argument(2).to_double(vm));
        auto r         = TRY(vm.argument(3).to_double(vm));
        auto g         = TRY(vm.argument(4).to_double(vm));
        auto b         = TRY(vm.argument(5).to_double(vm));
        auto intensity = TRY(vm.argument(6).to_double(vm));
        runtime.bridge_backend().set_directional_light(to_x, to_y, to_z, r, g, b, intensity);
        return JS::js_undefined();
    }, 7); // setDirectionalLight

    reg(mycelium, [&runtime](JS::VM& vm) -> JS::ThrowCompletionOr<JS::Value> {
        auto index     = TRY(vm.argument(0).to_i32(vm));
        auto x         = TRY(vm.argument(1).to_double(vm));
        auto y         = TRY(vm.argument(2).to_double(vm));
        auto z         = TRY(vm.argument(3).to_double(vm));
        auto r         = TRY(vm.argument(4).to_double(vm));
        auto g         = TRY(vm.argument(5).to_double(vm));
        auto b         = TRY(vm.argument(6).to_double(vm));
        auto intensity = TRY(vm.argument(7).to_double(vm));
        auto radius    = TRY(vm.argument(8).to_double(vm));
        runtime.bridge_backend().set_point_light(index, x, y, z, r, g, b, intensity, radius);
        return JS::js_undefined();
    }, 9); // setPointLight

    reg(mycelium, [&runtime](JS::VM& vm) -> JS::ThrowCompletionOr<JS::Value> {
        auto index     = TRY(vm.argument(0).to_i32(vm));
        auto x         = TRY(vm.argument(1).to_double(vm));
        auto y         = TRY(vm.argument(2).to_double(vm));
        auto z         = TRY(vm.argument(3).to_double(vm));
        auto dx        = TRY(vm.argument(4).to_double(vm));
        auto dy        = TRY(vm.argument(5).to_double(vm));
        auto dz        = TRY(vm.argument(6).to_double(vm));
        auto inner_deg = TRY(vm.argument(7).to_double(vm));
        auto outer_deg = TRY(vm.argument(8).to_double(vm));
        auto r         = TRY(vm.argument(9).to_double(vm));
        auto g         = TRY(vm.argument(10).to_double(vm));
        auto b         = TRY(vm.argument(11).to_double(vm));
        auto intensity = TRY(vm.argument(12).to_double(vm));
        auto radius    = TRY(vm.argument(13).to_double(vm));
        runtime.bridge_backend().set_spot_light(index, x, y, z, dx, dy, dz, inner_deg, outer_deg, r, g, b, intensity, radius);
        return JS::js_undefined();
    }, 14); // setSpotLight

    reg(mycelium, [&runtime](JS::VM&) -> JS::ThrowCompletionOr<JS::Value> {
        runtime.bridge_backend().clear_point_lights();
        return JS::js_undefined();
    }, 0); // clearPointLights

    reg(mycelium, [&runtime, &realm](JS::VM&) -> JS::ThrowCompletionOr<JS::Value> {
        auto light = runtime.bridge_backend().scene_light();
        if (!light.has_value())
            return JS::js_null();
        auto object = JS::Object::create(realm, realm.intrinsics().object_prototype());
        TRY(object->create_data_property("ambientR"_utf16_fly_string,        JS::Value(light->ambient_rgb[0])));
        TRY(object->create_data_property("ambientG"_utf16_fly_string,        JS::Value(light->ambient_rgb[1])));
        TRY(object->create_data_property("ambientB"_utf16_fly_string,        JS::Value(light->ambient_rgb[2])));
        TRY(object->create_data_property("ambientIntensity"_utf16_fly_string, JS::Value(light->ambient_intensity)));
        TRY(object->create_data_property("lightToX"_utf16_fly_string,        JS::Value(light->light_to_xyz[0])));
        TRY(object->create_data_property("lightToY"_utf16_fly_string,        JS::Value(light->light_to_xyz[1])));
        TRY(object->create_data_property("lightToZ"_utf16_fly_string,        JS::Value(light->light_to_xyz[2])));
        TRY(object->create_data_property("lightIntensity"_utf16_fly_string,  JS::Value(light->light_intensity)));
        TRY(object->create_data_property("lightR"_utf16_fly_string,          JS::Value(light->light_rgb[0])));
        TRY(object->create_data_property("lightG"_utf16_fly_string,          JS::Value(light->light_rgb[1])));
        TRY(object->create_data_property("lightB"_utf16_fly_string,          JS::Value(light->light_rgb[2])));
        return JS::Value(object);
    }, 0); // getSceneLight

    // -- Panel --
    reg(mycelium, [&runtime](JS::VM& vm) -> JS::ThrowCompletionOr<JS::Value> {
        auto entity = TRY(validated_entity(vm, runtime.bridge_backend(), 0, "createPanel"sv));
        auto url    = TRY(vm.argument(1).to_string(vm));
        auto width  = TRY(vm.argument(2).to_double(vm));
        auto height = TRY(vm.argument(3).to_double(vm));
        return JS::Value(runtime.bridge_backend().create_panel(entity, move(url), width, height));
    }, 4); // createPanel

    // Verify no metadata/impl count drift.
    VERIFY(metadata_index == BRIDGE_FUNCTION_COUNT);

    // ---- Sub-object namespaces (fs.*, input.*, api.*) ----
    // These are infrastructure bindings not individually listed in s_metadata.

    fs.define_native_function(realm, "readText"_utf16_fly_string, [&runtime](JS::VM& vm) -> JS::ThrowCompletionOr<JS::Value> {
        auto path = TRY(vm.argument(0).to_string(vm));
        return TRY(runtime.read_text(vm, path));
    }, 1, JS::default_attributes);

    fs.define_native_function(realm, "exists"_utf16_fly_string, [&runtime](JS::VM& vm) -> JS::ThrowCompletionOr<JS::Value> {
        auto path = TRY(vm.argument(0).to_string(vm));
        return JS::Value(runtime.file_exists(path));
    }, 1, JS::default_attributes);

    fs.define_native_function(realm, "writeText"_utf16_fly_string, [&runtime](JS::VM& vm) -> JS::ThrowCompletionOr<JS::Value> {
        auto path = TRY(vm.argument(0).to_string(vm));
        auto text = TRY(vm.argument(1).to_string(vm));
        return TRY(runtime.write_text(vm, path, text));
    }, 2, JS::default_attributes);

    input.define_native_function(realm, "isKeyDown"_utf16_fly_string, [&runtime](JS::VM& vm) -> JS::ThrowCompletionOr<JS::Value> {
        if (!runtime.input_state())
            return JS::Value(false);
        auto key = TRY(vm.argument(0).to_string(vm));
        return JS::Value(runtime.input_state()->key_down(key));
    }, 1, JS::default_attributes);

    input.define_native_function(realm, "consumeKeyPress"_utf16_fly_string, [&runtime](JS::VM& vm) -> JS::ThrowCompletionOr<JS::Value> {
        if (!runtime.input_state())
            return JS::Value(false);
        auto key      = TRY(vm.argument(0).to_string(vm));
        auto key_name = key.to_byte_string();
        auto scancode = SDL_GetScancodeFromName(key_name.characters());
        if (scancode == SDL_SCANCODE_UNKNOWN)
            return JS::Value(false);
        return JS::Value(runtime.input_state()->consume_key_press(scancode));
    }, 1, JS::default_attributes);

    input.define_native_function(realm, "isMouseButtonDown"_utf16_fly_string, [&runtime](JS::VM& vm) -> JS::ThrowCompletionOr<JS::Value> {
        if (!runtime.input_state())
            return JS::Value(false);
        auto button = TRY(vm.argument(0).to_u8(vm));
        return JS::Value(runtime.input_state()->mouse_button_down(button));
    }, 1, JS::default_attributes);

    input.define_native_function(realm, "mouseDeltaX"_utf16_fly_string, [&runtime](JS::VM&) -> JS::ThrowCompletionOr<JS::Value> {
        return JS::Value(runtime.input_state() ? runtime.input_state()->mouse_delta_x() : 0.0f);
    }, 0, JS::default_attributes);

    input.define_native_function(realm, "mouseDeltaY"_utf16_fly_string, [&runtime](JS::VM&) -> JS::ThrowCompletionOr<JS::Value> {
        return JS::Value(runtime.input_state() ? runtime.input_state()->mouse_delta_y() : 0.0f);
    }, 0, JS::default_attributes);

    input.define_native_function(realm, "wheelDeltaY"_utf16_fly_string, [&runtime](JS::VM&) -> JS::ThrowCompletionOr<JS::Value> {
        return JS::Value(runtime.input_state() ? runtime.input_state()->wheel_delta_y() : 0.0f);
    }, 0, JS::default_attributes);

    api.define_native_function(realm, "functions"_utf16_fly_string, [](JS::VM& vm) -> JS::ThrowCompletionOr<JS::Value> {
        auto& realm = *vm.current_realm();
        auto const& functions = BridgeRegistry::the().functions();
        auto array = TRY(JS::Array::create(realm, functions.size()));
        for (u32 i = 0; i < functions.size(); ++i) {
            auto const& fn = functions[i];
            String name = fn.js_namespace.is_empty()
                ? MUST(String::from_utf8(fn.name))
                : MUST(String::formatted("{}.{}", fn.js_namespace, fn.name));
            TRY(array->create_data_property(JS::PropertyKey(i), JS::PrimitiveString::create(vm, name)));
        }
        return JS::Value(array);
    }, 0, JS::default_attributes);

    api.define_native_function(realm, "hasFunction"_utf16_fly_string, [](JS::VM& vm) -> JS::ThrowCompletionOr<JS::Value> {
        auto name = TRY(vm.argument(0).to_string(vm));
        return JS::Value(BridgeRegistry::the().find_function(name) != nullptr);
    }, 1, JS::default_attributes);

    api.define_native_function(realm, "describeFunction"_utf16_fly_string, [](JS::VM& vm) -> JS::ThrowCompletionOr<JS::Value> {
        auto name = TRY(vm.argument(0).to_string(vm));
        auto const* function = BridgeRegistry::the().find_function(name);
        if (!function)
            return JS::js_undefined();
        StringBuilder builder;
        builder.appendff("{}.{}(", function->module, function->name);
        for (size_t i = 0; i < function->arguments.size(); ++i) {
            if (i > 0) builder.append(", "sv);
            builder.appendff("{}: {}", function->arguments[i].name, bridge_value_type_name(function->arguments[i].type));
        }
        builder.appendff(") -> {} — {}", bridge_value_type_name(function->return_type), function->description);
        return JS::PrimitiveString::create(vm, MUST(builder.to_string()));
    }, 1, JS::default_attributes);
}

} // namespace MyceliumVR::BridgeFunctions
