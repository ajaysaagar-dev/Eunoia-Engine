cmake_minimum_required(VERSION 3.15)

# Layer 7: Editor — tool application (ImGui, ImGuizmo, Viewport, Outliner, Details)
set(EDITOR_SOURCES
    Src/Main.cpp
    Src/EngineUI.cpp
    Src/EditorGizmoSystem.cpp
    Src/Panels/OutlinerPanel.cpp
    Src/Panels/DetailsPanel.cpp
    Src/Panels/MaterialEditorPanel.cpp
    Src/Panels/EnvironmentPanel.cpp
    Src/Panels/ContentBrowserPanel.cpp
    Src/Panels/ProjectBrowserPanel.cpp
)

add_executable(Eunoia-Editor ${EDITOR_SOURCES})

# Precompiled headers for fast incremental compiles
target_precompile_headers(Eunoia-Editor PRIVATE
    <windows.h>
    <d3d12.h>
    <dxgi1_6.h>
    <wrl/client.h>
    <vector>
    <string>
    <unordered_map>
    <memory>
    <functional>
    <algorithm>
    <filesystem>
    <glm/glm.hpp>
    <imgui.h>
)

target_include_directories(Eunoia-Editor PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/Include
    ${EUNOIA_ROOT}/Plugins/Post-Process-System/Include
    ${EUNOIA_ROOT}/Plugins/Water/Include
    ${EUNOIA_ROOT}/Plugins/Graphics/Include
    ${EUNOIA_ROOT}/Plugins/UISystem/Include
    ${EUNOIA_ROOT}/Plugins/Collisions/Include
    ${EUNOIA_ROOT}/Plugins/Physics/Include
    ${EUNOIA_ROOT}/Plugins/RayTrace/Include
    ${EUNOIA_ROOT}/Plugins/SaveSystem/Include
    ${EUNOIA_ROOT}/Plugins/Texture-Streaming/Include
    ${EUNOIA_ROOT}/Plugins/Node-Behaviour/Include
    ${EUNOIA_ROOT}/Plugins/Node-Behaviour/ThirdParty
    ${EUNOIA_ROOT}/Plugins/SFLOD/Include
    ${EUNOIA_ROOT}/Plugins/Animation-Graph/Include
    ${EUNOIA_ROOT}/Plugins/Animation-Blending/Include
    ${EUNOIA_ROOT}/Plugins/Level-Animation/Include
    ${EUNOIA_ROOT}/Plugins/Character-Mechanics/Include
    ${EUNOIA_ROOT}/EunoiaUI-Framework/Include
    ${EUNOIA_DEPS}/tinyobj
    ${EUNOIA_DEPS}/cgltf
    ${EUNOIA_DEPS}/ufbx
    ${EUNOIA_DEPS}/meshoptimizer
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
    ${EUNOIA_ALL_PLUGIN_TARGETS}
    imgui_vendor
    ufbx_vendor
    meshoptimizer_vendor
    directxtex_vendor
    glfw3
    d3d12
    dxgi
    d3dcompiler
    opengl32
    gdi32
    imm32
    comdlg32
    dwmapi
)

target_compile_features(Eunoia-Editor PRIVATE cxx_std_20)
