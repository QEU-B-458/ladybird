/*
 * MyceliumVR - a social VR platform built on the Ladybird browser engine
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "VulkanProbe.h"

#include "VulkanRenderer.h"

namespace MyceliumVR {

ErrorOr<void> run_vulkan_probe(SDL_Window& window)
{
    outln("MyceliumVR Vulkan probe:");
    auto renderer = TRY(VulkanRenderer::create(window));
    TRY(renderer->draw_test_triangle());
    outln("  Vulkan probe OK");
    return {};
}

}
