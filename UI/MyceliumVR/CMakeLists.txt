include(SDL3)
include(vulkan)

find_program(SPIRV_AS_EXECUTABLE NAMES spirv-as)
find_package(unofficial-shaderc CONFIG QUIET)

add_executable(ladybird
    main.cpp
    Application.h
    Engine/Engine.h
    Engine/Engine.cpp
    Rendering/Renderer.h
    Rendering/Renderer.cpp
    Rendering/OverlayManager.h
    Rendering/OverlayManager.cpp
    Rendering/VulkanProbe.h
    Rendering/VulkanProbe.cpp
    Rendering/TextureLibrary.h
    Rendering/TextureLibrary.cpp
    Rendering/VulkanRenderer.h
    Rendering/VulkanRenderer.cpp
    Rendering/WebViewManager.h
    Rendering/WebViewManager.cpp
    Rendering/WebContentView.h
    Rendering/WebContentView.cpp
    Scripting/BridgeBackend.h
    Scripting/BridgeBackend.cpp
    Scripting/BridgeFunctions.h
    Scripting/BridgeFunctions.cpp
    Scripting/BridgeRegistry.h
    Scripting/BridgeRegistry.cpp
    Scripting/SDKGenerator.h
    Scripting/SDKGenerator.cpp
    Scripting/ScriptRuntime.h
    Scripting/ScriptRuntime.cpp
    Support/ShaderCompiler.h
    Support/ShaderCompiler.cpp
    Support/GltfLoader.h
    Support/GltfLoader.cpp
    Support/MeshLibrary.h
    Support/MeshLibrary.cpp
    Support/MaterialLibrary.h
    Support/MaterialLibrary.cpp
    Support/InputState.h
    Support/InputState.cpp
    Support/VirtualFileSystem.h
    Support/VirtualFileSystem.cpp
    World/Session.h
    World/Session.cpp
    World/WorldManifest.h
    World/WorldManifest.cpp
    World/World.h
    World/World.cpp
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
    LibCrypto
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
