/*
 * MyceliumVR - a social VR platform built on the Ladybird browser engine
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "ShaderCompiler.h"

#include "VirtualFileSystem.h"

#include <AK/ByteString.h>
#include <AK/Format.h>
#include <AK/HashTable.h>
#include <AK/ScopeGuard.h>
#include <AK/StringView.h>
#include <AK/StringBuilder.h>
#include <LibCrypto/Hash/SHA2.h>

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

static ErrorOr<ByteBuffer> serialize_spirv(ShaderBytecode const& bytecode)
{
    auto output = TRY(ByteBuffer::create_uninitialized(bytecode.byte_size()));
    for (size_t i = 0; i < bytecode.words.size(); ++i) {
        auto word = bytecode.words[i];
        auto byte_offset = i * sizeof(u32);
        output[byte_offset] = static_cast<u8>(word & 0xff);
        output[byte_offset + 1] = static_cast<u8>((word >> 8) & 0xff);
        output[byte_offset + 2] = static_cast<u8>((word >> 16) & 0xff);
        output[byte_offset + 3] = static_cast<u8>((word >> 24) & 0xff);
    }
    return output;
}

static String shader_cache_path(StringView cache_root, ShaderCompileRequest const& request, ShaderCompilerBackend backend)
{
    auto digest = Crypto::Hash::SHA256::hash(reinterpret_cast<u8 const*>(request.source.characters_without_null_termination()), request.source.length());
    auto filename = MUST(String::formatted(
        "{}-{}-{}.spv",
        shader_compiler_backend_name(backend),
        shader_stage_name(request.stage),
        MUST(String::formatted("{}", digest))));
    return MUST(String::formatted("{}{}", cache_root, filename));
}

static String shader_base_path(StringView source_name)
{
    auto last_slash = source_name.find_last('/');
    if (!last_slash.has_value())
        return MUST(String::from_utf8(""sv));
    return MUST(String::from_utf8(source_name.substring_view(0, *last_slash + 1).bytes()));
}

static String resolve_include_path(StringView including_source_name, StringView include_name)
{
    auto base_path = shader_base_path(including_source_name);
    return MUST(String::formatted("{}{}", base_path, include_name));
}

static ErrorOr<void> append_expanded_shader_source(
    VirtualFileSystem const& file_system,
    StringBuilder& output,
    StringView source_name,
    StringView source,
    HashTable<String>& include_stack)
{
    auto normalized_source_name = TRY(String::from_utf8(source_name.bytes()));
    if (include_stack.contains(normalized_source_name))
        return Error::from_string_literal("Recursive GLSL #include detected");

    include_stack.set(normalized_source_name);
    ScopeGuard pop_include = [&] {
        include_stack.remove(normalized_source_name);
    };

    output.appendff("// BEGIN {}\n", normalized_source_name);

    size_t line_start = 0;
    while (line_start < source.length()) {
        auto line_end = source.substring_view(line_start).find('\n').value_or(source.length() - line_start) + line_start;
        auto line = source.substring_view(line_start, line_end - line_start);
        auto trimmed = line.trim_whitespace();

        if (trimmed.starts_with("#include "sv)) {
            auto include_spec = trimmed.substring_view(9).trim_whitespace();
            if (include_spec.length() < 2 || include_spec[0] != '"' || include_spec[include_spec.length() - 1] != '"')
                return Error::from_string_literal("GLSL #include must use quoted relative paths");

            auto include_name = include_spec.substring_view(1, include_spec.length() - 2);
            auto include_path = resolve_include_path(source_name, include_name);
            auto include_bytes = TRY(file_system.read_file(include_path));
            auto include_source = TRY(String::from_utf8(StringView { include_bytes }));

            TRY(append_expanded_shader_source(
                file_system,
                output,
                include_path,
                include_source,
                include_stack));
        } else {
            output.append(line);
            output.append('\n');
        }

        line_start = line_end + 1;
    }

    output.appendff("// END {}\n", normalized_source_name);
    return {};
}

static ErrorOr<String> preprocess_glsl_includes(VirtualFileSystem const& file_system, ShaderCompileRequest const& request)
{
    HashTable<String> include_stack;
    StringBuilder builder;
    TRY(append_expanded_shader_source(file_system, builder, request.source_name, request.source, include_stack));
    return builder.to_string();
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

ErrorOr<ShaderBytecode> ShaderCompiler::load_or_compile_glsl_to_spirv(VirtualFileSystem& file_system, ShaderCompileRequest const& request, StringView cache_root) const
{
    auto expanded_source = TRY(preprocess_glsl_includes(file_system, request));
    ShaderCompileRequest expanded_request = request;
    expanded_request.source = expanded_source;

    auto cached_shader_path = shader_cache_path(cache_root, expanded_request, m_backend);
    if (file_system.exists(cached_shader_path))
        return load_spirv(file_system, cached_shader_path);

    auto bytecode = TRY(compile_glsl_to_spirv(expanded_request));
    auto serialized = TRY(serialize_spirv(bytecode));
    TRY(file_system.write_file(cached_shader_path, serialized.bytes()));
    bytecode.source_path = move(cached_shader_path);
    return bytecode;
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
