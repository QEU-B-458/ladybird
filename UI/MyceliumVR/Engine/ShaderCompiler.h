/*
 * MyceliumVR - a social VR platform built on the Ladybird browser engine
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <AK/String.h>
#include <AK/StringView.h>
#include <AK/Vector.h>

namespace MyceliumVR {

class VirtualFileSystem;

enum class ShaderStage {
    Vertex,
    Fragment,
    Compute,
};

enum class ShaderSourceLanguage {
    GLSL,
    SPIRV,
};

enum class ShaderCompilerBackend {
    PrecompiledSPIRVOnly,
    ShaderC,
    Glslang,
};

struct ShaderBytecode {
    Vector<u32> words;
    String source_path;

    [[nodiscard]] size_t byte_size() const { return words.size() * sizeof(u32); }
    [[nodiscard]] u32 const* data() const { return words.data(); }
};

struct ShaderCompileRequest {
    ShaderStage stage { ShaderStage::Vertex };
    ShaderSourceLanguage language { ShaderSourceLanguage::GLSL };
    StringView source_name;
    StringView source;
};

class ShaderCompiler {
public:
    explicit ShaderCompiler(ShaderCompilerBackend = ShaderCompilerBackend::PrecompiledSPIRVOnly);

    [[nodiscard]] ShaderCompilerBackend backend() const { return m_backend; }
    [[nodiscard]] bool supports_runtime_glsl_compilation() const;

    ErrorOr<ShaderBytecode> load_spirv(VirtualFileSystem const&, StringView virtual_path) const;
    ErrorOr<ShaderBytecode> compile_glsl_to_spirv(ShaderCompileRequest const&) const;

    static ErrorOr<ShaderBytecode> parse_spirv(ReadonlyBytes, StringView source_path);

private:
    ShaderCompilerBackend m_backend { ShaderCompilerBackend::PrecompiledSPIRVOnly };
};

StringView shader_stage_name(ShaderStage);
StringView shader_compiler_backend_name(ShaderCompilerBackend);

}
