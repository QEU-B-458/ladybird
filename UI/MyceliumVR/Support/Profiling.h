/*
 * MyceliumVR - a social VR platform built on the Ladybird browser engine
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

// Single include point for Tracy profiling macros.
// When TRACY_ENABLE is not defined all macros expand to nothing and
// TracyVkCtx is aliased to void* so the rest of the codebase compiles
// without conditional guards at every call site.

#if defined(TRACY_ENABLE)
#    include <tracy/Tracy.hpp>
// TracyVulkan.hpp requires Vulkan headers to come first.
// Only include it when vulkan/vulkan.h has already been included by the caller.
#    if defined(VULKAN_H_)
#        include <tracy/TracyVulkan.hpp>
#    endif
#else
#    define ZoneScoped
#    define ZoneScopedN(name)
#    define ZoneText(text, size)
#    define FrameMark
#    define FrameMarkNamed(name)
#    define FrameMarkStart(name)
#    define FrameMarkEnd(name)
#    define TracyVkContext(...) nullptr
#    define TracyVkDestroy(ctx)
#    define TracyVkCollect(ctx, cmd)
#    define TracyVkZone(ctx, cmd, name)
using TracyVkCtx = void*;
#endif
