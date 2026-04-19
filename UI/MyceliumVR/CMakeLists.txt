include(SDL3)
include(vulkan)

find_package(EnTT CONFIG REQUIRED)
find_package(Tracy CONFIG QUIET)
find_package(VulkanMemoryAllocator CONFIG REQUIRED)

find_program(SPIRV_AS_EXECUTABLE NAMES spirv-as)
find_package(unofficial-shaderc CONFIG QUIET)

add_executable(ladybird
    main.cpp
    Application.h
    Engine/Engine.h
    Engine/Engine.cpp
    Rendering/Renderer.h
    Rendering/Renderer.cpp
    Rendering/Web/OverlayManager.h
    Rendering/Web/OverlayManager.cpp
    Rendering/Backend/VulkanProbe.h
    Rendering/Backend/VulkanProbe.cpp
    Rendering/Pipeline/TextureLibrary.h
    Rendering/Pipeline/TextureLibrary.cpp
    Rendering/Backend/VulkanRenderer.h
    Rendering/Backend/VulkanRenderer.cpp
    Rendering/VulkanRenderPass.h
    Rendering/Passes/DepthPrepass.h
    Rendering/Passes/DepthPrepass.cpp
    Rendering/Passes/GeometryPass.h
    Rendering/Passes/GeometryPass.cpp
    Rendering/Passes/ShadowPass.h
    Rendering/Passes/ShadowPass.cpp
    Rendering/Passes/UIPanelPass.h
    Rendering/Passes/UIPanelPass.cpp
    Rendering/Passes/ComputeCullPass.h
    Rendering/Passes/ComputeCullPass.cpp
    Rendering/Passes/ClusterLightAssignPass.h
    Rendering/Passes/ClusterLightAssignPass.cpp
    Rendering/RenderGraph.h
    Rendering/RenderGraph.cpp
    Rendering/Backend/VulkanContext.h
    Rendering/Backend/VulkanContext.cpp
    Rendering/VulkanCommon.h
    Rendering/Backend/VulkanDebug.h
    Rendering/Backend/VulkanDebug.cpp
    Rendering/Backend/VulkanBackend.h
    Rendering/Backend/VulkanBackend.cpp
    Rendering/Backend/VulkanResourceManager.h
    Rendering/Backend/VulkanResourceManager.cpp
    Rendering/Pipeline/VulkanMeshBuilder.h
    Rendering/Pipeline/VulkanMeshBuilder.cpp
    Rendering/Backend/VulkanSwapchain.h
    Rendering/Backend/VulkanSwapchain.cpp
    Rendering/Pipeline/VulkanPipeline.h
    Rendering/Pipeline/VulkanPipeline.cpp
    Rendering/Pipeline/VulkanPBRManager.h
    Rendering/Pipeline/VulkanPBRManager.cpp
    Rendering/Web/WebViewManager.h
    Rendering/Web/WebViewManager.cpp
    Rendering/Web/WebContentView.h
    Rendering/Web/WebContentView.cpp
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

set_source_files_properties(
    Rendering/Renderer.cpp
    Rendering/Backend/VulkanRenderer.cpp
    Rendering/Passes/DepthPrepass.cpp
    Rendering/Passes/GeometryPass.cpp
    Rendering/Passes/ShadowPass.cpp
    Rendering/Passes/UIPanelPass.cpp
    Rendering/Backend/VulkanContext.cpp
    Rendering/Backend/VulkanDebug.cpp
    Rendering/Backend/VulkanResourceManager.cpp
    Rendering/Pipeline/VulkanMeshBuilder.cpp
    Rendering/Backend/VulkanSwapchain.cpp
    Rendering/Pipeline/VulkanPipeline.cpp
    Rendering/Pipeline/VulkanPBRManager.cpp
    PROPERTIES COMPILE_FLAGS "-O0 -g3"
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
    EnTT::EnTT
    GPUOpen::VulkanMemoryAllocator
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

# Tracy profiling:
#   Debug builds   — enabled automatically if Tracy::TracyClient is found (on-demand mode,
#                    so there is zero overhead until the Tracy GUI connects).
#   Release builds — opt-in only: pass -DMYCELIUMVR_TRACY=ON to cmake to enable.
#
# NOTE: vcpkg compiles TracyClient.cpp once with the "on-demand" feature, so
# TRACY_ON_DEMAND must always be defined in our code to match the compiled library.
# The effective difference is: in debug you always have Tracy ready to attach; in
# release it is simply absent unless you ask for it.
if (TARGET Tracy::TracyClient)
    set(_tracy_default OFF)
    if (CMAKE_BUILD_TYPE STREQUAL "Debug")
        set(_tracy_default ON)
    endif()
    option(MYCELIUMVR_TRACY "Enable Tracy profiler for MyceliumVR" ${_tracy_default})

    if (MYCELIUMVR_TRACY)
        target_link_libraries(ladybird PRIVATE Tracy::TracyClient)
        # TRACY_ON_DEMAND must match the vcpkg "on-demand" feature used to compile
        # TracyClient.cpp — a mismatch causes undefined behaviour at runtime.
        target_compile_definitions(ladybird PRIVATE TRACY_ENABLE=1 TRACY_ON_DEMAND=1)
        if (CMAKE_BUILD_TYPE STREQUAL "Debug")
            message(STATUS "Tracy profiler enabled for MyceliumVR (on-demand, debug — connect profiler to start capturing)")
        else()
            message(STATUS "Tracy profiler enabled for MyceliumVR (on-demand, release — opt-in)")
        endif()
    endif()
endif()

if (HAS_VULKAN)
    target_link_libraries(ladybird PRIVATE Vulkan::Vulkan Vulkan::Headers)
endif()

target_include_directories(ladybird PRIVATE
    ${LADYBIRD_SOURCE_DIR}
)

create_ladybird_bundle(ladybird)
