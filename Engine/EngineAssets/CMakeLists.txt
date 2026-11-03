cmake_minimum_required(VERSION 3.15)

# Layer 4: EngineAssets — asset import/cook/load (meshes, textures, shaders, geometry)
add_library(EngineAssets STATIC
    Src/AssetRegistry.cpp
    Src/AssetManager.cpp
    Src/MeshImporter.cpp
    Src/TextureManager.cpp
    ${EUNOIA_ROOT}/Plugins/Animation-Graph/Source/SkeletalAnimation.cpp
)

target_include_directories(EngineAssets PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/Include>
    $<INSTALL_INTERFACE:Include>
    ${EUNOIA_DEPS}/glm
    ${EUNOIA_DEPS}/tinyobj
    ${EUNOIA_DEPS}/cgltf
    ${EUNOIA_DEPS}/ufbx
    ${EUNOIA_DEPS}/meshoptimizer
    ${EUNOIA_DEPS}/stb
    ${EUNOIA_DEPS}/DirectXTex
    ${EUNOIA_ROOT}/Plugins/Animation-Graph/Include
)

target_link_libraries(EngineAssets PUBLIC
    EngineRHI
    EnginePlatform
    EngineCore
    ufbx_vendor
    meshoptimizer_vendor
    directxtex_vendor
)

target_compile_features(EngineAssets PUBLIC cxx_std_20)
