/*
 * MyceliumVR - a social VR platform built on the Ladybird browser engine
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "ShaderCompiler.h"

#include "VirtualFileSystem.h"

#include <AK/ByteString.h>
#include <AK/Format.h>

#if defined(USE_SHADERC)
#    include <shaderc/shaderc.hpp>
#endif

namespace MyceliumVR {

static constexpr u32 spirv_magic = 0x07230203;
static constexpr size_t spirv_header_word_count = 5;

static u32 read_little_endian_u32(ReadonlyBytes bytes, size_t byte_offset)
{
    VERIFY(byte_offset + sizeof(u32) <= bytes.size());
    return static_cast<u32>(bytes[byte_offset])
        | (static_cast<u32>(bytes[byte_offset + 1]) << 8)
        | (static_cast<u32>(bytes[byte_offset + 2]) << 16)
        | (static_cast<u32>(bytes[byte_offset + 3]) << 24);
}

ShaderCompiler::ShaderCompiler(ShaderCompilerBackend backend)
    : m_backend(backend)
{
#if !defined(USE_SHADERC)
    if (m_backend == ShaderCompilerBackend::ShaderC)
        m_backend = ShaderCompilerBackend::PrecompiledSPIRVOnly;
#endif
}

bool ShaderCompiler::supports_runtime_glsl_compilation() const
{
    return m_backend == ShaderCompilerBackend::ShaderC || m_backend == ShaderCompilerBackend::Glslang;
}

ErrorOr<ShaderBytecode> ShaderCompiler::load_spirv(VirtualFileSystem const& file_system, StringView virtual_path) const
{
    auto bytes = TRY(file_system.read_file(virtual_path));
    return TRY(parse_spirv(bytes.bytes(), virtual_path));
}

ErrorOr<ShaderBytecode> ShaderCompiler::compile_glsl_to_spirv(ShaderCompileRequest const& request) const
{
    if (!supports_runtime_glsl_compilation())
        return Error::from_string_literal("Runtime GLSL compilation is not available; load precompiled SPIR-V or add shaderc/glslang");

#if defined(USE_SHADERC)
    if (m_backend == ShaderCompilerBackend::ShaderC) {
        shaderc_shader_kind shader_kind;
        switch (request.stage) {
        case ShaderStage::Vertex:
            shader_kind = shaderc_vertex_shader;
            break;
        case ShaderStage::Fragment:
            shader_kind = shaderc_fragment_shader;
            break;
        case ShaderStage::Compute:
            shader_kind = shaderc_compute_shader;
            break;
        }

        auto source_name = request.source_name.is_empty()
            ? ByteString("<mycelium-shader>")
            : MUST(String::from_utf8(request.source_name.bytes())).to_byte_string();

        shaderc::CompileOptions options;
        options.SetSourceLanguage(shaderc_source_language_glsl);
        options.SetTargetEnvironment(shaderc_target_env_vulkan, shaderc_env_version_vulkan_1_0);

        shaderc::Compiler compiler;
        auto result = compiler.CompileGlslToSpv(
            reinterpret_cast<char const*>(request.source.characters_without_null_termination()),
            request.source.length(),
            shader_kind,
            source_name.characters(),
            options);

        if (result.GetCompilationStatus() != shaderc_compilation_status_success) {
            auto error_message = result.GetErrorMessage();
            return Error::from_string_view(StringView { error_message.data(), error_message.length() });
        }

        ShaderBytecode bytecode;
        bytecode.source_path = MUST(String::from_utf8(request.source_name.bytes()));
        bytecode.words.ensure_capacity(result.cend() - result.cbegin());
        for (auto word : result)
            bytecode.words.unchecked_append(word);

        return bytecode;
    }
#endif

    // Glslang remains as the fallback slot if shaderc does not fit a platform.
    return Error::from_string_literal("Runtime GLSL compiler backend is not implemented yet");
}

ErrorOr<ShaderBytecode> ShaderCompiler::parse_spirv(ReadonlyBytes bytes, StringView source_path)
{
    if (bytes.is_empty())
        return Error::from_string_literal("SPIR-V bytecode is empty");
    if ((bytes.size() % sizeof(u32)) != 0)
        return Error::from_string_literal("SPIR-V bytecode size is not a multiple of 4");

    auto word_count = bytes.size() / sizeof(u32);
    if (word_count < spirv_header_word_count)
        return Error::from_string_literal("SPIR-V bytecode is shorter than the required header");

    if (read_little_endian_u32(bytes, 0) != spirv_magic)
        return Error::from_string_literal("SPIR-V bytecode has an invalid magic value");

    ShaderBytecode bytecode;
    bytecode.source_path = MUST(String::from_utf8(source_path.bytes()));
    bytecode.words.resize(word_count);
    for (size_t i = 0; i < word_count; ++i)
        bytecode.words[i] = read_little_endian_u32(bytes, i * sizeof(u32));

    return bytecode;
}

StringView shader_stage_name(ShaderStage stage)
{
    switch (stage) {
    case ShaderStage::Vertex:
        return "vertex"sv;
    case ShaderStage::Fragment:
        return "fragment"sv;
    case ShaderStage::Compute:
        return "compute"sv;
    }
    VERIFY_NOT_REACHED();
}

StringView shader_compiler_backend_name(ShaderCompilerBackend backend)
{
    switch (backend) {
    case ShaderCompilerBackend::PrecompiledSPIRVOnly:
        return "precompiled-spirv-only"sv;
    case ShaderCompilerBackend::ShaderC:
        return "shaderc"sv;
    case ShaderCompilerBackend::Glslang:
        return "glslang"sv;
    }
    VERIFY_NOT_REACHED();
}

}
