cmake_minimum_required(VERSION 3.15)

# Standalone Runtime executable (no ImGui dependency)
add_executable(Eunoia-Runtime
    Src/Main.cpp
)

target_include_directories(Eunoia-Runtime PRIVATE
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
    ${EUNOIA_GLFW_ROOT}/include
)

target_link_directories(Eunoia-Runtime PRIVATE
    ${EUNOIA_GLFW_ROOT}/lib-mingw-w64
)

target_link_libraries(Eunoia-Runtime PRIVATE
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
    gdi32
)

target_compile_features(Eunoia-Runtime PRIVATE cxx_std_20)
