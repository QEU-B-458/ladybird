/*
 * MyceliumVR - a social VR platform built on the Ladybird browser engine
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "ScriptHost.h"

#include <LibJS/Runtime/VM.h>

namespace MyceliumVR {

ScriptHost::~ScriptHost() = default;

ErrorOr<void> ScriptHost::initialize()
{
    if (m_vm)
        return {};

    m_vm = JS::VM::create();
    m_vm->set_dynamic_imports_allowed(false);
    return {};
}

JS::VM& ScriptHost::vm()
{
    VERIFY(m_vm);
    return *m_vm;
}

JS::VM const& ScriptHost::vm() const
{
    VERIFY(m_vm);
    return *m_vm;
}

}
