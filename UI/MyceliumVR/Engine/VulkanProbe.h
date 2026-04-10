/*
 * MyceliumVR - a social VR platform built on the Ladybird browser engine
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <AK/Error.h>
#include <SDL3/SDL.h>

namespace MyceliumVR {

ErrorOr<void> run_vulkan_probe(SDL_Window&);

}
