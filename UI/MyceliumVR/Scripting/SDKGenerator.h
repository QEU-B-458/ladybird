/*
 * MyceliumVR - a social VR platform built on the Ladybird browser engine
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <AK/StringView.h>

namespace MyceliumVR {

ErrorOr<void> generate_sdk(StringView target, StringView output_directory);

}
