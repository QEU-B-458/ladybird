/*
 * MyceliumVR - a social VR platform built on the Ladybird browser engine
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <AK/Error.h>
#include <AK/RefPtr.h>
#include <LibJS/Runtime/VM.h>

namespace MyceliumVR {

class ScriptHost {
public:
    ScriptHost() = default;
    ~ScriptHost();

    ErrorOr<void> initialize();

    bool is_initialized() const { return m_vm; }
    JS::VM& vm();
    JS::VM const& vm() const;

private:
    RefPtr<JS::VM> m_vm;
};

}
