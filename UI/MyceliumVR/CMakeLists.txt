include(SDL3)
include(vulkan)

find_program(SPIRV_AS_EXECUTABLE NAMES spirv-as)
find_package(unofficial-shaderc CONFIG QUIET)

add_executable(ladybird
    main.cpp
    Application.h
    Engine/BridgeBackend.h
    Engine/BridgeBackend.cpp
    Engine/BridgeRegistry.h
    Engine/BridgeRegistry.cpp
    Engine/Engine.h
    Engine/Engine.cpp
    Engine/Renderer.h
    Engine/Renderer.cpp
    Engine/SDKGenerator.h
    Engine/SDKGenerator.cpp
    Engine/Session.h
    Engine/Session.cpp
    Engine/ShaderCompiler.h
    Engine/ShaderCompiler.cpp
    Engine/ScriptRuntime.h
    Engine/ScriptRuntime.cpp
    Engine/VirtualFileSystem.h
    Engine/VirtualFileSystem.cpp
    Engine/VulkanProbe.h
    Engine/VulkanProbe.cpp
    Engine/WorldManifest.h
    Engine/WorldManifest.cpp
    Engine/World.h
    Engine/World.cpp
    WebContentView.h
    WebContentView.cpp
)

if (HAS_VULKAN)
    if (NOT SPIRV_AS_EXECUTABLE)
        message(FATAL_ERROR "MyceliumVR Vulkan shader assets require spirv-as. Install SPIRV-Tools or add shaderc/glslang support.")
    endif()

    set(MYCELIUMVR_SHADER_OUTPUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/Shaders")
    set(MYCELIUMVR_PROBE_VERTEX_SHADER "${MYCELIUMVR_SHADER_OUTPUT_DIR}/ProbeTriangle.vert.spv")
    set(MYCELIUMVR_PROBE_FRAGMENT_SHADER "${MYCELIUMVR_SHADER_OUTPUT_DIR}/ProbeTriangle.frag.spv")

    add_custom_command(
        OUTPUT "${MYCELIUMVR_PROBE_VERTEX_SHADER}"
        COMMAND "${CMAKE_COMMAND}" -E make_directory "${MYCELIUMVR_SHADER_OUTPUT_DIR}"
        COMMAND "${SPIRV_AS_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/Shaders/ProbeTriangle.vert.spvasm" -o "${MYCELIUMVR_PROBE_VERTEX_SHADER}"
        DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/Shaders/ProbeTriangle.vert.spvasm"
        VERBATIM
    )

    add_custom_command(
        OUTPUT "${MYCELIUMVR_PROBE_FRAGMENT_SHADER}"
        COMMAND "${CMAKE_COMMAND}" -E make_directory "${MYCELIUMVR_SHADER_OUTPUT_DIR}"
        COMMAND "${SPIRV_AS_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/Shaders/ProbeTriangle.frag.spvasm" -o "${MYCELIUMVR_PROBE_FRAGMENT_SHADER}"
        DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/Shaders/ProbeTriangle.frag.spvasm"
        VERBATIM
    )

    add_custom_target(myceliumvr_shaders
        DEPENDS
            "${MYCELIUMVR_PROBE_VERTEX_SHADER}"
            "${MYCELIUMVR_PROBE_FRAGMENT_SHADER}"
    )

    add_dependencies(ladybird myceliumvr_shaders)
    target_compile_definitions(ladybird PRIVATE
        MYCELIUMVR_SHADER_DIRECTORY="${MYCELIUMVR_SHADER_OUTPUT_DIR}"
        MYCELIUMVR_SHADER_SOURCE_DIRECTORY="${CMAKE_CURRENT_SOURCE_DIR}/Shaders"
    )
endif()

target_link_libraries(ladybird PRIVATE
    SDL3::SDL3
    LibWebView
    LibWeb
    LibGfx
    LibCore
    LibGC
    LibJS
    LibMain
    LibURL
)

if (TARGET unofficial::shaderc::shaderc)
    target_link_libraries(ladybird PRIVATE unofficial::shaderc::shaderc)
    target_compile_definitions(ladybird PRIVATE USE_SHADERC=1)
endif()

if (HAS_VULKAN)
    target_link_libraries(ladybird PRIVATE Vulkan::Vulkan Vulkan::Headers)
endif()

target_include_directories(ladybird PRIVATE
    ${LADYBIRD_SOURCE_DIR}
)

create_ladybird_bundle(ladybird)
