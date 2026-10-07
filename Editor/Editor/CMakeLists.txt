cmake_minimum_required(VERSION 3.15)

# Layer 7: Editor — tool application (ImGui, ImGuizmo, Viewport, Outliner, Details)
set(EDITOR_SOURCES
    Src/Main.cpp
    Src/EngineUI.cpp
    Src/EditorGizmoSystem.cpp
    ${EUNOIA_ROOT}/Plugins/Post-Process-System/Source/PostProcessSystem.cpp
    ${EUNOIA_ROOT}/Plugins/Post-Process-System/Source/PostProcessPlugin.cpp
    ${EUNOIA_ROOT}/Plugins/RayTracing/Source/RayTracingSystem.cpp
    ${EUNOIA_ROOT}/Plugins/RayTracing/Source/RayTracingPlugin.cpp
    ${EUNOIA_ROOT}/Plugins/VolumetricAtmosphere/Source/VolumetricAtmosphere.cpp
    ${EUNOIA_ROOT}/Plugins/VolumetricAtmosphere/Source/VolumetricAtmospherePlugin.cpp
    ${EUNOIA_ROOT}/Plugins/OcclusionCulling/Source/OcclusionCullingSystem.cpp
    ${EUNOIA_ROOT}/Plugins/OcclusionCulling/Source/OcclusionCullingPlugin.cpp
    # ImGui
    ${EUNOIA_DEPS}/imgui/imgui.cpp
    ${EUNOIA_DEPS}/imgui/imgui_draw.cpp
    ${EUNOIA_DEPS}/imgui/imgui_tables.cpp
    ${EUNOIA_DEPS}/imgui/imgui_widgets.cpp
    ${EUNOIA_DEPS}/imgui/backends/imgui_impl_glfw.cpp
    ${EUNOIA_DEPS}/imgui/backends/imgui_impl_dx12.cpp
    ${EUNOIA_DEPS}/imgui/ImGuizmo.cpp
)

add_executable(Eunoia-Editor ${EDITOR_SOURCES})

target_include_directories(Eunoia-Editor PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/Include
    ${EUNOIA_ROOT}/Plugins/Post-Process-System/Include
    ${EUNOIA_ROOT}/Plugins/RayTracing/Include
    ${EUNOIA_ROOT}/Plugins/VolumetricAtmosphere/Include
    ${EUNOIA_ROOT}/Plugins/OcclusionCulling/Include
    ${EUNOIA_DEPS}/imgui
    ${EUNOIA_DEPS}/imgui/backends
    ${EUNOIA_GLFW_ROOT}/include
)

target_link_directories(Eunoia-Editor PRIVATE
    ${EUNOIA_GLFW_ROOT}/lib-mingw-w64
)

target_link_libraries(Eunoia-Editor PRIVATE
    EngineScene
    EngineRenderer
    EngineAssets
    EngineRHI
    EnginePlatform
    EngineCore
    EunoiaPluginCore
    glfw3
    d3d12
    dxgi
    d3dcompiler
    gdi32
    imm32
    comdlg32
    dwmapi
)

target_compile_features(Eunoia-Editor PRIVATE cxx_std_17)
