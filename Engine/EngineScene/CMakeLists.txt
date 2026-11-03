cmake_minimum_required(VERSION 3.15)

# Layer 6: EngineScene — ECS/scene graph, gameplay-facing API, Behaviours
add_library(EngineScene STATIC
    Src/EunoiaBehaviour.cpp
    Src/SceneCacheManager.cpp
)

target_include_directories(EngineScene PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/Include>
    $<INSTALL_INTERFACE:Include>
    ${EUNOIA_DEPS}/glm
    ${EUNOIA_DEPS}/json
    ${EUNOIA_ROOT}/Plugins/UISystem/Include
    ${EUNOIA_ROOT}/Plugins/Collisions/Include
    ${EUNOIA_ROOT}/Plugins/Physics/Include
    ${EUNOIA_ROOT}/Plugins/SaveSystem/Include
    ${EUNOIA_ROOT}/Plugins/RayTrace/Include
    ${EUNOIA_ROOT}/Plugins/Character-Mechanics/Include
    ${EUNOIA_ROOT}/Plugins/Animation-Graph/Include
    ${EUNOIA_ROOT}/Plugins/Animation-Blending/Include
    ${EUNOIA_ROOT}/Plugins/Level-Animation/Include
    ${EUNOIA_ROOT}/Plugins/SFLOD/Include
    ${EUNOIA_ROOT}/Plugins/Node-Behaviour/Include
    ${EUNOIA_ROOT}/Plugins/Node-Behaviour/ThirdParty
    ${EUNOIA_ROOT}/EunoiaUI-Framework/Include
)

target_link_libraries(EngineScene PUBLIC
    EngineRenderer
    EngineAssets
    EnginePlatform
    EngineCore
)

target_compile_features(EngineScene PUBLIC cxx_std_20)
