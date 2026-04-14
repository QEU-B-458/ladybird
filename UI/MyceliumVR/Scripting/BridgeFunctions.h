/*
 * MyceliumVR - a social VR platform built on the Ladybird browser engine
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

// BridgeFunctions is THE single source of truth for all mycelium bridge functions.
//
// Adding a new function requires exactly one edit: add an entry in BridgeFunctions.cpp.
// The metadata is automatically available to BridgeRegistry (and therefore SDKGenerator
// and api.*), and the implementation is automatically bound by ScriptRuntime.
//
// Layout of BridgeFunctions.cpp:
//   1. all_metadata() — pure metadata array, no LibJS types, no ScriptRuntime.
//      BridgeRegistry pulls from this at static-init time (works for --generate-sdk).
//   2. bind_all() — impl lambdas, same order as all_metadata().
//      MyceliumGlobalObject::initialize calls this to bind everything.
//   A VERIFY at the end of bind_all() catches any count mismatch at startup.

#include "BridgeRegistry.h"

#include <AK/Span.h>
#include <LibJS/Forward.h>

namespace MyceliumVR {

class ScriptRuntime;

namespace BridgeFunctions {

// Returns the full metadata table. Called by BridgeRegistry constructor.
// No ScriptRuntime dependency — safe to call for SDK generation.
ReadonlySpan<BridgeFunction> all_metadata();

// Binds all impl lambdas to the four JS sub-objects.
// Registry is already populated from all_metadata() at static-init time.
// Must be called exactly once during global object init.
void bind_all(
    ScriptRuntime& runtime,
    JS::Realm& realm,
    JS::Object& mycelium,
    JS::Object& api,
    JS::Object& fs,
    JS::Object& input);

} // namespace BridgeFunctions

} // namespace MyceliumVR
